#define WIN32_LEAN_AND_MEAN
#include "coding_agent.h"
#include "forge_json.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static volatile int cancel_flag;
static int json_line(char *line,size_t cap){size_t n=0;int c;while((c=fgetc(stdin))!=EOF){if(c=='\n')break;if(n+1>=cap)return -1;line[n++]=(char)c;}if(!n&&c==EOF)return 0;line[n]=0;return 1;}
typedef struct { SRWLOCK lock; HANDLE ready; char active[96]; char response_id[96]; int response_value; int response_ready; } approval_queue;
static DWORD WINAPI cancel_reader(void*p){approval_queue*q=(approval_queue*)p;char line[4096];while(json_line(line,sizeof(line))>0){if(forge_is_cancel_json(line)){InterlockedExchange((volatile LONG*)&cancel_flag,1);SetEvent(q->ready);break;}char*id=NULL;int approved=0;if(forge_parse_approval_json(line,&id,&approved)){AcquireSRWLockExclusive(&q->lock);if(forge_approval_id_matches(q->active,id)&&!q->response_ready){strncpy_s(q->response_id,sizeof(q->response_id),id,_TRUNCATE);q->response_value=approved;q->response_ready=1;SetEvent(q->ready);}ReleaseSRWLockExclusive(&q->lock);free(id);}}return 0;}
static int studio_approval(void*ctx,const char*request_id,const char*tool_call_id,const char*tool,const char*arguments,const char*capability,int mutation){approval_queue*q=(approval_queue*)ctx;fj_value*e=fjson_object_new();fbuf b;DWORD waited;int result=0;if(!q||!e)return 0;AcquireSRWLockExclusive(&q->lock);strncpy_s(q->active,sizeof(q->active),request_id,_TRUNCATE);q->response_ready=0;q->response_id[0]=0;ReleaseSRWLockExclusive(&q->lock);
    if(fjson_object_set(e,"type",fjson_string_new("approval_required"))||fjson_object_set(e,"request_id",fjson_string_new(request_id))||fjson_object_set(e,"tool_call_id",fjson_string_new(tool_call_id))||fjson_object_set(e,"tool",fjson_string_new(tool))||fjson_object_set(e,"arguments",fjson_parse(arguments))||fjson_object_set(e,"capability",fjson_string_new(capability))||fjson_object_set(e,"mutation",fjson_bool_new(mutation))){fjson_free(e);return 0;}
    fb_init(&b);if(fjson_write(e,&b)){fb_free(&b);fjson_free(e);return 0;}fwrite(b.data,1,b.len,stdout);fputc('\n',stdout);fflush(stdout);fb_free(&b);fjson_free(e);
     waited=WaitForSingleObject(q->ready,30000);if(waited==WAIT_OBJECT_0&&!InterlockedCompareExchange((volatile LONG*)&cancel_flag,0,0)){AcquireSRWLockExclusive(&q->lock);if(q->response_ready&&forge_approval_id_matches(request_id,q->response_id))result=q->response_value?1:0;q->response_ready=0;q->active[0]=0;ReleaseSRWLockExclusive(&q->lock);}else{AcquireSRWLockExclusive(&q->lock);q->active[0]=0;q->response_ready=0;ReleaseSRWLockExclusive(&q->lock);if(waited==WAIT_TIMEOUT)forge_emit_json_event("approval_timeout","Approval scaduta dopo 30 secondi; nessuna azione eseguita.",0,tool,tool_call_id);}return result;}
static int run_studio(void){
    /* The default Windows executable stack is commonly 1 MiB. Keep the large
       JSONL input buffer off-stack: allocating it as a local array caused
       STATUS_STACK_OVERFLOW before the first event on a minimal start request. */
    const size_t line_capacity=1024u*1024u;
    char *line=(char*)malloc(line_capacity);
    if(!line){fprintf(stderr,"out of memory allocating JSONL input\n");return 2;}
    int got=json_line(line,line_capacity);
    if(got<=0){fprintf(stderr,"expected JSONL start on stdin\n");free(line);return 2;}
    /* Some Windows PowerShell/.NET versions prepend the standard UTF-8 BOM to
       a redirected StandardInput stream. JSON Lines starts are UTF-8, so accept
       that optional BOM on the first record only. */
    char *start_line=line;
    if((unsigned char)line[0]==0xEF&&(unsigned char)line[1]==0xBB&&(unsigned char)line[2]==0xBF)start_line+=3;
     forge_run_options o={0};char*task=NULL,*workspace=NULL,*url=NULL,*checkpoint=NULL,*ve=NULL,*va=NULL,*mcp_servers=NULL,*history_json=NULL,*instructions=NULL;char**allow=NULL;size_t allow_count=0;int chat_mode=0,checkpoint_is_generated=0;char generated_checkpoint[MAX_PATH];approval_queue q={SRWLOCK_INIT,NULL,{0},{0},0,0};q.ready=CreateEventA(NULL,FALSE,FALSE,NULL);
    if(!q.ready){fprintf(stderr,"CreateEvent for approval queue failed (Windows error %lu)\n",(unsigned long)GetLastError());free(line);return 2;}
     if(!forge_parse_start_json(start_line,&task,&workspace,&url,&checkpoint,&ve,&va,&allow,&allow_count,&chat_mode)){
        size_t input_len=strlen(line);
        fprintf(stderr,"invalid start JSON or missing task/workspace (bytes=%zu prefix=%02X %02X %02X %02X)\n",
            input_len,(unsigned char)(input_len>0?line[0]:0),(unsigned char)(input_len>1?line[1]:0),
            (unsigned char)(input_len>2?line[2]:0),(unsigned char)(input_len>3?line[3]:0));
        CloseHandle(q.ready);free(line);return 2;
    }
    if(!forge_parse_mcp_servers_json(start_line,&mcp_servers)){fprintf(stderr,"invalid mcp_servers configuration\n");free(task);free(workspace);free(url);free(checkpoint);free(ve);free(va);for(size_t i=0;i<allow_count;i++)free(allow[i]);free(allow);CloseHandle(q.ready);free(line);return 2;}
    if(!forge_parse_context_fields(start_line,&history_json,&instructions)){fprintf(stderr,"invalid continuity context (history/instructions)\n");free(task);free(workspace);free(url);free(checkpoint);free(ve);free(va);free(mcp_servers);for(size_t i=0;i<allow_count;i++)free(allow[i]);free(allow);CloseHandle(q.ready);free(line);return 2;}
    free(line);
    if(!checkpoint){char temp[MAX_PATH];DWORD tn=GetTempPathA(MAX_PATH,temp);if(!tn||tn>=MAX_PATH||!GetTempFileNameA(temp,"frg",0,generated_checkpoint)){fprintf(stderr,"cannot allocate Studio checkpoint path\n");free(task);free(workspace);free(url);free(ve);free(va);free(mcp_servers);free(history_json);free(instructions);for(size_t i=0;i<allow_count;i++)free(allow[i]);free(allow);CloseHandle(q.ready);return 2;}checkpoint=generated_checkpoint;checkpoint_is_generated=1;}
     o.task=task;o.workspace=workspace;o.base_url=url;o.checkpoint=checkpoint;o.verify_exe=ve;o.verify_args=va;o.allow_exes=(const char*const*)allow;o.allow_exe_count=allow_count;o.mcp_servers_json=mcp_servers;o.history_json=history_json;o.instructions=instructions;o.timeout_ms=120000;o.max_turns=32;o.studio_mode=1;o.chat_mode=chat_mode;o.cancel=&cancel_flag;o.approval_reply=studio_approval;o.approval_context=&q;
    HANDLE thread=CreateThread(NULL,0,cancel_reader,&q,0,NULL);if(!thread){fprintf(stderr,"cannot start JSONL input reader\n");free(task);free(workspace);free(url);if(!checkpoint_is_generated)free(checkpoint);free(ve);free(va);free(mcp_servers);free(history_json);free(instructions);for(size_t i=0;i<allow_count;i++)free(allow[i]);free(allow);CloseHandle(q.ready);return 2;}int rc=forge_run_agent(&o);InterlockedExchange((volatile LONG*)&cancel_flag,1);SetEvent(q.ready);DWORD reader_wait=WaitForSingleObject(thread,5000);if(reader_wait==WAIT_TIMEOUT){fprintf(stderr,"JSONL reader did not stop after agent completion; cancelling blocked stdin read\n");CancelSynchronousIo(thread);WaitForSingleObject(thread,INFINITE);}CloseHandle(thread);free(task);free(workspace);free(url);if(!checkpoint_is_generated)free(checkpoint);free(ve);free(va);free(mcp_servers);free(history_json);free(instructions);for(size_t i=0;i<allow_count;i++)free(allow[i]);free(allow);CloseHandle(q.ready);return rc;
}
static void usage(void){fprintf(stderr,"Usage:\n  forge run --task TEXT --workspace DIR --verify-exe EXE --verify-args ARGS [--allow-exe EXE]* [--checkpoint FILE] [--max-turns N] [--timeout-ms N] [--base-url URL]\n  forge resume --task TEXT --workspace DIR --checkpoint FILE --verify-exe EXE --verify-args ARGS [--allow-exe EXE]*\n  Studio: forge.exe with JSONL start/cancel on stdin\n");}
static int number(const char*s,unsigned*out){char*e;unsigned long n=strtoul(s,&e,10);if(!*s||*e||!n||n>1000000)return 0;*out=(unsigned)n;return 1;}
int main(int argc,char**argv){
    if(argc==1)return run_studio();forge_run_options o={0};const char**allow=(const char**)calloc((size_t)argc,sizeof(char*));int resume=0;
    if(!allow){fprintf(stderr,"out of memory\n");return 2;}if(argc<2){usage();free(allow);return 2;}
    if(!strcmp(argv[1],"resume"))resume=1;else if(strcmp(argv[1],"run")){usage();free(allow);return 2;}
    o.resume=resume;o.timeout_ms=120000;o.max_turns=64;o.base_url="http://127.0.0.1:18932";
    for(int i=2;i<argc;i++){
        if(i+1>=argc){usage();free(allow);return 2;}
        if(!strcmp(argv[i],"--task"))o.task=argv[++i];else if(!strcmp(argv[i],"--workspace"))o.workspace=argv[++i];
        else if(!strcmp(argv[i],"--verify-exe"))o.verify_exe=argv[++i];else if(!strcmp(argv[i],"--verify-args"))o.verify_args=argv[++i];
        else if(!strcmp(argv[i],"--checkpoint"))o.checkpoint=argv[++i];else if(!strcmp(argv[i],"--base-url"))o.base_url=argv[++i];
        else if(!strcmp(argv[i],"--allow-exe"))allow[o.allow_exe_count++]=argv[++i];
        else if(!strcmp(argv[i],"--max-turns")){if(!number(argv[++i],&o.max_turns)){fprintf(stderr,"invalid --max-turns\n");free(allow);return 2;}}
        else if(!strcmp(argv[i],"--timeout-ms")){if(!number(argv[++i],&o.timeout_ms)){fprintf(stderr,"invalid --timeout-ms\n");free(allow);return 2;}}
        else {fprintf(stderr,"unknown option: %s\n",argv[i]);usage();free(allow);return 2;}
    }
    if(!o.task||!o.workspace||(resume&&!o.checkpoint)||((o.verify_exe==NULL)!=(o.verify_args==NULL))){usage();free(allow);return 2;}
    if(!o.checkpoint){static char checkpoint[MAX_PATH];if(snprintf(checkpoint,sizeof(checkpoint),"%s\\.forge-checkpoint.json",o.workspace)>=(int)sizeof(checkpoint)){fprintf(stderr,"workspace path too long\n");free(allow);return 2;}o.checkpoint=checkpoint;}
    o.allow_exes=allow;int rc=forge_run_agent(&o);free(allow);return rc;
}
