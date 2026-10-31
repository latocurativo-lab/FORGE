#define _CRT_SECURE_NO_WARNINGS
#include "forge_mcp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct call_arg { fmcp_client *client; fmcp_status status; fj_value *result; } call_arg;
static DWORD WINAPI blocked_call(void *p) { call_arg *a=p; fj_value *args=fjson_object_new();a->status=fmcp_call_tool(a->client,"mcp.fake.echo",args,&a->result);fjson_free(args);return 0; }

int main(int argc,char **argv8) {
    if(argc<4||argc>8)return 90;
    wchar_t *storage[8]={0}, **argv=storage;
    for(int i=0;i<argc&&i<8;i++){int n=MultiByteToWideChar(CP_UTF8,0,argv8[i],-1,NULL,0);if(!n)return 90;argv[i]=calloc((size_t)n,sizeof(wchar_t));if(!argv[i]||!MultiByteToWideChar(CP_UTF8,0,argv8[i],-1,argv[i],n))return 90;}
    fmcp_client c; const wchar_t *allow[]={argv[1]};
    const wchar_t *args[]={L"-u",argv[2],argv[3]};
    fmcp_config cfg={argv[1],args,3,allow,1,1,"test","1","fake",1000,65536};
    if(wcscmp(argv[3],L"noapproval")==0){cfg.caller_approved=0;return fmcp_start(&c,&cfg)==FMCP_NOT_APPROVED?0:91;}
    fmcp_status s=fmcp_start(&c,&cfg);if(s)return (int)s+10;
    fj_value *r=NULL;
    if(wcscmp(argv[3],L"handshake")==0){fmcp_stop(&c);return 0;}
    s=fmcp_list_tools(&c,&r);if(s){fmcp_stop(&c);return (int)s+30;}
    if(!fjson_get(r,"tools")){fjson_free(r);fmcp_stop(&c);return 31;}fjson_free(r);r=NULL;
    if(wcscmp(argv[3],L"cancel")==0){call_arg a={&c,FMCP_OK,NULL};HANDLE th=CreateThread(NULL,0,blocked_call,&a,0,NULL);if(!th){fmcp_stop(&c);return 32;}Sleep(60);fmcp_cancel(&c);WaitForSingleObject(th,2000);CloseHandle(th);if(a.result)fjson_free(a.result);fmcp_stop(&c);return a.status==FMCP_CANCELLED?0:33;}
    fj_value *a=fjson_object_new();fjson_object_set(a,"text",fjson_string_new("hello"));s=fmcp_call_tool(&c,"mcp.fake.echo",a,&r);fjson_free(a);
    if(wcscmp(argv[3],L"timeout")==0){fmcp_stop(&c);return s==FMCP_TIMEOUT?0:32;}
    if(s){fmcp_stop(&c);return (int)s+40;}if(!fjson_get(r,"content")){fjson_free(r);fmcp_stop(&c);return 41;}
    fjson_free(r);fmcp_stop(&c);return 0;
}
