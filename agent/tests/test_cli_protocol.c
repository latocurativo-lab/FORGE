#define _CRT_SECURE_NO_WARNINGS
#include "coding_agent.h"
#include "forge_runtime.h"
#include "forge_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <windows.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(void){
    CHECK(!forge_task_requests_verification("Ciao, come stai?"));
    CHECK(!forge_task_requests_verification("Leggi il file e spiegamelo"));
    CHECK(forge_task_requests_verification("Per favore esegui i test"));
    CHECK(forge_task_requests_verification("Please VERIFY this change"));
    char *task=NULL,*workspace=NULL,*url=NULL,*checkpoint=NULL,*ve=NULL,*va=NULL,**allow=NULL,*request_id=NULL,*mcp_json=NULL;size_t allow_count=0;int approved=1,chat_mode=0;
    CHECK(forge_parse_start_json("{\"type\":\"start\",\"mode\":\"coding\",\"task\":\"hello\",\"workspace\":\"C:\\\\fixture\",\"base_url\":\"http://127.0.0.1:18932\",\"allow_exe\":[\"C:\\\\ctest.exe\",\"C:\\\\git.exe\"],\"verifier\":{\"executable\":\"C:\\\\ctest.exe\",\"args\":\"--version\"}}",&task,&workspace,&url,&checkpoint,&ve,&va,&allow,&allow_count,&chat_mode));
    CHECK(!chat_mode&&!strcmp(task,"hello")&&!strcmp(workspace,"C:\\fixture")&&!strcmp(url,"http://127.0.0.1:18932")&&!strcmp(ve,"C:\\ctest.exe")&&!strcmp(va,"--version")&&allow_count==2&&!strcmp(allow[0],"C:\\ctest.exe"));
    free(task);free(workspace);free(url);free(checkpoint);free(ve);free(va);for(size_t i=0;i<allow_count;i++)free(allow[i]);free(allow);
    task=workspace=url=checkpoint=ve=va=NULL;allow=NULL;allow_count=0;
    CHECK(forge_parse_start_json("{\"type\":\"start\",\"mode\":\"chat\",\"task\":\"CI SEI\",\"workspace\":\"C:\\\\fixture\",\"verifier\":{\"executable\":\"\",\"args\":\"\"}}",&task,&workspace,&url,&checkpoint,&ve,&va,&allow,&allow_count,&chat_mode));
    CHECK(chat_mode&&!strcmp(task,"CI SEI")&&!allow_count&&!strcmp(ve,"")&&!strcmp(va,""));
    free(task);free(workspace);free(url);free(checkpoint);free(ve);free(va);free(allow);
    CHECK(forge_parse_mcp_servers_json("{\"type\":\"start\",\"mcp_servers\":[{\"id\":\"fixture\"}]}",&mcp_json));CHECK(!strcmp(mcp_json,"[{\"id\":\"fixture\"}]"));free(mcp_json);
    CHECK(forge_parse_mcp_servers_json("{\"type\":\"start\"}",&mcp_json));CHECK(!strcmp(mcp_json,"[]"));free(mcp_json);
    CHECK(!forge_parse_mcp_servers_json("{\"type\":\"start\",\"mcp_servers\":{}}",&mcp_json));
    CHECK(forge_parse_approval_json("{\"type\":\"approval\",\"request_id\":\"pending-1\",\"approved\":false}",&request_id,&approved));CHECK(!strcmp(request_id,"pending-1")&&!approved);free(request_id);
    CHECK(!forge_parse_approval_json("{\"type\":\"approval\",\"request_id\":\"pending-1\",\"approved\":\"yes\"}",&request_id,&approved));
    CHECK(forge_approval_id_matches("pending-1","pending-1"));CHECK(!forge_approval_id_matches("pending-2","pending-1"));CHECK(!forge_approval_id_matches("pending-1",""));
    CHECK(forge_is_cancel_json("{\"type\":\"cancel\"}"));CHECK(!forge_is_cancel_json("{\"type\":\"start\"}"));
    CHECK(!forge_cli_confirm("test approval",1)); /* Studio has no approval-response message: default deny. */
    const char *mock_response="{\"choices\":[{\"finish_reason\":\"tool_calls\",\"message\":{\"content\":null,\"tool_calls\":[{\"id\":\"call_mock\",\"type\":\"function\",\"function\":{\"name\":\"read_file\",\"arguments\":\"{\\\"path\\\":\\\"a.c\\\"}\"}}]}}]}";
    fr_reply reply={0};char decode_error[256]={0};
    CHECK(fr_decode_response(mock_response,&reply,decode_error,sizeof(decode_error))==0);
    CHECK(reply.stop==FR_STOP_TOOL_CALL&&reply.tool_call_count==1&&!strcmp(reply.tool_calls[0].id,"call_mock"));
    CHECK(reply.text&&!*reply.text&&reply.tool_calls[0].arguments_len==strlen(reply.tool_calls[0].arguments));
    fr_message exchange[3]={{"assistant",reply.text,NULL,reply.tool_calls,reply.tool_call_count},{"tool","{\"ok\":true,\"result\":\"contents\"}",reply.tool_calls[0].id,NULL,0},{"assistant","continue",NULL,NULL,0}};
    char *encoded=NULL;size_t encoded_len=0;CHECK(fr_encode_messages(exchange,3,&encoded,&encoded_len,decode_error,sizeof(decode_error))==0);
    fj_value *encoded_tree=fjson_parse(encoded);CHECK(encoded_tree&&fjson_count(encoded_tree)==3);fjson_free(encoded_tree);free(encoded);fr_reply_free(&reply);
    const char *args_a="{\"path\":\"a.c\"}",*args_b="{\"path\":\"b.c\",\"expected\":\"old\",\"content\":\"new\"}";
    fr_tool_call calls[2]={{"call_a","read_file",(char*)args_a,strlen(args_a)},{"call_b","replace_file",(char*)args_b,strlen(args_b)}};
    fr_message m[4]={{"user","task",NULL,NULL,0},{"assistant",NULL,NULL,calls,2},{"tool","{\"ok\":true}","call_a",NULL,0},{"tool","{\"ok\":true}","call_b",NULL,0}};
    char *json=NULL,error[200]={0};size_t n=0;
    CHECK(fr_encode_messages(m,4,&json,&n,error,sizeof(error))==0);
    fj_value*v=fjson_parse(json);CHECK(v&&v->type==FJ_ARRAY&&fjson_count(v)==4);
    const fj_value*assistant=fjson_at(v,1);const fj_value*tc=fjson_get(assistant,"tool_calls");CHECK(tc&&fjson_count(tc)==2);
    const fj_value*tool2=fjson_at(v,3);const fj_value*id=fjson_get(tool2,"tool_call_id");CHECK(id&&id->type==FJ_STRING&&!strcmp(id->str,"call_b"));
    fjson_free(v);free(json);
    m[3].tool_call_id="missing-id";CHECK(fr_encode_messages(m,4,&json,&n,error,sizeof(error))!=0);free(json);
    FILE *tmp=tmpfile();CHECK(tmp);int saved=_dup(_fileno(stdout));CHECK(saved>=0);fflush(stdout);CHECK(_dup2(_fileno(tmp),_fileno(stdout))==0);
    CHECK(forge_emit_json_event("tool_result","ok",2,"read_file","call_a")==0);fflush(stdout);rewind(tmp);
    char line[2048];CHECK(fgets(line,sizeof(line),tmp)!=NULL);v=fjson_parse(line);CHECK(v&&v->type==FJ_OBJECT);
    const fj_value*type=fjson_get(v,"type"),*seq=fjson_get(v,"seq"),*tool=fjson_get(v,"tool");
    CHECK(type&&type->type==FJ_STRING&&!strcmp(type->str,"tool_result"));CHECK(seq&&seq->type==FJ_NUMBER);CHECK(tool&&tool->type==FJ_STRING&&!strcmp(tool->str,"read_file"));
    fjson_free(v);fflush(stdout);CHECK(_dup2(saved,_fileno(stdout))==0);_close(saved);fclose(tmp);
    puts("OK test_cli_protocol");return 0;
}
