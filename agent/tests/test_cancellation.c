#define WIN32_LEAN_AND_MEAN
#include "forge_runtime.h"
#include "forge_tools.h"
#include "coding_agent.h"
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)

typedef struct { SOCKET listener; HANDLE accepted; } fake_server;
static DWORD WINAPI serve_slow(void *arg) {
    fake_server *s=(fake_server*)arg; SOCKET c=accept(s->listener,NULL,NULL);
    if(c!=INVALID_SOCKET){char buf[2048];recv(c,buf,sizeof(buf),0);SetEvent(s->accepted);Sleep(10000);closesocket(c);}return 0;
}
static DWORD WINAPI serve_tool_call(void *arg) {
    fake_server *s=(fake_server*)arg;SOCKET c=accept(s->listener,NULL,NULL);
    if(c!=INVALID_SOCKET){char buf[8192]={0};size_t used=0;int wanted=-1;while(used<sizeof(buf)-1){int got=recv(c,buf+used,(int)(sizeof(buf)-1-used),0);if(got<=0)break;used+=(size_t)got;buf[used]=0;char *end=strstr(buf,"\r\n\r\n");if(end&&wanted<0){char *cl=strstr(buf,"Content-Length:");if(cl)wanted=atoi(cl+15);else wanted=0;wanted+=(int)(end+4-buf);}if(wanted>=0&&used>=(size_t)wanted)break;}const char *body="{\"choices\":[{\"finish_reason\":\"tool_calls\",\"message\":{\"content\":null,\"tool_calls\":[{\"id\":\"call_cancel\",\"type\":\"function\",\"function\":{\"name\":\"write_file\",\"arguments\":\"{\\\"path\\\":\\\"cancelled.txt\\\",\\\"content\\\":\\\"side effect\\\"}\"}}]}}]}";
        char header[256];int hn=snprintf(header,sizeof(header),"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",(unsigned)strlen(body));send(c,header,hn,0);send(c,body,(int)strlen(body),0);closesocket(c);}return 0;
}
typedef struct { volatile int cancel; int rc; fr_config config; fr_reply reply; char error[256]; } generate_job;
static DWORD WINAPI generate_slow(void *arg) {
    generate_job *j=(generate_job*)arg; fr_message m={"user","cancel test",NULL,NULL,0};
    j->rc=fr_generate(&j->config,&m,1,&j->reply,j->error,sizeof(j->error));return 0;
}
static DWORD WINAPI cancel_later(void *arg) { volatile int *flag=(volatile int*)arg;Sleep(100);InterlockedExchange((volatile LONG*)flag,1);return 0; }
static int cancel_during_approval(void *context,const char *rid,const char *cid,const char *tool,const char *args,const char *cap,int mutation) {
    volatile int *flag=(volatile int*)context;(void)rid;(void)cid;(void)tool;(void)args;(void)cap;(void)mutation;
    Sleep(100);InterlockedExchange((volatile LONG*)flag,1);return 1;
}

int main(void) {
    WSADATA data; CHECK(WSAStartup(MAKEWORD(2,2),&data)==0);
    fake_server server={0}; server.accepted=CreateEventW(NULL,TRUE,FALSE,NULL); CHECK(server.accepted);
    server.listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);CHECK(server.listener!=INVALID_SOCKET);
    struct sockaddr_in addr;memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=0;
    CHECK(bind(server.listener,(struct sockaddr*)&addr,sizeof(addr))==0&&listen(server.listener,1)==0);
    int alen=sizeof(addr);CHECK(getsockname(server.listener,(struct sockaddr*)&addr,&alen)==0);
    HANDLE srv=CreateThread(NULL,0,serve_slow,&server,0,NULL);CHECK(srv);
    generate_job job;memset(&job,0,sizeof(job));char url[96];snprintf(url,sizeof(url),"http://127.0.0.1:%u",ntohs(addr.sin_port));
    job.config.base_url=url;job.config.timeout_ms=9000;job.config.cancel=&job.cancel;
    HANDLE gen=CreateThread(NULL,0,generate_slow,&job,0,NULL);CHECK(gen);
    CHECK(WaitForSingleObject(server.accepted,3000)==WAIT_OBJECT_0);
    ULONGLONG start=GetTickCount64();InterlockedExchange((volatile LONG*)&job.cancel,1);
    CHECK(WaitForSingleObject(gen,2000)==WAIT_OBJECT_0);
    CHECK(GetTickCount64()-start<2000&&job.reply.stop==FR_STOP_CANCELLED&&job.rc!=0);
    fr_reply_free(&job.reply);CloseHandle(gen);closesocket(server.listener);WaitForSingleObject(srv,1500);CloseHandle(srv);CloseHandle(server.accepted);

    /* Cancellation racing a positive approval must win; the pre-approval
       checkpoint was already unsafe and the write must never be dispatched. */
    fake_server approved_server={0};approved_server.accepted=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(approved_server.accepted);
    approved_server.listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);CHECK(approved_server.listener!=INVALID_SOCKET);
    memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=0;
    CHECK(bind(approved_server.listener,(struct sockaddr*)&addr,sizeof(addr))==0&&listen(approved_server.listener,1)==0);
    alen=sizeof(addr);CHECK(getsockname(approved_server.listener,(struct sockaddr*)&addr,&alen)==0);
    HANDLE approved_thread=CreateThread(NULL,0,serve_tool_call,&approved_server,0,NULL);CHECK(approved_thread);
    wchar_t work_temp[MAX_PATH],work_dir[MAX_PATH],checkpoint[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,work_temp)&&GetTempFileNameW(work_temp,L"fca",0,work_dir));
    DeleteFileW(work_dir);CHECK(CreateDirectoryW(work_dir,NULL));swprintf_s(checkpoint,MAX_PATH,L"%s\\checkpoint.json",work_dir);
    char workspace_utf8[MAX_PATH],checkpoint_utf8[MAX_PATH],tool_url[96];
    WideCharToMultiByte(CP_UTF8,0,work_dir,-1,workspace_utf8,sizeof(workspace_utf8),NULL,NULL);
    WideCharToMultiByte(CP_UTF8,0,checkpoint,-1,checkpoint_utf8,sizeof(checkpoint_utf8),NULL,NULL);
    snprintf(tool_url,sizeof(tool_url),"http://127.0.0.1:%u",ntohs(addr.sin_port));volatile int approval_cancel=0;
    forge_run_options opt={0};opt.task="cancel write after approval";opt.workspace=workspace_utf8;opt.base_url=tool_url;opt.checkpoint=checkpoint_utf8;
    opt.timeout_ms=5000;opt.max_turns=2;opt.studio_mode=1;opt.cancel=&approval_cancel;opt.approval_reply=cancel_during_approval;opt.approval_context=(void*)&approval_cancel;
    int agent_rc=forge_run_agent(&opt);CHECK(agent_rc!=0);
    wchar_t cancelled_path[MAX_PATH];swprintf_s(cancelled_path,MAX_PATH,L"%s\\cancelled.txt",work_dir);CHECK(GetFileAttributesW(cancelled_path)==INVALID_FILE_ATTRIBUTES);
    FILE *cf=_wfopen(checkpoint,L"rb");CHECK(cf);char checkpoint_text[4096];size_t checkpoint_size=fread(checkpoint_text,1,sizeof(checkpoint_text)-1,cf);fclose(cf);checkpoint_text[checkpoint_size]=0;
    CHECK(strstr(checkpoint_text,"\"safe\":false")!=NULL);
    closesocket(approved_server.listener);WaitForSingleObject(approved_thread,1000);CloseHandle(approved_thread);CloseHandle(approved_server.accepted);DeleteFileW(checkpoint);RemoveDirectoryW(work_dir);

    wchar_t temp[MAX_PATH],dir[MAX_PATH],marker[MAX_PATH],exe[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp)&&GetTempFileNameW(temp,L"fct",0,dir));
    DeleteFileW(dir);CHECK(CreateDirectoryW(dir,NULL));swprintf_s(marker,MAX_PATH,L"%s\\late.txt",dir);
    DWORD n=GetEnvironmentVariableW(L"ComSpec",exe,MAX_PATH);CHECK(n&&n<MAX_PATH);const wchar_t *allowed[]={exe};
    ft_workspace ws;CHECK(ft_open(&ws,dir,FT_EXEC)==FT_OK);volatile int cancel=0;
    char out[128]={0},err[128]={0};size_t on=0,en=0;DWORD code=0;HANDLE timer=CreateThread(NULL,0,cancel_later,(void*)&cancel,0,NULL);CHECK(timer);
    ULONGLONG tool_start=GetTickCount64();ft_result tr=ft_run_approved_cancelable(&ws,exe,allowed,1,1,1,
        L"/c ping -n 6 127.0.0.1 >nul & echo late> late.txt",10000,&cancel,
        out,sizeof(out),&on,err,sizeof(err),&en,&code);
    CHECK(tr==FT_CANCELLED&&GetTickCount64()-tool_start<2000);WaitForSingleObject(timer,1000);CloseHandle(timer);
    Sleep(6200);CHECK(GetFileAttributesW(marker)==INVALID_FILE_ATTRIBUTES);
    RemoveDirectoryW(dir);WSACleanup();puts("OK test_cancellation: generation abort, approval race, unsafe checkpoint, Job Object tree kill");return 0;
}
