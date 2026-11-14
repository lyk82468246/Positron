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
typedef struct PxiObject {
    WCHAR path[MAX_PATH];
    DWORD oid,attributes;
    HANDLE pin;
} PxiObject;
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

static int pxi_ledger_entry(const WCHAR *root,char *line,int first,unsigned long *total,PxiObject *item)
{
    WCHAR path[MAX_PATH],canonical[MAX_PATH];
    WIN32_FIND_DATAW observed;
    DWORD fields[6];
    unsigned int i;
    size_t length;
    length=wcslen(root);
    for(i=0;i<6;++i) if(!pxi_ledger_number(&line,&fields[i])) return PRUNTIME_INVALID;
    if(first ? *line!=0 : (*line!='\\' || !line[1])) return PRUNTIME_INVALID;
    if(strpbrk(line,"\t\r") || fields[0]==0 || fields[2]) return PRUNTIME_INVALID;
    wcscpy(path,root);
    if(!MultiByteToWideChar(CP_UTF8,0,line,-1,path+length,MAX_PATH-length))
        return PRUNTIME_INVALID;
    if(AppRuntimePlatform_CanonicalPath(path,canonical,(fields[1]&FILE_ATTRIBUTE_DIRECTORY)!=0)!=PRUNTIME_OK ||
            wcscmp(path,canonical) || pxi_observe(path,&observed)!=PRUNTIME_OK ||
            observed.dwOID!=fields[0] || observed.dwFileAttributes!=fields[1] ||
            observed.nFileSizeHigh!=fields[2] || observed.nFileSizeLow!=fields[3] ||
            observed.ftLastWriteTime.dwHighDateTime!=fields[4] ||
            observed.ftLastWriteTime.dwLowDateTime!=fields[5]) return PRUNTIME_IO;
    if(!(fields[1]&FILE_ATTRIBUTE_DIRECTORY)) {
        if(fields[3]>PXI_BYTES-*total) return PRUNTIME_LIMIT;
        *total+=fields[3];
    }
    wcscpy(item->path,path);
    item->attributes=fields[1];
    return PRUNTIME_OK;
}

static int pxi_ledger_tree(PxiObject *items,unsigned int count)
{
    WIN32_FIND_DATAW data;
    WCHAR path[MAX_PATH];
    HANDLE find;
    unsigned int i,j,seen;
    size_t length;
    DWORD error;
    seen=1;
    for(i=0;i<count;++i) {
        if(!(items[i].attributes&FILE_ATTRIBUTE_DIRECTORY)) continue;
        length=wcslen(items[i].path);
        if(length+3>MAX_PATH) return PRUNTIME_LIMIT;
        wcscpy(path,items[i].path); wcscat(path,L"\\*");
        find=FindFirstFileW(path,&data);
        if(find==INVALID_HANDLE_VALUE) {
            if(GetLastError()==ERROR_FILE_NOT_FOUND) continue;
            return PRUNTIME_IO;
        }
        do {
            if(!wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L"..")) continue;
            if(length+wcslen(data.cFileName)+2>MAX_PATH) {
                FindClose(find); return PRUNTIME_LIMIT;
            }
            wcscpy(path+length+1,data.cFileName);
            for(j=i+1;j<count;++j) if(!wcscmp(path,items[j].path)) break;
            if(j==count || ++seen>count) {
                FindClose(find); return PRUNTIME_INVALID;
            }
        } while(FindNextFileW(find,&data));
        error=GetLastError();
        FindClose(find);
        if(error!=ERROR_NO_MORE_FILES) return PRUNTIME_IO;
    }
    return seen==count ? PRUNTIME_OK : PRUNTIME_INVALID;
}

int AppPxi_CheckOwnership(const WCHAR *root,unsigned long *bytes)
{
    static const char header[]="PXI-OWNERSHIP 1 ";
    WCHAR sidecar[MAX_PATH],canonical[MAX_PATH];
    HANDLE file;
    char *buffer,*cursor,*end;
    PxiObject *items;
    DWORD size,read,count;
    unsigned int i;
    unsigned long total;
    int rc;
    if(!root || !bytes || wcslen(root)+11>=MAX_PATH) return PRUNTIME_ARGUMENT;
    if(AppRuntimePlatform_CanonicalPath(root,canonical,1)!=PRUNTIME_OK || wcscmp(root,canonical))
        return PRUNTIME_INVALID;
    wcscpy(sidecar,root); wcscat(sidecar,L".ownership");
    if(AppRuntimePlatform_CanonicalPath(sidecar,canonical,0)!=PRUNTIME_OK || wcscmp(sidecar,canonical))
        return PRUNTIME_INVALID;
    file=CreateFileW(sidecar,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(file==INVALID_HANDLE_VALUE) return PRUNTIME_IO;
    size=GetFileSize(file,NULL);
    if(size<sizeof(header) || size>PXI_OBJECTS*(MAX_PATH*3+128U)) {
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
    items=(PxiObject *)calloc(count,sizeof(*items));
    if(!items) { rc=PRUNTIME_MEMORY; goto done; }
    total=0;
    for(i=0;i<count;++i) {
        end=strchr(cursor,'\n');
        if(!end) { rc=PRUNTIME_INVALID; goto done; }
        *end=0;
        rc=pxi_ledger_entry(root,cursor,i==0,&total,&items[i]);
        if(rc!=PRUNTIME_OK) goto done;
        cursor=end+1;
    }
    rc=*cursor ? PRUNTIME_INVALID : pxi_ledger_tree(items,count);
    if(rc==PRUNTIME_OK) *bytes=total;
done:
    free(items);
    free(buffer);
    CloseHandle(file);
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
