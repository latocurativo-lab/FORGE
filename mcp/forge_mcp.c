#define WIN32_LEAN_AND_MEAN
#include "forge_mcp.h"
#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#ifndef _WIN32
#error "FORGE MCP stdio transport currently requires Windows"
#endif

static int abs_path(const wchar_t *s) { return s && ((wcslen(s)>2 && s[1]==L':' && (s[2]==L'\\'||s[2]==L'/')) || (s[0]==L'\\'&&s[1]==L'\\')); }
static int same_path(const wchar_t *a,const wchar_t *b) {
    wchar_t aa[32768],bb[32768]; DWORD na=GetFullPathNameW(a,32768,aa,NULL), nb=GetFullPathNameW(b,32768,bb,NULL);
    return na && nb && na<32768 && nb<32768 && _wcsicmp(aa,bb)==0;
}
static wchar_t *quote_arg(const wchar_t *s) {
    size_t n=wcslen(s), cap=n*2+3, j=0; wchar_t *o=calloc(cap,sizeof(wchar_t)); if(!o)return NULL;
    o[j++]=L'"'; size_t slashes=0;
    for(size_t i=0;i<n;i++) { if(s[i]==L'\\'){slashes++;continue;} if(s[i]==L'"'){for(size_t k=0;k<slashes;k++)o[j++]=L'\\';slashes=0;o[j++]=L'\\';o[j++]=L'"';}else{for(size_t k=0;k<slashes;k++)o[j++]=L'\\';slashes=0;o[j++]=s[i];} }
    for(size_t k=0;k<slashes;k++)o[j++]=L'\\'; o[j++]=L'"'; return o;
}
static int write_all(HANDLE h,const void *p,DWORD n) { const char *s=p; while(n){DWORD w=0;if(!WriteFile(h,s,n,&w,NULL)||!w)return 0;s+=w;n-=w;}return 1; }
static int read_exact(fmcp_client *c,void *buf,DWORD n,uint32_t deadline) {
    char *p=buf; while(n){if(InterlockedCompareExchange(&c->cancelled,0,0))return -2;
        DWORD avail=0;if(!PeekNamedPipe(c->output,NULL,0,NULL,&avail,NULL))return 0;
        if(!avail){if(WaitForSingleObject(c->process,0)==WAIT_OBJECT_0)return 0;if((LONG)(GetTickCount()-deadline)>=0)return -1;Sleep(5);continue;}
        DWORD want=avail<n?avail:n, got=0;if(!ReadFile(c->output,p,want,&got,NULL)||!got)return 0;p+=got;n-=got;
    }return 1;
}
static fmcp_status recv_json(fmcp_client*c,fj_value **out) {
    char header[128];size_t k=0;uint32_t deadline=GetTickCount()+c->timeout_ms;char ch;
    while(k<sizeof(header)-1){int r=read_exact(c,&ch,1,deadline);if(r==-2)return FMCP_CANCELLED;if(r==-1)return FMCP_TIMEOUT;if(!r)return FMCP_IO_ERROR;header[k++]=ch;if(k>=4&&!memcmp(header+k-4,"\r\n\r\n",4))break;}
    if(k==sizeof(header)-1)return FMCP_PROTOCOL_ERROR;header[k]=0;
    if(strncmp(header,"Content-Length: ",16))return FMCP_PROTOCOL_ERROR;
    for(char*p=header+16;*p&&*p!='\r';p++)if(*p<'0'||*p>'9')return FMCP_PROTOCOL_ERROR;
    char *end=NULL;unsigned long len=strtoul(header+16,&end,10);
    if(end==header+16||len==0||len>c->max_frame_bytes||strcmp(end,"\r\n\r\n"))return len>c->max_frame_bytes?FMCP_TOO_LARGE:FMCP_PROTOCOL_ERROR;
    char *body=malloc(len+1);if(!body)return FMCP_NO_MEMORY;int rr=read_exact(c,body,(DWORD)len,deadline);if(rr<=0){free(body);return rr==-1?FMCP_TIMEOUT:rr==-2?FMCP_CANCELLED:FMCP_IO_ERROR;}body[len]=0;
    /* Reject embedded NUL, invalid UTF-8 and non-whitespace trailing data. */
    if(memchr(body,0,len)){free(body);return FMCP_PROTOCOL_ERROR;}
    fj_value *v=fjson_parse(body);free(body);if(!v||v->type!=FJ_OBJECT){fjson_free(v);return FMCP_PROTOCOL_ERROR;}*out=v;return FMCP_OK;
}
static fmcp_status send_json(fmcp_client*c,const fj_value*v) {
    fbuf b;fb_init(&b);if(fjson_write(v,&b)){fb_free(&b);return FMCP_NO_MEMORY;}if(b.len>c->max_frame_bytes){fb_free(&b);return FMCP_TOO_LARGE;}
    char h[64];int n=snprintf(h,sizeof(h),"Content-Length: %zu\r\n\r\n",b.len);AcquireSRWLockExclusive(&c->write_lock);int ok=n>0&&write_all(c->input,h,(DWORD)n)&&write_all(c->input,b.data,(DWORD)b.len);ReleaseSRWLockExclusive(&c->write_lock);fb_free(&b);return ok?FMCP_OK:FMCP_IO_ERROR;
}
static fj_value *request(const char *method,const fj_value *params,uint32_t id) {
    fj_value *v=fjson_object_new();if(!v)return NULL;fjson_object_set(v,"jsonrpc",fjson_string_new("2.0"));fjson_object_set(v,"id",fjson_number_new(id));fjson_object_set(v,"method",fjson_string_new(method));
    if(params){fbuf b;fb_init(&b);if(fjson_write(params,&b)){fb_free(&b);fjson_free(v);return NULL;}fj_value *copy=fjson_parse(b.data);fb_free(&b);if(!copy){fjson_free(v);return NULL;}fjson_object_set(v,"params",copy);}return v;
}
static fmcp_status rpc(fmcp_client*c,const char*method,const fj_value*params,fj_value**result) {
    uint32_t id=++c->next_id;fj_value *q=request(method,params,id);if(!q)return FMCP_NO_MEMORY;fmcp_status s=send_json(c,q);fjson_free(q);if(s)return s;InterlockedExchange(&c->active_id,(LONG)id);
    for(;;){fj_value *r=NULL;s=recv_json(c,&r);if(s){InterlockedExchange(&c->active_id,0);return s;}const fj_value *version=fjson_get(r,"jsonrpc"),*rid=fjson_get(r,"id"),*err=fjson_get(r,"error"),*res=fjson_get(r,"result");
        if(!rid){/* notifications from server are ignored as untrusted data */fjson_free(r);continue;}
        if(!version||version->type!=FJ_STRING||strcmp(fjson_str(version),"2.0")||rid->type!=FJ_NUMBER||fjson_num(rid)!=(double)id||((err!=NULL)==(res!=NULL))){InterlockedExchange(&c->active_id,0);fjson_free(r);return FMCP_PROTOCOL_ERROR;}
        if(err){InterlockedExchange(&c->active_id,0);fjson_free(r);return FMCP_SERVER_ERROR;}InterlockedExchange(&c->active_id,0);*result=(fj_value*)res;/* unlink before freeing the response envelope */fj_value **link=&r->head;while(*link&&*link!=(fj_value*)res)link=&(*link)->next;if(*link)*link=((fj_value*)res)->next;((fj_value*)res)->key=NULL;((fj_value*)res)->next=NULL;fjson_free(r);return FMCP_OK;}
}
static fmcp_status notify(fmcp_client*c,const char*method) { fj_value*v=fjson_object_new();if(!v)return FMCP_NO_MEMORY;fjson_object_set(v,"jsonrpc",fjson_string_new("2.0"));fjson_object_set(v,"method",fjson_string_new(method));fmcp_status s=send_json(c,v);fjson_free(v);return s; }
void fmcp_stop(fmcp_client*c){if(!c)return;if(c->input&&c->input!=INVALID_HANDLE_VALUE)CloseHandle(c->input);if(c->output&&c->output!=INVALID_HANDLE_VALUE)CloseHandle(c->output);if(c->job){TerminateJobObject(c->job,1);CloseHandle(c->job);}if(c->process)CloseHandle(c->process);memset(c,0,sizeof(*c));}
fmcp_status fmcp_start(fmcp_client*c,const fmcp_config*cfg) {
    if(!c||!cfg||!cfg->executable||!abs_path(cfg->executable)||!cfg->allowlist||!cfg->allowlist_count||cfg->argument_count>128||(cfg->argument_count&&!cfg->arguments)||cfg->max_frame_bytes==0||cfg->max_frame_bytes>FMCP_MAX_FRAME||!cfg->server_id||!cfg->server_id[0]||strlen(cfg->server_id)>=sizeof(c->server_id))return FMCP_INVALID;
    if(!cfg->caller_approved)return FMCP_NOT_APPROVED;
    for(const char*p=cfg->server_id;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'))return FMCP_INVALID;
    int allowed=0;for(size_t i=0;i<cfg->allowlist_count;i++)if(abs_path(cfg->allowlist[i])&&same_path(cfg->executable,cfg->allowlist[i]))allowed=1;if(!allowed)return FMCP_NOT_APPROVED;
    memset(c,0,sizeof(*c));InitializeSRWLock(&c->write_lock);strcpy_s(c->server_id,sizeof(c->server_id),cfg->server_id);c->input=c->output=INVALID_HANDLE_VALUE;c->timeout_ms=cfg->timeout_ms?cfg->timeout_ms:10000;c->max_frame_bytes=cfg->max_frame_bytes;
    SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};HANDLE child_in=0,child_out=0; if(!CreatePipe(&child_in,&c->input,&sa,0)||!CreatePipe(&c->output,&child_out,&sa,0)){fmcp_stop(c);return FMCP_LAUNCH_ERROR;}SetHandleInformation(c->input,HANDLE_FLAG_INHERIT,0);SetHandleInformation(c->output,HANDLE_FLAG_INHERIT,0);
    size_t cap=32768;wchar_t *cmd=calloc(cap,sizeof(wchar_t));if(!cmd){CloseHandle(child_in);CloseHandle(child_out);fmcp_stop(c);return FMCP_NO_MEMORY;}wchar_t *q=quote_arg(cfg->executable);if(!q){free(cmd);CloseHandle(child_in);CloseHandle(child_out);fmcp_stop(c);return FMCP_NO_MEMORY;}wcscat_s(cmd,cap,q);free(q);
    for(size_t i=0;i<cfg->argument_count;i++){if(!cfg->arguments[i])continue;q=quote_arg(cfg->arguments[i]);if(!q||wcslen(cmd)+wcslen(q)+2>=cap){free(q);free(cmd);CloseHandle(child_in);CloseHandle(child_out);fmcp_stop(c);return FMCP_INVALID;}wcscat_s(cmd,cap,L" ");wcscat_s(cmd,cap,q);free(q);}
    STARTUPINFOW si={sizeof(si)};si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=child_in;si.hStdOutput=child_out;si.hStdError=GetStdHandle(STD_ERROR_HANDLE);PROCESS_INFORMATION pi={0};
     BOOL made=CreateProcessW(cfg->executable,cmd,NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi);free(cmd);CloseHandle(child_in);CloseHandle(child_out);if(!made){fmcp_stop(c);return FMCP_LAUNCH_ERROR;}c->process=pi.hProcess;CloseHandle(pi.hThread);c->job=CreateJobObjectW(NULL,NULL);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;if(!c->job||!SetInformationJobObject(c->job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))||!AssignProcessToJobObject(c->job,c->process)){fmcp_stop(c);return FMCP_LAUNCH_ERROR;}
    fj_value*p=fjson_object_new(),*capabilities=fjson_object_new();fjson_object_set(p,"protocolVersion",fjson_string_new("2025-03-26"));fjson_object_set(p,"capabilities",capabilities);fj_value*info=fjson_object_new();fjson_object_set(info,"name",fjson_string_new(cfg->client_name?cfg->client_name:"FORGE"));fjson_object_set(info,"version",fjson_string_new(cfg->client_version?cfg->client_version:"0.1"));fjson_object_set(p,"clientInfo",info);
    fj_value*r=NULL;fmcp_status s=rpc(c,"initialize",p,&r);fjson_free(p);if(s){fmcp_stop(c);return s;}const fj_value*ver=fjson_get(r,"protocolVersion");if(!ver||ver->type!=FJ_STRING||strcmp(fjson_str(ver),"2025-03-26")){fjson_free(r);fmcp_stop(c);return FMCP_PROTOCOL_ERROR;}fjson_free(r);s=notify(c,"notifications/initialized");if(s){fmcp_stop(c);return s;}c->initialized=1;return FMCP_OK;
}
fmcp_status fmcp_list_tools(fmcp_client*c,fj_value**result){if(!c||!c->initialized||!result)return FMCP_INVALID;*result=NULL;fj_value*p=fjson_object_new();fmcp_status s=rpc(c,"tools/list",p,result);fjson_free(p);if(s)return s;const fj_value*t=fjson_get(*result,"tools");if(!t||t->type!=FJ_ARRAY||fjson_count(t)>FMCP_MAX_TOOLS){fjson_free(*result);*result=NULL;return FMCP_PROTOCOL_ERROR;}for(fj_value*i=t->head;i;i=i->next){fj_value*n=(fj_value*)fjson_get(i,"name");if(i->type!=FJ_OBJECT||!n||n->type!=FJ_STRING||!fjson_str(n)[0]){fjson_free(*result);*result=NULL;return FMCP_PROTOCOL_ERROR;}for(const char*q=n->str;*q;q++)if(!((*q>='a'&&*q<='z')||(*q>='A'&&*q<='Z')||(*q>='0'&&*q<='9')||*q=='_'||*q=='-'||*q=='.')){fjson_free(*result);*result=NULL;return FMCP_PROTOCOL_ERROR;}for(fj_value*j=t->head;j!=i;j=j->next){const fj_value*prior=fjson_get(j,"name");if(prior&&prior->type==FJ_STRING&&!strcmp(prior->str,n->str)){fjson_free(*result);*result=NULL;return FMCP_PROTOCOL_ERROR;}}char full[256];if(snprintf(full,sizeof(full),"mcp.%s.%s",c->server_id,n->str)>=(int)sizeof(full)){fjson_free(*result);*result=NULL;return FMCP_PROTOCOL_ERROR;}char*copy=fstrdup(full);if(!copy){fjson_free(*result);*result=NULL;return FMCP_NO_MEMORY;}free(n->str);n->str=copy;}return FMCP_OK;}
fmcp_status fmcp_call_tool(fmcp_client*c,const char*name,const fj_value*args,fj_value**result){if(!c||!c->initialized||!name||!args||args->type!=FJ_OBJECT||!result)return FMCP_INVALID;char prefix[160];snprintf(prefix,sizeof(prefix),"mcp.%s.",c->server_id);size_t plen=strlen(prefix);if(strncmp(name,prefix,plen)||!name[plen])return FMCP_INVALID;*result=NULL;fj_value*p=fjson_object_new();fjson_object_set(p,"name",fjson_string_new(name+plen));fbuf b;fb_init(&b);fjson_write(args,&b);fj_value*a=fjson_parse(b.data);fb_free(&b);if(!a){fjson_free(p);return FMCP_NO_MEMORY;}fjson_object_set(p,"arguments",a);fmcp_status s=rpc(c,"tools/call",p,result);fjson_free(p);return s;}
fmcp_status fmcp_cancel(fmcp_client*c){if(!c)return FMCP_INVALID;LONG id=InterlockedCompareExchange(&c->active_id,0,0);InterlockedExchange(&c->cancelled,1);if(id&&c->input&&c->input!=INVALID_HANDLE_VALUE){fj_value*p=fjson_object_new(),*v=fjson_object_new();if(!p||!v){fjson_free(p);fjson_free(v);return FMCP_NO_MEMORY;}fjson_object_set(p,"requestId",fjson_number_new((double)id));fjson_object_set(v,"jsonrpc",fjson_string_new("2.0"));fjson_object_set(v,"method",fjson_string_new("notifications/cancelled"));fjson_object_set(v,"params",p);fmcp_status s=send_json(c,v);fjson_free(v);return s;}return FMCP_OK;}
