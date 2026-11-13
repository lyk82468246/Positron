#include <stdlib.h>
#include <string.h>
#include "app_install_store.h"

typedef struct InstallDb {
    int (*prepare)(PDbHandle,const char *,PDbStmtHandle *);
    int (*text)(PDbStmtHandle,int,const char *,int);
    int (*integer)(PDbStmtHandle,int,__int64);
    int (*step)(PDbStmtHandle);
    int (*finalize)(PDbStmtHandle);
    int (*type)(PDbStmtHandle,int);
    __int64 (*number)(PDbStmtHandle,int);
    const char *(*string)(PDbStmtHandle,int);
    int (*bytes)(PDbStmtHandle,int);
    int (*begin)(PDbHandle);
    int (*commit)(PDbHandle);
    int (*rollback)(PDbHandle);
    int (*error)(PDbHandle,PDbErrorInfo *);
    int (*state)(PDbHandle,PDbConnectionState *);
    int (*clear)(PDbHandle);
} InstallDb;

static const char install_schema[] =
    "CREATE TABLE app_installs(installation_id TEXT PRIMARY KEY NOT NULL,"
    "app_id TEXT UNIQUE NOT NULL,state INTEGER NOT NULL,generation INTEGER NOT NULL,"
    "target_schema INTEGER NOT NULL,directory TEXT NOT NULL,revision INTEGER NOT NULL,"
    "app_version INTEGER NOT NULL,api_version INTEGER NOT NULL,digest INTEGER NOT NULL,"
    "content_id TEXT NOT NULL,requested INTEGER NOT NULL,approved INTEGER NOT NULL,"
    "approval_policy INTEGER NOT NULL);";
static const char install_select_id[] =
    "SELECT installation_id,app_id,state,generation,target_schema,directory,revision,"
    "app_version,api_version,digest,content_id,requested,approved,approval_policy "
    "FROM app_installs WHERE installation_id=?1";
static const char install_select_app[] =
    "SELECT installation_id,app_id,state,generation,target_schema,directory,revision,"
    "app_version,api_version,digest,content_id,requested,approved,approval_policy "
    "FROM app_installs WHERE app_id=?1";
static const char install_insert[] =
    "INSERT INTO app_installs VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14)";
static const char install_update[] =
    "UPDATE app_installs SET app_id=?2,state=?3,generation=?4,target_schema=?5,"
    "directory=?6,revision=?7,app_version=?8,api_version=?9,digest=?10,content_id=?11,"
    "requested=?12,approved=?13,approval_policy=?14 WHERE installation_id=?1";

static const char update_schema[] =
    "CREATE TABLE app_update_attempts(installation_id TEXT PRIMARY KEY NOT NULL,"
    "attempt_id TEXT NOT NULL,state INTEGER NOT NULL,generation INTEGER NOT NULL);"
    "CREATE TABLE app_update_revisions(installation_id TEXT NOT NULL,app_id TEXT NOT NULL,"
    "state INTEGER NOT NULL,generation INTEGER NOT NULL,target_schema INTEGER NOT NULL,"
    "directory TEXT NOT NULL,revision INTEGER NOT NULL,app_version INTEGER NOT NULL,"
    "api_version INTEGER NOT NULL,digest INTEGER NOT NULL,content_id TEXT NOT NULL,"
    "requested INTEGER NOT NULL,approved INTEGER NOT NULL,approval_policy INTEGER NOT NULL,"
    "role INTEGER NOT NULL,PRIMARY KEY(installation_id,role));";
#define UPDATE_FIELDS "SELECT installation_id,app_id,state,generation,target_schema,directory,revision," \
    "app_version,api_version,digest,content_id,requested,approved,approval_policy " \
    "FROM app_update_revisions WHERE installation_id=?1 AND role="
static const char update_select_base[] = UPDATE_FIELDS "1";
static const char update_select_candidate[] = UPDATE_FIELDS "2";
#undef UPDATE_FIELDS
#define UPDATE_VALUES "INSERT OR REPLACE INTO app_update_revisions VALUES(" \
    "?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,"
static const char update_write_base[] = UPDATE_VALUES "1)";
static const char update_write_candidate[] = UPDATE_VALUES "2)";
#undef UPDATE_VALUES

const char *AppInstallStore_Schema(void) { return install_schema; }
const char *AppInstallStore_UpdateSchema(void) { return update_schema; }

/* Compare explicit persisted fields, never padding. This is storage CAS
 * equality; permitted transitions/approval/readback classification stay DLL. */
int AppInstallStore_RecordEqual(const PRuntimeRegistrationRecord *a,
        const PRuntimeRegistrationRecord *b)
{
    const PRuntimeApprovalSnapshot *x;
    const PRuntimeApprovalSnapshot *y;
    x = &a->approval; y = &b->approval;
    return a->state == b->state && a->generation == b->generation &&
        a->target_schema == b->target_schema && !strcmp(a->directory,b->directory) &&
        !strcmp(x->id,y->id) && !strcmp(x->installation_id,y->installation_id) &&
        x->revision == y->revision && x->app_version == y->app_version &&
        x->runtime_api_version == y->runtime_api_version &&
        x->digest_algorithm == y->digest_algorithm &&
        !memcmp(x->content_id,y->content_id,32) &&
        x->requested_permissions == y->requested_permissions &&
        x->approved_permissions == y->approved_permissions &&
        x->approval_policy == y->approval_policy;
}

static int install_api(HMODULE module, InstallDb *a)
{
#define GET(field,name,type) a->field = (type)GetProcAddress(module,TEXT(name)); if (!a->field) return 0
    GET(prepare,"PDb_Prepare",int (*)(PDbHandle,const char *,PDbStmtHandle *));
    GET(text,"PDb_BindText",int (*)(PDbStmtHandle,int,const char *,int));
    GET(integer,"PDb_BindInt64",int (*)(PDbStmtHandle,int,__int64));
    GET(step,"PDb_Step",int (*)(PDbStmtHandle));
    GET(finalize,"PDb_Finalize",int (*)(PDbStmtHandle));
    GET(type,"PDb_ColumnType",int (*)(PDbStmtHandle,int));
    GET(number,"PDb_ColumnInt64",__int64 (*)(PDbStmtHandle,int));
    GET(string,"PDb_ColumnText",const char *(*)(PDbStmtHandle,int));
    GET(bytes,"PDb_ColumnBytes",int (*)(PDbStmtHandle,int));
    GET(begin,"PDb_Begin",int (*)(PDbHandle));
    GET(commit,"PDb_Commit",int (*)(PDbHandle));
    GET(rollback,"PDb_Rollback",int (*)(PDbHandle));
    GET(error,"PDb_GetErrorInfo",int (*)(PDbHandle,PDbErrorInfo *));
    GET(state,"PDb_GetConnectionState",int (*)(PDbHandle,PDbConnectionState *));
    GET(clear,"PDb_ClearError",int (*)(PDbHandle));
#undef GET
    return 1;
}
static int install_string(InstallDb *a, PDbStmtHandle s, int column,
        char *out, unsigned int capacity)
{
    const char *text;
    int bytes;
    if (a->type(s,column) != PDB_VALUE_TEXT) return 0;
    bytes = a->bytes(s,column); text = a->string(s,column);
    if (!text || bytes < 1 || (unsigned int)bytes >= capacity || memchr(text,0,bytes)) return 0;
    memcpy(out,text,bytes); out[bytes] = 0; return 1;
}
static int install_number(InstallDb *a, PDbStmtHandle s, int col, unsigned int *out)
{
    __int64 n;
    if (a->type(s,col) != PDB_VALUE_INTEGER) return 0;
    n = a->number(s,col);
    if (n < 0 || n > 2147483647) return 0;
    *out = (unsigned int)n; return 1;
}
int AppInstallStore_List(HMODULE module,PDbHandle db,AppInstalledList *list)
{
    InstallDb a;
    PDbStmtHandle statement;
    char normalized[64];
    int rc,finish,index;
    memset(list,0,sizeof(*list));
    if(!install_api(module,&a)) return PRUNTIME_IO;
    statement=NULL;
    rc=a.prepare(db,"SELECT app_id,app_version,installation_id,revision,generation FROM app_installs WHERE state=?1 ORDER BY app_id LIMIT 17",&statement);
    if(rc==PDB_OK) rc=a.integer(statement,1,PRUNTIME_REG_READY);
    if(rc==PDB_OK) rc=a.step(statement);
    while(rc==PDB_STEP_ROW && list->count<APP_INSTALLED_LIST_MAX) {
        index=list->count;
        if(!install_string(&a,statement,0,list->rows[index].id,sizeof(list->rows[index].id)) ||
                PRuntime_NormalizeId(list->rows[index].id,(unsigned int)strlen(list->rows[index].id),
                    normalized,sizeof(normalized))!=PRUNTIME_OK ||
                strcmp(normalized,list->rows[index].id) ||
                !install_number(&a,statement,1,&list->rows[index].version) ||
                !install_string(&a,statement,2,list->rows[index].installation_id,sizeof(list->rows[index].installation_id)) ||
                !install_number(&a,statement,3,&list->rows[index].revision) ||
                !install_number(&a,statement,4,&list->rows[index].generation)) {
            rc=PDB_ERROR; break;
        }
        ++list->count;
        rc=a.step(statement);
    }
    if(rc==PDB_STEP_ROW) { list->more=1; rc=a.step(statement); }
    finish=statement ? a.finalize(statement) : PDB_OK;
    return rc==PDB_STEP_DONE && finish==PDB_OK ? PRUNTIME_OK : PRUNTIME_IO;
}
static int install_read(InstallDb *a, PDbHandle db, const char *query,
        const char *key, PRuntimeRegistrationRecord *r)
{
    PDbStmtHandle s;
    PRuntimeApprovalSnapshot *p;
    char hex[65];
    const char *digits;
    const char *x;
    const char *y;
    unsigned int requested,approved,i;
    int rc,ok,done;
    s = NULL; p = &r->approval; requested=0; approved=0;
    memset(r,0,sizeof(*r)); r->size=sizeof(*r); r->version=1;
    p->size=sizeof(*p); p->version=1;
    rc=a->prepare(db,query,&s);
    if (rc==PDB_OK) rc=a->text(s,1,key,(int)strlen(key));
    if (rc==PDB_OK) rc=a->step(s);
    if (rc==PDB_STEP_DONE) return a->finalize(s)==PDB_OK ? PRUNTIME_NOT_FOUND : PRUNTIME_IO;
    ok = rc==PDB_STEP_ROW;
    if (ok) ok=install_string(a,s,0,p->installation_id,sizeof(p->installation_id)) &&
        install_string(a,s,1,p->id,sizeof(p->id)) &&
        install_number(a,s,2,&r->state) && install_number(a,s,3,&r->generation) &&
        install_number(a,s,4,&r->target_schema) && install_string(a,s,5,r->directory,sizeof(r->directory)) &&
        install_number(a,s,6,&p->revision) && install_number(a,s,7,&p->app_version) &&
        install_number(a,s,8,&p->runtime_api_version) && install_number(a,s,9,&p->digest_algorithm) &&
        install_string(a,s,10,hex,sizeof(hex)) && install_number(a,s,11,&requested) &&
        install_number(a,s,12,&approved) && install_number(a,s,13,&p->approval_policy);
    digits="0123456789abcdef";
    if (ok && strlen(hex)!=64) ok=0;
    if (ok) for (i=0;i<32;++i) {
        x=strchr(digits,hex[i*2]); y=strchr(digits,hex[i*2+1]);
        if (!x || !y) { ok=0; break; }
        p->content_id[i]=(unsigned char)(((x-digits)<<4)|(y-digits));
    }
    if (ok) { p->requested_permissions=requested; p->approved_permissions=approved; }
    done=ok ? a->step(s) : PDB_ERROR;
    rc=s ? a->finalize(s) : PDB_ERROR;
    return ok && done==PDB_STEP_DONE && rc==PDB_OK ? PRUNTIME_OK : PRUNTIME_IO;
}
static int install_write(InstallDb *a,PDbHandle db,const char *query,const PRuntimeRegistrationRecord *r)
{
    PDbStmtHandle s;
    const PRuntimeApprovalSnapshot *p;
    const char *digits;
    char hex[65];
    unsigned int i;
    int rc,finish;
    p=&r->approval; digits="0123456789abcdef";
    for(i=0;i<32;++i) { hex[i*2]=digits[p->content_id[i]>>4]; hex[i*2+1]=digits[p->content_id[i]&15]; }
    hex[64]=0; s=NULL;
    rc=a->prepare(db,query,&s);
#define BINDTEXT(n,v) if(rc==PDB_OK) rc=a->text(s,n,v,(int)strlen(v))
#define BINDINT(n,v) if(rc==PDB_OK) rc=a->integer(s,n,(__int64)(v))
    BINDTEXT(1,p->installation_id); BINDTEXT(2,p->id); BINDINT(3,r->state);
    BINDINT(4,r->generation); BINDINT(5,r->target_schema); BINDTEXT(6,r->directory);
    BINDINT(7,p->revision); BINDINT(8,p->app_version); BINDINT(9,p->runtime_api_version);
    BINDINT(10,p->digest_algorithm); BINDTEXT(11,hex); BINDINT(12,p->requested_permissions);
    BINDINT(13,p->approved_permissions); BINDINT(14,p->approval_policy);
#undef BINDTEXT
#undef BINDINT
    if(rc==PDB_OK) rc=a->step(s);
    finish=s ? a->finalize(s) : PDB_OK;
    return rc==PDB_STEP_DONE && finish==PDB_OK ? PDB_OK : (rc==PDB_STEP_DONE ? finish : rc);
}

static int update_read(InstallDb *a,PDbHandle db,const char *key,PRuntimeUpdateRegistration *r)
{
    PDbStmtHandle s;
    int rc,finish;
    memset(r,0,sizeof(*r)); r->size=sizeof(*r); r->version=1;
    rc=install_read(a,db,install_select_id,key,&r->current);
    if(rc!=PRUNTIME_OK) return rc;
    s=NULL;
    rc=a->prepare(db,"SELECT attempt_id,state,generation FROM app_update_attempts WHERE installation_id=?1",&s);
    if(rc==PDB_OK) rc=a->text(s,1,key,(int)strlen(key));
    if(rc==PDB_OK) rc=a->step(s);
    if(rc==PDB_STEP_ROW) {
        if(install_string(a,s,0,r->attempt_id,sizeof(r->attempt_id)) &&
                install_number(a,s,1,&r->attempt_state) && r->attempt_state &&
                install_number(a,s,2,&r->attempt_generation)) rc=a->step(s);
        else rc=PDB_ERROR;
    }
    finish=s ? a->finalize(s) : PDB_OK;
    if(rc!=PDB_STEP_DONE || finish!=PDB_OK) return PRUNTIME_IO;
    if(!r->attempt_state) return PRUNTIME_OK;
    if(install_read(a,db,update_select_base,key,&r->base)!=PRUNTIME_OK ||
            install_read(a,db,update_select_candidate,key,&r->candidate)!=PRUNTIME_OK) return PRUNTIME_IO;
    return PRUNTIME_OK;
}

static int update_equal(const PRuntimeUpdateRegistration *a,const PRuntimeUpdateRegistration *b)
{
    return AppInstallStore_RecordEqual(&a->current,&b->current) &&
        a->attempt_state==b->attempt_state && a->attempt_generation==b->attempt_generation &&
        !strcmp(a->attempt_id,b->attempt_id) && (!a->attempt_state ||
        (AppInstallStore_RecordEqual(&a->base,&b->base) &&
         AppInstallStore_RecordEqual(&a->candidate,&b->candidate)));
}

static int update_write(InstallDb *a,PDbHandle db,const PRuntimeUpdateRegistration *r)
{
    PDbStmtHandle s;
    const char *key;
    int rc,finish;
    key=r->current.approval.installation_id;
    rc=install_write(a,db,install_update,&r->current);
    /* Revocation of an installation with no update history preserves absence. */
    if(!r->attempt_state) return rc;
    if(rc==PDB_OK) rc=install_write(a,db,update_write_base,&r->base);
    if(rc==PDB_OK) rc=install_write(a,db,update_write_candidate,&r->candidate);
    if(rc!=PDB_OK) return rc;
    s=NULL;
    rc=a->prepare(db,"INSERT OR REPLACE INTO app_update_attempts VALUES(?1,?2,?3,?4)",&s);
    if(rc==PDB_OK) rc=a->text(s,1,key,(int)strlen(key));
    if(rc==PDB_OK) rc=a->text(s,2,r->attempt_id,(int)strlen(r->attempt_id));
    if(rc==PDB_OK) rc=a->integer(s,3,r->attempt_state);
    if(rc==PDB_OK) rc=a->integer(s,4,r->attempt_generation);
    if(rc==PDB_OK) rc=a->step(s);
    finish=s ? a->finalize(s) : PDB_OK;
    return rc==PDB_STEP_DONE ? finish : rc;
}

void AppInstallStore_Process(HMODULE module,PDbHandle db,AppInstallCall *c)
{
    InstallDb a;
    PDbConnectionState state;
    PRuntimeRegistrationRecord *old;
    int rc,found;
    memset(&a,0,sizeof(a)); c->result=PRUNTIME_IO;
    c->outcome=PRUNTIME_COMMIT_NOT_COMMITTED; c->unsafe=0; c->cleanup_result=PDB_OK;
    memset(&c->error,0,sizeof(c->error)); c->error.size=sizeof(c->error); c->error.version=PDB_ERROR_INFO_VERSION;
    if (!install_api(module,&a)) return;
    state.size=sizeof(state); state.version=PDB_CONNECTION_STATE_VERSION;
    if(a.state(db,&state)!=PDB_OK || state.transaction_active || state.statement_count) { c->unsafe=1; return; }
    if(a.clear(db)!=PDB_OK) return;
    if(c->operation==APP_INSTALL_READ_ID || c->operation==APP_INSTALL_READ_APP) {
        c->result=install_read(&a,db,c->operation==APP_INSTALL_READ_APP ? install_select_app : install_select_id,c->key,&c->record);
        goto settled;
    }
    old=&c->record;
    rc=a.begin(db);
    if(rc!=PDB_OK) goto error;
    if(c->operation==APP_UPDATE_READ || c->operation==APP_UPDATE_CAS) {
        c->result=update_read(&a,db,c->key,c->update_record);
        if(c->operation==APP_UPDATE_READ || c->result!=PRUNTIME_OK) goto rollback;
        if(!update_equal(c->update_record,c->update_expected)) {
            c->result=PRUNTIME_INVALID; goto rollback;
        }
        rc=update_write(&a,db,c->update_proposed);
        if(rc!=PDB_OK) goto error;
        goto commit;
    }
    found=install_read(&a,db,install_select_id,c->proposed.approval.installation_id,old);
    if(c->previous_exists) {
        if(found!=PRUNTIME_OK || !AppInstallStore_RecordEqual(old,&c->previous) ||
                c->previous.generation>=2147483647U || c->proposed.generation!=c->previous.generation+1) {
            c->result=found==PRUNTIME_IO ? PRUNTIME_IO : PRUNTIME_INVALID; goto rollback;
        }
    } else {
        if(found!=PRUNTIME_NOT_FOUND) { c->result=found==PRUNTIME_IO ? PRUNTIME_IO : PRUNTIME_BUSY; goto rollback; }
        found=install_read(&a,db,install_select_app,c->proposed.approval.id,old);
        if(found!=PRUNTIME_NOT_FOUND || c->proposed.generation!=1) {
            c->result=found==PRUNTIME_IO ? PRUNTIME_IO : PRUNTIME_BUSY; goto rollback;
        }
    }
    rc=install_write(&a,db,c->previous_exists ? install_update : install_insert,&c->proposed);
    if(rc!=PDB_OK) goto error;
commit:
    c->outcome=PRUNTIME_COMMIT_UNKNOWN;
    rc=a.commit(db);
    if(rc!=PDB_OK) goto error;
    c->outcome=PRUNTIME_COMMIT_CONFIRMED; c->result=PRUNTIME_OK;
#ifdef _DEBUG
    if(c->fault==1) { c->outcome=PRUNTIME_COMMIT_UNKNOWN; c->result=PRUNTIME_IO; }
#endif
    goto settled;
error:
    (void)a.error(db,&c->error); c->result=PRUNTIME_IO;
rollback:
    if(a.state(db,&state)==PDB_OK && state.transaction_active) c->cleanup_result=a.rollback(db);
    if(c->cleanup_result!=PDB_OK) c->result=PRUNTIME_IO;
settled:
    if(c->result==PRUNTIME_IO && !c->error.result) (void)a.error(db,&c->error);
    if(a.state(db,&state)!=PDB_OK || state.transaction_active || state.statement_count) {
        c->unsafe=1; c->result=PRUNTIME_IO;
    }
}
