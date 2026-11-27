#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdio.h>
#include "../positron_archive/positron_archive.h"
#include "app_pxi.h"
#include "app_runtime_registry.h"
#include "app_debug.h"

#define PXI_OBJECTS 256U
#define PXI_BYTES (4U*1024U*1024U)
#define PXI_RECLAIM_MAX_CANDIDATES 64U
typedef struct PxiObject {
    WCHAR path[MAX_PATH];
    DWORD oid,attributes;
    HANDLE pin;
} PxiObject;
typedef struct PxiLedgerItem {
    WCHAR path[MAX_PATH];
    DWORD oid;
    DWORD attributes;
    DWORD size_high;
    DWORD size_low;
    DWORD mtime_high;
    DWORD mtime_low;
    int present;
    int matched;
} PxiLedgerItem;
struct AppPxi {
    HANDLE input,anchor;
    BY_HANDLE_FILE_INFORMATION input_info;
    PArchiveReader reader;
    PxiObject objects[PXI_OBJECTS];
    unsigned int count;
    int retained;
    int ownership_written;
    int (*cancelled)(void *);
    void *user;
    char directory[1024];
    unsigned char block[PARCHIVE_CHUNK_BYTES];
};
#ifdef _DEBUG
static void (*pxi_debug_hook)(const char *,void *);
static void *pxi_debug_user;
void AppPxi_DebugHook(void (*hook)(const char *,void *),void *user)
{ pxi_debug_hook=hook; pxi_debug_user=user; }
#endif
static int pxi_cancelled(AppPxi *p) { return p->cancelled && p->cancelled(p->user); }
static int pxi_same_input(AppPxi *p)
{
    BY_HANDLE_FILE_INFORMATION now,*old;
    old=&p->input_info;
    return GetFileInformationByHandle(p->input,&now) && now.dwOID==old->dwOID &&
        now.nFileIndexLow==old->nFileIndexLow && now.nFileIndexHigh==old->nFileIndexHigh &&
        now.dwVolumeSerialNumber==old->dwVolumeSerialNumber && now.nFileSizeLow==old->nFileSizeLow &&
        now.nFileSizeHigh==old->nFileSizeHigh && now.dwFileAttributes==old->dwFileAttributes &&
        !memcmp(&now.ftLastWriteTime,&old->ftLastWriteTime,sizeof(FILETIME));
}
static unsigned int pxi_read(void *user,unsigned int offset,void *buffer,unsigned int bytes)
{
    AppPxi *p;
    DWORD got;
    p=(AppPxi *)user; got=0;
    if(pxi_cancelled(p) || !pxi_same_input(p) ||
        SetFilePointer(p->input,offset,NULL,FILE_BEGIN)==INVALID_SET_FILE_POINTER ||
        !ReadFile(p->input,buffer,bytes,&got,NULL) || !pxi_same_input(p)) return 0;
    return got;
}
static int pxi_archive_result(int rc)
{
    if(rc==PARCHIVE_LIMIT) return PRUNTIME_LIMIT;
    if(rc==PARCHIVE_MEMORY) return PRUNTIME_MEMORY;
    if(rc==PARCHIVE_UNSUPPORTED) return PRUNTIME_UNSUPPORTED;
    if(rc==PARCHIVE_IO) return PRUNTIME_IO;
    return PRUNTIME_INVALID;
}
static int pxi_observe(const WCHAR *path,WIN32_FIND_DATAW *data)
{
    HANDLE find;
    find=FindFirstFileW(path,data);
    if(find==INVALID_HANDLE_VALUE) return PRUNTIME_IO;
    FindClose(find); return PRUNTIME_OK;
}
static int pxi_record(AppPxi *p,const WCHAR *path,int directory)
{
    WIN32_FIND_DATAW data;
    WCHAR canonical[MAX_PATH];
    PxiObject *item;
    if(pxi_observe(path,&data)!=PRUNTIME_OK || !data.dwOID ||
        AppRuntimePlatform_CanonicalPath(path,canonical,directory)!=PRUNTIME_OK || wcscmp(path,canonical)) return PRUNTIME_IO;
    item=&p->objects[p->count-1];
    item->oid=data.dwOID; item->attributes=data.dwFileAttributes;
    return PRUNTIME_OK;
}
/* Reserve BEFORE creating, so even an observation failure remembers the path.
 * Such an unproven object will not be deleted by cleanup. */
static PxiObject *pxi_reserve(AppPxi *p,const WCHAR *path)
{
    PxiObject *item;
    if(p->count==PXI_OBJECTS) return NULL;
    item=&p->objects[p->count++]; wcscpy(item->path,path); return item;
}
static int pxi_directory(AppPxi *p,const WCHAR *path)
{
    unsigned int n;
    PxiObject *item;
    WIN32_FIND_DATAW data;
    WCHAR canonical[MAX_PATH];
    for(n=0;n<p->count;++n) if(!wcscmp(p->objects[n].path,path)) {
        if(!(p->objects[n].attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            pxi_observe(path,&data)!=PRUNTIME_OK || data.dwOID!=p->objects[n].oid ||
            AppRuntimePlatform_CanonicalPath(path,canonical,1)!=PRUNTIME_OK || wcscmp(path,canonical)) return PRUNTIME_IO;
        return PRUNTIME_OK;
    }
    item=pxi_reserve(p,path); if(!item) return PRUNTIME_LIMIT;
    if(!CreateDirectoryW(path,NULL)) { --p->count; return PRUNTIME_IO; }
    return pxi_record(p,path,1);
}
static int pxi_output_root(AppPxi *p)
{
    WCHAR exe[MAX_PATH],base[MAX_PATH],root[MAX_PATH],anchor[MAX_PATH],id[33],*slash;
    char random[33];
    DWORD error,length;
    BY_HANDLE_FILE_INFORMATION info;
    int rc;
    length=GetModuleFileNameW(NULL,exe,MAX_PATH);
    if(!length || length>=MAX_PATH) return PRUNTIME_IO;
    slash=wcsrchr(exe,L'\\'); if(!slash) return PRUNTIME_IO; *slash=0;
    if(wcslen(exe)+80>=MAX_PATH) return PRUNTIME_LIMIT;
    wcscpy(base,exe); wcscat(base,L"\\apps");
    if(!CreateDirectoryW(base,NULL)) {
        error=GetLastError(); if(error!=ERROR_ALREADY_EXISTS) return PRUNTIME_IO;
    }
    rc=AppRuntimePlatform_CanonicalPath(base,root,1);
    if(rc!=PRUNTIME_OK) return rc;
    wcscpy(base,root); wcscpy(anchor,base); wcscat(anchor,L"\\.positron-runtime-apps");
    p->anchor=CreateFileW(anchor,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN,NULL);
    if(p->anchor==INVALID_HANDLE_VALUE) { p->anchor=NULL; return PRUNTIME_IO; }
    if(AppRuntimePlatform_CanonicalPath(anchor,root,0)!=PRUNTIME_OK || wcscmp(anchor,root) ||
        !GetFileInformationByHandle(p->anchor,&info) || info.nNumberOfLinks>1 ||
        info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY|0x400)) return PRUNTIME_IO;
    rc=AppRuntimeRegistry_NewId(random); if(rc!=PRUNTIME_OK) return rc;
    if(!MultiByteToWideChar(CP_UTF8,0,random,-1,id,33)) return PRUNTIME_IO;
    wcscpy(root,base); wcscat(root,L"\\pxi-"); wcscat(root,id);
    rc=pxi_directory(p,root);
    if(rc!=PRUNTIME_OK) return rc;
    if(!WideCharToMultiByte(CP_UTF8,0,root,-1,p->directory,sizeof(p->directory),NULL,NULL)) return PRUNTIME_LIMIT;
    return PRUNTIME_OK;
}
static int pxi_target(AppPxi *p,const char *relative,int directory,WCHAR *output)
{
    WCHAR key[PARCHIVE_PATH_BYTES+1],path[MAX_PATH],*cursor;
    int n,rc;
    n=MultiByteToWideChar(CP_UTF8,0,relative,-1,key,PARCHIVE_PATH_BYTES+1);
    if(n<1 || !p->count || wcslen(p->objects[0].path)+wcslen(key)+2>=MAX_PATH) return PRUNTIME_LIMIT;
    wcscpy(path,p->objects[0].path); wcscat(path,L"\\"); n=(int)wcslen(path);
    wcscat(path,key);
    for(cursor=path+n;*cursor;++cursor) if(*cursor==L'/') *cursor=L'\\';
    n=(int)wcslen(path); if(n && path[n-1]==L'\\') path[n-1]=0;
    for(cursor=path+wcslen(p->objects[0].path)+1;*cursor;++cursor) if(*cursor==L'\\') {
        *cursor=0; rc=pxi_directory(p,path); *cursor=L'\\';
        if(rc!=PRUNTIME_OK) return rc;
    }
    if(directory) { rc=pxi_directory(p,path); if(rc!=PRUNTIME_OK) return rc; }
    wcscpy(output,path); return PRUNTIME_OK;
}
static int pxi_extract_entry(AppPxi *p,unsigned int index,PArchiveEntry *entry)
{
    WCHAR path[MAX_PATH];
    PxiObject *item;
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION info,now;
    unsigned int got,total;
    DWORD written;
    int rc,result;
    rc=pxi_target(p,entry->path,entry->is_directory,path);
    if(rc!=PRUNTIME_OK) return rc;
    file=NULL; item=NULL;
    if(!entry->is_directory) {
        item=pxi_reserve(p,path); if(!item) return PRUNTIME_LIMIT;
        file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,0,NULL);
        if(file==INVALID_HANDLE_VALUE) { --p->count; return PRUNTIME_IO; }
        rc=pxi_record(p,path,0);
        if(rc!=PRUNTIME_OK) { CloseHandle(file); return rc; }
    }
    rc=PArchive_Begin(p->reader,index); total=0;
    result=rc==PARCHIVE_OK ? PRUNTIME_OK : pxi_archive_result(rc);
    while(rc==PARCHIVE_OK) {
        if(pxi_cancelled(p)) { result=PRUNTIME_CANCELLED; break; }
        got=0; rc=PArchive_Read(p->reader,p->block,sizeof(p->block),&got);
        if(rc<0) { result=pxi_archive_result(rc); break; }
        if(got && (!file || total>entry->uncompressed_bytes || got>entry->uncompressed_bytes-total ||
            !WriteFile(file,p->block,got,&written,NULL) || written!=got)) { result=PRUNTIME_IO; break; }
        total+=got;
    }
    if(result==PRUNTIME_OK && (rc!=PARCHIVE_DONE || total!=entry->uncompressed_bytes)) result=PRUNTIME_INVALID;
    if(file) {
        if(result==PRUNTIME_OK && (!FlushFileBuffers(file) || !GetFileInformationByHandle(file,&info))) result=PRUNTIME_IO;
        CloseHandle(file);
        if(result==PRUNTIME_OK) {
            item->pin=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
            if(item->pin==INVALID_HANDLE_VALUE) { item->pin=NULL; result=PRUNTIME_IO; }
            else if(!GetFileInformationByHandle(item->pin,&now) || now.dwOID!=info.dwOID ||
                now.nFileIndexLow!=info.nFileIndexLow || now.nFileIndexHigh!=info.nFileIndexHigh ||
                now.nFileSizeLow!=total || now.nFileSizeHigh || now.nNumberOfLinks>1 ||
                now.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY|0x400)) result=PRUNTIME_IO;
        }
    }
    if(pxi_cancelled(p)) result=PRUNTIME_CANCELLED;
    return result;
}
int AppPxi_Extract(const char *package,int (*cancelled)(void *),void *user,AppPxi **out)
{
    AppPxi *p;
    PArchiveSource source;
    PArchiveLimits limits;
    PArchiveEntry entry;
    WCHAR path[MAX_PATH],canonical[MAX_PATH];
    unsigned int count,index;
    int rc;
    p=(AppPxi *)calloc(1,sizeof(*p)); if(!p) return PRUNTIME_MEMORY;
    p->cancelled=cancelled; p->user=user;
    if(!package || !MultiByteToWideChar(CP_UTF8,0,package,-1,path,MAX_PATH)) { rc=PRUNTIME_INVALID; goto done; }
    rc=AppRuntimePlatform_CanonicalPath(path,canonical,0);
    if(rc!=PRUNTIME_OK) goto done;
    p->input=CreateFileW(canonical,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(p->input==INVALID_HANDLE_VALUE) { p->input=NULL; rc=PRUNTIME_IO; goto done; }
    if(!GetFileInformationByHandle(p->input,&p->input_info) || p->input_info.nFileSizeHigh ||
        p->input_info.nNumberOfLinks>1 || p->input_info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY|0x400)) { rc=PRUNTIME_IO; goto done; }
    if(p->input_info.nFileSizeLow>16U*1024U*1024U) { rc=PRUNTIME_LIMIT; goto done; }
    memset(&source,0,sizeof(source)); source.size=sizeof(source); source.version=1;
    source.archive_bytes=p->input_info.nFileSizeLow; source.read_at=pxi_read; source.user_data=p;
    memset(&limits,0,sizeof(limits)); limits.size=sizeof(limits); limits.version=1;
    limits.max_entries=128; limits.max_entry_bytes=PXI_BYTES; limits.max_total_bytes=PXI_BYTES;
    limits.max_memory_bytes=1024U*1024U;
    rc=PArchive_Open(&source,&limits,&p->reader);
    if(rc!=PARCHIVE_OK) { rc=pxi_archive_result(rc); goto done; }
    rc=PArchive_Count(p->reader,&count); if(rc!=PARCHIVE_OK) { rc=pxi_archive_result(rc); goto done; }
    rc=pxi_output_root(p); if(rc!=PRUNTIME_OK) goto done;
#ifdef _DEBUG
    if(pxi_debug_hook) {
        void (*hook)(const char *,void *);
        void *hook_user;
        hook=pxi_debug_hook; hook_user=pxi_debug_user; pxi_debug_hook=NULL; pxi_debug_user=NULL;
        hook(p->directory,hook_user);
    }
#endif
    for(index=0;index<count;++index) {
        if(pxi_cancelled(p)) { rc=PRUNTIME_CANCELLED; break; }
        entry.size=sizeof(entry); entry.version=1;
        rc=PArchive_Entry(p->reader,index,&entry);
        if(rc!=PARCHIVE_OK) { rc=pxi_archive_result(rc); break; }
        rc=pxi_extract_entry(p,index,&entry); if(rc!=PRUNTIME_OK) break;
    }
    if(rc==PRUNTIME_OK && !pxi_same_input(p)) rc=PRUNTIME_IO;
done:
    PArchive_Close(p->reader); p->reader=NULL;
    if(p->input) { CloseHandle(p->input); p->input=NULL; }
    if(pxi_cancelled(p)) rc=PRUNTIME_CANCELLED;
    if(rc==PRUNTIME_OK) *out=p;
    else (void)AppPxi_Close(&p);
    return rc;
}
const char *AppPxi_Directory(AppPxi *p) { return p->directory; }
void AppPxi_Retain(AppPxi *p) { if(p) p->retained=1; }

/* Store outside the package so the Runtime digest covers author files only.
 * An incomplete write cannot authorize reclamation. The reader must require
 * the declared record count, exact EOF and a fresh full identity check. */
int AppPxi_RecordOwnership(AppPxi *p)
{
    WCHAR path[MAX_PATH],temporary[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE file;
    DWORD written;
    unsigned int n;
    int length,rc;
    char relative[MAX_PATH*3];
    char line[MAX_PATH*3+128];
    if(!p || p->ownership_written) return PRUNTIME_OK;
    if(!p->count || wcslen(p->objects[0].path)+15>=MAX_PATH) return PRUNTIME_LIMIT;
    wcscpy(path,p->objects[0].path); wcscat(path,L".ownership");
    wcscpy(temporary,path); wcscat(temporary,L".tmp");
    file=CreateFileW(temporary,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) return PRUNTIME_IO;
    length=sprintf(line,"PXI-OWNERSHIP 1 %u\n",p->count);
    rc=WriteFile(file,line,length,&written,NULL) && written==(DWORD)length ? PRUNTIME_OK : PRUNTIME_IO;
    for(n=0;rc==PRUNTIME_OK && n<p->count;++n) {
        PxiObject *item;
        item=&p->objects[n];
        if(pxi_observe(item->path,&data)!=PRUNTIME_OK || !item->oid ||
                item->oid!=data.dwOID || item->attributes!=data.dwFileAttributes ||
                !WideCharToMultiByte(CP_UTF8,0,item->path+wcslen(p->objects[0].path),-1,
                    relative,sizeof(relative),NULL,NULL) ||
                strpbrk(relative,"\r\n\t")) { rc=PRUNTIME_IO; break; }
        length=sprintf(line,"%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%s\n",
                (unsigned long)data.dwOID,(unsigned long)data.dwFileAttributes,
                (unsigned long)data.nFileSizeHigh,(unsigned long)data.nFileSizeLow,
                (unsigned long)data.ftLastWriteTime.dwHighDateTime,
                (unsigned long)data.ftLastWriteTime.dwLowDateTime,relative);
        if(!WriteFile(file,line,length,&written,NULL) || written!=(DWORD)length) rc=PRUNTIME_IO;
    }
    if(rc==PRUNTIME_OK && !FlushFileBuffers(file)) rc=PRUNTIME_IO;
    if(!CloseHandle(file)) rc=PRUNTIME_IO;
    if(rc==PRUNTIME_OK && !MoveFileW(temporary,path)) rc=PRUNTIME_IO;
    if(rc==PRUNTIME_OK) p->ownership_written=1;
    /* A failed write leaves only a temporary file, never a published ledger. */
    return rc;
}

static int pxi_ledger_number(char **cursor,DWORD *value)
{
    DWORD number,digit;
    char *p;
    p=*cursor; number=0;
    if(*p<'0' || *p>'9') return 0;
    while(*p>='0' && *p<='9') {
        digit=(DWORD)(*p++-'0');
        if(number>(0xffffffffUL-digit)/10) return 0;
        number=number*10+digit;
    }
    if(*p!='\t') return 0;
    *value=number; *cursor=p+1; return 1;
}

static int pxi_observe_status(const WCHAR *path,WIN32_FIND_DATAW *data,int *not_found)
{
    HANDLE find;
    DWORD error;
    if(not_found) *not_found=0;
    SetLastError(0);
    find=FindFirstFileW(path,data);
    if(find==INVALID_HANDLE_VALUE) {
        error=GetLastError();
        if(not_found && (error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND ||
                error==ERROR_NO_MORE_FILES))
            *not_found=1;
        return PRUNTIME_IO;
    }
    FindClose(find);
    return PRUNTIME_OK;
}

static int pxi_ledger_entry(const WCHAR *root,char *line,int first,int allow_missing,
        unsigned long *total,PxiLedgerItem *items,unsigned int index)
{
    WCHAR path[MAX_PATH],canonical[MAX_PATH];
    WIN32_FIND_DATAW observed;
    PxiLedgerItem *item;
    DWORD fields[6];
    unsigned int i;
    size_t length;
    int directory,not_found;
    item=&items[index];
    length=wcslen(root);
    for(i=0;i<6;++i) if(!pxi_ledger_number(&line,&fields[i])) return PRUNTIME_INVALID;
    if(first ? *line!=0 : (*line!='\\' || !line[1])) return PRUNTIME_INVALID;
    if(strpbrk(line,"\t\r") || fields[0]==0 || fields[2]) return PRUNTIME_INVALID;
    wcscpy(path,root);
    if(!MultiByteToWideChar(CP_UTF8,0,line,-1,path+length,(int)(MAX_PATH-length)))
        return PRUNTIME_INVALID;
    for(i=0;i<index;++i) if(!wcscmp(items[i].path,path)) return PRUNTIME_INVALID;
    directory=(fields[1]&FILE_ATTRIBUTE_DIRECTORY)!=0;
    if(first && !directory) return PRUNTIME_INVALID;
    wcscpy(item->path,path);
    item->oid=fields[0];
    item->attributes=fields[1];
    item->size_high=fields[2];
    item->size_low=fields[3];
    item->mtime_high=fields[4];
    item->mtime_low=fields[5];
    item->present=0;
    item->matched=0;
    if(pxi_observe_status(path,&observed,&not_found)!=PRUNTIME_OK) {
        if(allow_missing && not_found) return PRUNTIME_OK;
        return PRUNTIME_IO;
    }
    if(AppRuntimePlatform_CanonicalPath(path,canonical,directory)!=PRUNTIME_OK ||
            wcscmp(path,canonical) ||
            observed.dwOID!=fields[0] || observed.dwFileAttributes!=fields[1] ||
            observed.nFileSizeHigh!=fields[2] || observed.nFileSizeLow!=fields[3])
        return PRUNTIME_IO;
    /* Deleting child files inside a directory updates directory mtime on FAT/CE.
     * Regular files always require exact mtime; directories require exact mtime
     * during fresh preflight and exact OID/attributes/size/path during resume. */
    if((!directory || !allow_missing) &&
            (observed.ftLastWriteTime.dwHighDateTime!=fields[4] ||
             observed.ftLastWriteTime.dwLowDateTime!=fields[5]))
        return PRUNTIME_IO;
    item->present=1;
    if(!directory) {
        if(fields[3]>PXI_BYTES-*total) return PRUNTIME_LIMIT;
        *total+=fields[3];
    }
    return PRUNTIME_OK;
}

static int pxi_ledger_tree(PxiLedgerItem *items,unsigned int count)
{
    WIN32_FIND_DATAW data;
    WCHAR path[MAX_PATH];
    HANDLE find;
    unsigned int i,j,seen,expected;
    size_t length;
    DWORD error;
    expected=0;
    for(i=0;i<count;++i) {
        items[i].matched=0;
        if(items[i].present) ++expected;
    }
    if(!expected) return PRUNTIME_OK;
    if(!items[0].present) return PRUNTIME_INVALID;
    items[0].matched=1;
    seen=1;
    for(i=0;i<count;++i) {
        if(!items[i].present || !(items[i].attributes&FILE_ATTRIBUTE_DIRECTORY)) continue;
        length=wcslen(items[i].path);
        if(length+3>MAX_PATH) return PRUNTIME_LIMIT;
        wcscpy(path,items[i].path); wcscat(path,L"\\*");
        find=FindFirstFileW(path,&data);
        if(find==INVALID_HANDLE_VALUE) {
            error=GetLastError();
            if(error==ERROR_FILE_NOT_FOUND || error==ERROR_NO_MORE_FILES) continue;
            return PRUNTIME_IO;
        }
        do {
            if(!wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L"..")) continue;
            if(length+wcslen(data.cFileName)+2>MAX_PATH) {
                FindClose(find); return PRUNTIME_LIMIT;
            }
            wcscpy(path+length+1,data.cFileName);
            for(j=i+1;j<count;++j) {
                if(items[j].present && !items[j].matched && !wcscmp(path,items[j].path))
                    break;
            }
            if(j==count || ++seen>expected) {
                FindClose(find); return PRUNTIME_INVALID;
            }
            items[j].matched=1;
        } while(FindNextFileW(find,&data));
        error=GetLastError();
        FindClose(find);
        if(error!=ERROR_NO_MORE_FILES) return PRUNTIME_IO;
    }
    return seen==expected ? PRUNTIME_OK : PRUNTIME_INVALID;
}

static int pxi_load_ledger(const WCHAR *root,int allow_missing,
        PxiLedgerItem **out_items,unsigned int *out_count,unsigned long *out_bytes)
{
    static const char header[]="PXI-OWNERSHIP 1 ";
    WCHAR sidecar[MAX_PATH],canonical[MAX_PATH];
    HANDLE file;
    char *buffer,*cursor,*end;
    PxiLedgerItem *items;
    DWORD size,read,count;
    unsigned int i;
    unsigned long total;
    int rc;
    if(!root || !out_items || !out_count || !out_bytes || wcslen(root)+16>=MAX_PATH)
        return PRUNTIME_ARGUMENT;
    *out_items=NULL; *out_count=0; *out_bytes=0;
    if(!allow_missing && (AppRuntimePlatform_CanonicalPath(root,canonical,1)!=PRUNTIME_OK ||
            wcscmp(root,canonical)))
        return PRUNTIME_INVALID;
    wcscpy(sidecar,root); wcscat(sidecar,L".ownership");
    if(AppRuntimePlatform_CanonicalPath(sidecar,canonical,0)!=PRUNTIME_OK || wcscmp(sidecar,canonical))
        return PRUNTIME_INVALID;
    file=CreateFileW(sidecar,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(file==INVALID_HANDLE_VALUE) return PRUNTIME_IO;
    size=GetFileSize(file,NULL);
    if(size==0xFFFFFFFFUL || size<sizeof(header) || size>PXI_OBJECTS*(MAX_PATH*3+128U)) {
        CloseHandle(file); return PRUNTIME_LIMIT;
    }
    buffer=(char *)malloc(size+1);
    if(!buffer) { CloseHandle(file); return PRUNTIME_MEMORY; }
    rc=PRUNTIME_INVALID;
    items=NULL;
    if(!ReadFile(file,buffer,size,&read,NULL) || read!=size) { rc=PRUNTIME_IO; goto done; }
    if(memchr(buffer,0,size) || memcmp(buffer,header,sizeof(header)-1)) goto done;
    buffer[size]=0;
    cursor=buffer+sizeof(header)-1;
    end=strchr(cursor,'\n');
    if(!end) goto done;
    *end='\t';
    if(!pxi_ledger_number(&cursor,&count) || cursor!=end+1 || count<1 || count>PXI_OBJECTS) goto done;
    items=(PxiLedgerItem *)calloc(count,sizeof(*items));
    if(!items) { rc=PRUNTIME_MEMORY; goto done; }
    total=0;
    for(i=0;i<count;++i) {
        end=strchr(cursor,'\n');
        if(!end) { rc=PRUNTIME_INVALID; goto done; }
        *end=0;
        rc=pxi_ledger_entry(root,cursor,i==0,allow_missing,&total,items,i);
        if(rc!=PRUNTIME_OK) goto done;
        cursor=end+1;
    }
    rc=*cursor ? PRUNTIME_INVALID : pxi_ledger_tree(items,count);
    if(rc==PRUNTIME_OK) {
        *out_items=items; *out_count=count; *out_bytes=total;
        items=NULL;
    }
done:
    free(items);
    free(buffer);
    CloseHandle(file);
    return rc;
}

int AppPxi_CheckOwnership(const WCHAR *root,unsigned long *bytes)
{
    PxiLedgerItem *items;
    unsigned int count;
    unsigned long total;
    int rc;
    if(!root || !bytes) return PRUNTIME_ARGUMENT;
    items=NULL; count=0; total=0;
    rc=pxi_load_ledger(root,0,&items,&count,&total);
    free(items);
    if(rc==PRUNTIME_OK) *bytes=total;
    return rc;
}

static int pxi_apps_base(WCHAR *canonical_apps,int *missing)
{
    WCHAR exe[MAX_PATH],base[MAX_PATH],*slash;
    WIN32_FIND_DATAW data;
    DWORD length;
    int not_found;
    if(missing) *missing=0;
    length=GetModuleFileNameW(NULL,exe,MAX_PATH);
    if(!length || length>=MAX_PATH) return PRUNTIME_IO;
    slash=wcsrchr(exe,L'\\');
    if(!slash || slash==exe) return PRUNTIME_IO;
    *slash=0;
    if(wcslen(exe)+16>=MAX_PATH) return PRUNTIME_LIMIT;
    wcscpy(base,exe); wcscat(base,L"\\apps");
    if(pxi_observe_status(base,&data,&not_found)!=PRUNTIME_OK) {
        if(missing && not_found) { *missing=1; return PRUNTIME_OK; }
        return PRUNTIME_IO;
    }
    return AppRuntimePlatform_CanonicalPath(base,canonical_apps,1);
}

static int pxi_validate_apps_child(const WCHAR *root,WCHAR *canonical_root,int *root_missing)
{
    WCHAR apps[MAX_PATH];
    const WCHAR *child,*cursor;
    WIN32_FIND_DATAW data;
    size_t apps_len;
    int apps_missing,not_found,rc;
    if(!root || !canonical_root || !root_missing || wcslen(root)+16>=MAX_PATH)
        return PRUNTIME_ARGUMENT;
    *root_missing=0;
    rc=pxi_apps_base(apps,&apps_missing);
    if(rc!=PRUNTIME_OK) return rc;
    if(apps_missing) return PRUNTIME_NOT_FOUND;
    apps_len=wcslen(apps);
    if(_wcsnicmp(root,apps,apps_len) || root[apps_len]!=L'\\')
        return PRUNTIME_INVALID;
    child=root+apps_len+1;
    if(wcsncmp(child,L"pxi-",4) || !child[4]) return PRUNTIME_INVALID;
    for(cursor=child;*cursor;++cursor) {
        if(!((*cursor>=L'a' && *cursor<=L'z') || (*cursor>=L'A' && *cursor<=L'Z') ||
                (*cursor>=L'0' && *cursor<=L'9') || *cursor==L'-' || *cursor==L'_'))
            return PRUNTIME_INVALID;
    }
    if(pxi_observe_status(root,&data,&not_found)!=PRUNTIME_OK) {
        if(!not_found) return PRUNTIME_IO;
        *root_missing=1;
        wcscpy(canonical_root,apps);
        wcscat(canonical_root,L"\\");
        wcscat(canonical_root,child);
        return PRUNTIME_OK;
    }
    rc=AppRuntimePlatform_CanonicalPath(root,canonical_root,1);
    if(rc!=PRUNTIME_OK || wcscmp(root,canonical_root)) return PRUNTIME_INVALID;
    return PRUNTIME_OK;
}

static int pxi_read_reclaim_marker(const WCHAR *reclaim_path,const char *expected_root,int *exists)
{
    static const char prefix[]="PXI-RECLAIM 1\n";
    WCHAR canonical[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE file;
    char *expected,*actual;
    DWORD size,read,expected_len;
    int not_found,rc;
    *exists=0;
    if(pxi_observe_status(reclaim_path,&data,&not_found)!=PRUNTIME_OK)
        return not_found ? PRUNTIME_OK : PRUNTIME_IO;
    if(AppRuntimePlatform_CanonicalPath(reclaim_path,canonical,0)!=PRUNTIME_OK ||
            wcscmp(reclaim_path,canonical))
        return PRUNTIME_INVALID;
    expected_len=(DWORD)(sizeof(prefix)-1+strlen(expected_root)+1);
    file=CreateFileW(reclaim_path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(file==INVALID_HANDLE_VALUE) return PRUNTIME_IO;
    size=GetFileSize(file,NULL);
    if(size!=expected_len) { CloseHandle(file); return PRUNTIME_INVALID; }
    expected=(char *)malloc(expected_len+1);
    actual=(char *)malloc(expected_len+1);
    if(!expected || !actual) {
        free(expected); free(actual); CloseHandle(file); return PRUNTIME_MEMORY;
    }
    sprintf(expected,"%s%s\n",prefix,expected_root);
    rc=ReadFile(file,actual,expected_len,&read,NULL) && read==expected_len &&
            !memcmp(actual,expected,expected_len) ? PRUNTIME_OK : PRUNTIME_INVALID;
    if(rc==PRUNTIME_OK) *exists=1;
    free(expected); free(actual); CloseHandle(file);
    return rc;
}

static int pxi_write_reclaim_marker(const WCHAR *reclaim_path,const char *utf8_root)
{
    WCHAR temporary[MAX_PATH];
    HANDLE file;
    char *body;
    DWORD length,written;
    int rc;
    if(wcslen(reclaim_path)+5>=MAX_PATH) return PRUNTIME_LIMIT;
    wcscpy(temporary,reclaim_path); wcscat(temporary,L".tmp");
    length=(DWORD)(14+strlen(utf8_root)+1);
    body=(char *)malloc(length+1);
    if(!body) return PRUNTIME_MEMORY;
    sprintf(body,"PXI-RECLAIM 1\n%s\n",utf8_root);
    file=CreateFileW(temporary,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) { free(body); return PRUNTIME_IO; }
    rc=WriteFile(file,body,length,&written,NULL) && written==length &&
            FlushFileBuffers(file) ? PRUNTIME_OK : PRUNTIME_IO;
    if(!CloseHandle(file)) rc=PRUNTIME_IO;
    if(rc==PRUNTIME_OK && !MoveFileW(temporary,reclaim_path)) rc=PRUNTIME_IO;
    if(rc!=PRUNTIME_OK) (void)DeleteFileW(temporary);
    free(body);
    return rc;
}

static int pxi_delete_sidecar(const WCHAR *path)
{
    WCHAR canonical[MAX_PATH];
    WIN32_FIND_DATAW data;
    int not_found;
    if(pxi_observe_status(path,&data,&not_found)!=PRUNTIME_OK)
        return not_found ? PRUNTIME_OK : PRUNTIME_IO;
    if((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            AppRuntimePlatform_CanonicalPath(path,canonical,0)!=PRUNTIME_OK ||
            wcscmp(path,canonical))
        return PRUNTIME_INVALID;
    return DeleteFileW(path) ? PRUNTIME_OK : PRUNTIME_IO;
}

typedef struct PxiReclaimWork {
    WCHAR canonical_root[MAX_PATH];
    WCHAR sidecar_path[MAX_PATH];
    WCHAR reclaim_path[MAX_PATH];
    WCHAR verify_path[MAX_PATH];
    char utf8_root[1024];
    AppInstallCall call;
} PxiReclaimWork;

static int pxi_reclaim_root_locked(AppSettingsStore *store,const WCHAR *root,
        unsigned long *reclaimed_bytes)
{
    PxiReclaimWork *w;
    PxiLedgerItem *items;
    WIN32_FIND_DATAW observed;
    HANDLE resource_lease;
    unsigned int count,i;
    unsigned long bytes;
    int root_missing,resuming,sidecar_missing,directory,rc;
    if(!store || !root) return PRUNTIME_ARGUMENT;
    if(reclaimed_bytes) *reclaimed_bytes=0;
    w=(PxiReclaimWork *)calloc(1,sizeof(*w));
    if(!w) return PRUNTIME_MEMORY;
    items=NULL; count=0; bytes=0; resource_lease=NULL;
    root_missing=0; resuming=0;
    rc=pxi_validate_apps_child(root,w->canonical_root,&root_missing);
    if(rc!=PRUNTIME_OK) goto done;
    if(!WideCharToMultiByte(CP_UTF8,0,w->canonical_root,-1,w->utf8_root,sizeof(w->utf8_root),NULL,NULL)) {
        rc=PRUNTIME_LIMIT; goto done;
    }
    wcscpy(w->sidecar_path,w->canonical_root); wcscat(w->sidecar_path,L".ownership");
    wcscpy(w->reclaim_path,w->canonical_root); wcscat(w->reclaim_path,L".reclaim");
    /* Exclusive resource-root lease compatible with PrepareRevision/OpenInstalled. */
    rc=AppRuntimePlatform_AcquireResourceLease(w->canonical_root,&resource_lease);
    if(rc!=PRUNTIME_OK) goto done;
    /* Serialized check on positron.db single owner covering all active/pending roots. */
    memset(&w->call,0,sizeof(w->call));
    w->call.operation=APP_INSTALL_CHECK_REFERENCES;
    w->call.resource_root=w->utf8_root;
    rc=AppSettingsStore_InstallCall(store,&w->call);
    if(rc!=PRUNTIME_OK) goto done;
    rc=pxi_read_reclaim_marker(w->reclaim_path,w->utf8_root,&resuming);
    if(rc!=PRUNTIME_OK) goto done;
    if(root_missing && !resuming) { rc=PRUNTIME_NOT_FOUND; goto done; }
    if(root_missing && resuming) {
        sidecar_missing=0;
        if(pxi_observe_status(w->sidecar_path,&observed,&sidecar_missing)!=PRUNTIME_OK &&
                !sidecar_missing) { rc=PRUNTIME_IO; goto done; }
        if(!sidecar_missing) {
            rc=pxi_load_ledger(w->canonical_root,1,&items,&count,&bytes);
            if(rc!=PRUNTIME_OK) goto done;
        }
    } else {
        rc=pxi_load_ledger(w->canonical_root,resuming,&items,&count,&bytes);
        if(rc!=PRUNTIME_OK) goto done;
        if(!resuming) {
            rc=pxi_write_reclaim_marker(w->reclaim_path,w->utf8_root);
            if(rc!=PRUNTIME_OK) goto done;
        }
        /* Strict reverse creation order; re-verify path and identity before each delete. */
        for(i=count;i>0;--i) {
            PxiLedgerItem *item;
            item=&items[i-1];
            if(!item->present) continue;
            directory=(item->attributes & FILE_ATTRIBUTE_DIRECTORY)!=0;
            if(AppRuntimePlatform_CanonicalPath(item->path,w->verify_path,directory)!=PRUNTIME_OK ||
                    wcscmp(w->verify_path,item->path) ||
                    pxi_observe(item->path,&observed)!=PRUNTIME_OK ||
                    observed.dwOID!=item->oid ||
                    observed.dwFileAttributes!=item->attributes ||
                    observed.nFileSizeHigh!=item->size_high ||
                    observed.nFileSizeLow!=item->size_low ||
                    (!directory &&
                     (observed.ftLastWriteTime.dwHighDateTime!=item->mtime_high ||
                      observed.ftLastWriteTime.dwLowDateTime!=item->mtime_low))) {
                rc=PRUNTIME_IO;
                goto done;
            }
            if(directory ? !RemoveDirectoryW(item->path) : !DeleteFileW(item->path)) {
                rc=PRUNTIME_IO;
                goto done;
            }
        }
    }
    rc=pxi_delete_sidecar(w->sidecar_path);
    if(rc==PRUNTIME_OK) rc=pxi_delete_sidecar(w->reclaim_path);
    if(rc==PRUNTIME_OK && reclaimed_bytes) *reclaimed_bytes=bytes;
done:
    if(resource_lease) CloseHandle(resource_lease);
    free(items);
    free(w);
    return rc;
}

static int pxi_acquire_reclaim_lease(HANDLE *out_lease)
{
    HANDLE mutex;
    DWORD error;
    *out_lease=NULL;
    SetLastError(0);
    mutex=CreateMutexW(NULL,FALSE,L"Positron.Runtime.Reclaim");
    error=GetLastError();
    if(!mutex) return PRUNTIME_IO;
    if(error==ERROR_ALREADY_EXISTS) { CloseHandle(mutex); return PRUNTIME_BUSY; }
    *out_lease=mutex;
    return PRUNTIME_OK;
}

int AppPxi_ReclaimRoot(AppSettingsStore *store,const WCHAR *root,unsigned long *reclaimed_bytes)
{
    HANDLE reclaim_lease;
    int rc;
    if(!store || !root) return PRUNTIME_ARGUMENT;
    rc=pxi_acquire_reclaim_lease(&reclaim_lease);
    if(rc!=PRUNTIME_OK) return rc;
    rc=pxi_reclaim_root_locked(store,root,reclaimed_bytes);
    CloseHandle(reclaim_lease);
    return rc;
}

static void pxi_add_candidate(WCHAR (*candidates)[MAX_PATH],unsigned int *count,
        const WCHAR *apps,const WCHAR *name)
{
    WCHAR full[MAX_PATH];
    unsigned int i;
    if(*count>=PXI_RECLAIM_MAX_CANDIDATES) return;
    if(wcslen(apps)+wcslen(name)+17>=MAX_PATH) return;
    wcscpy(full,apps); wcscat(full,L"\\"); wcscat(full,name);
    for(i=0;i<*count;++i) if(!_wcsicmp(candidates[i],full)) return;
    wcscpy(candidates[(*count)++],full);
}

int AppPxi_ReclaimUnused(AppSettingsStore *store,int (*cancelled)(void *),void *user,
        AppPxiReclaimSummary *summary)
{
    WCHAR apps[MAX_PATH],pattern[MAX_PATH],stem[MAX_PATH];
    WCHAR (*candidates)[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE reclaim_lease,find;
    unsigned int count,i;
    unsigned long bytes;
    size_t name_len;
    DWORD error;
    int missing,rc,item_rc;
    if(!store || !summary) return PRUNTIME_ARGUMENT;
    memset(summary,0,sizeof(*summary));
    rc=pxi_acquire_reclaim_lease(&reclaim_lease);
    if(rc!=PRUNTIME_OK) return rc;
    candidates=NULL; count=0; missing=0;
    rc=pxi_apps_base(apps,&missing);
    if(rc!=PRUNTIME_OK || missing) goto done;
    if(wcslen(apps)+3>=MAX_PATH) { rc=PRUNTIME_LIMIT; goto done; }
    candidates=(WCHAR (*)[MAX_PATH])calloc(PXI_RECLAIM_MAX_CANDIDATES,sizeof(*candidates));
    if(!candidates) { rc=PRUNTIME_MEMORY; goto done; }
    wcscpy(pattern,apps); wcscat(pattern,L"\\*");
    find=FindFirstFileW(pattern,&data);
    if(find==INVALID_HANDLE_VALUE) {
        error=GetLastError();
        if(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_NO_MORE_FILES) rc=PRUNTIME_IO;
        goto done;
    }
    do {
        if(!wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L"..")) continue;
        if(wcsncmp(data.cFileName,L"pxi-",4)) continue;
        if(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if(!wcschr(data.cFileName,L'.'))
                pxi_add_candidate(candidates,&count,apps,data.cFileName);
        } else {
            name_len=wcslen(data.cFileName);
            if(name_len>12 && !wcscmp(data.cFileName+name_len-8,L".reclaim")) {
                wcsncpy(stem,data.cFileName,name_len-8);
                stem[name_len-8]=0;
                if(!wcschr(stem,L'.'))
                    pxi_add_candidate(candidates,&count,apps,stem);
            }
        }
    } while(FindNextFileW(find,&data));
    error=GetLastError();
    FindClose(find);
    if(error!=ERROR_NO_MORE_FILES) { rc=PRUNTIME_IO; goto done; }
    for(i=0;i<count;++i) {
        if(cancelled && cancelled(user)) { rc=PRUNTIME_CANCELLED; break; }
        ++summary->scanned;
        bytes=0;
        item_rc=pxi_reclaim_root_locked(store,candidates[i],&bytes);
        if(item_rc==PRUNTIME_OK) {
            ++summary->reclaimed;
            summary->reclaimed_bytes+=bytes;
        } else if(item_rc==PRUNTIME_BUSY) {
            ++summary->referenced;
        } else {
            ++summary->skipped;
            if(!summary->first_error) summary->first_error=item_rc;
        }
    }
done:
    free(candidates);
    CloseHandle(reclaim_lease);
    return rc;
}

int AppPxi_Close(AppPxi **slot)
{
    AppPxi *p;
    PxiObject *item;
    WIN32_FIND_DATAW data;
    WCHAR canonical[MAX_PATH];
    int rc,directory;
    if(!slot || !*slot) return PRUNTIME_OK;
    p=*slot; rc=PRUNTIME_OK;
    while(p->count) {
        item=&p->objects[--p->count];
        if(item->pin) CloseHandle(item->pin);
        if(p->retained) continue;
        directory=(item->attributes & FILE_ATTRIBUTE_DIRECTORY)!=0;
        if(!item->oid || pxi_observe(item->path,&data)!=PRUNTIME_OK || data.dwOID!=item->oid ||
            AppRuntimePlatform_CanonicalPath(item->path,canonical,directory)!=PRUNTIME_OK || wcscmp(canonical,item->path) ||
            (directory ? !RemoveDirectoryW(item->path) : !DeleteFileW(item->path))) rc=PRUNTIME_IO;
    }
    if(p->anchor) CloseHandle(p->anchor);
#ifdef _DEBUG
    if(rc!=PRUNTIME_OK) {
        char line[1152];
        _snprintf(line,sizeof(line)-1,"pxi-output cleanup=failed preserved=unproven root=%s\r\n",p->directory);
        line[sizeof(line)-1]=0; AppDebug_Log(line);
    }
#endif
    free(p); *slot=NULL; return rc;
}
