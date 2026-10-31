#include "forge_tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

static int component_ok(const wchar_t *s, size_t n) {
    size_t i, stem; wchar_t name[8];
    if (!n || (n == 1 && s[0] == L'.') || (n == 2 && s[0] == L'.' && s[1] == L'.') || s[n-1] == L'.' || s[n-1] == L' ') return 0;
    for (i=0;i<n;i++) if (s[i] < 32 || wcschr(L":*?\"<>|/", s[i])) return 0;
    for (stem=0;stem<n && s[stem]!=L'.';stem++) {}
    if (stem<8) {
        for (i=0;i<stem;i++) name[i]=(wchar_t)towupper(s[i]); name[stem]=0;
        if (!wcscmp(name,L"CON") || !wcscmp(name,L"PRN") || !wcscmp(name,L"AUX") || !wcscmp(name,L"NUL") ||
            (stem==4 && (!wcsncmp(name,L"COM",3) || !wcsncmp(name,L"LPT",3)) && name[3]>=L'1' && name[3]<=L'9')) return 0;
    }
    return 1;
}
/* Reject all reparse points in existing components, including the root. */
static ft_result resolve(const ft_workspace *ws, const wchar_t *rel, int missing_last, wchar_t *out) {
    const wchar_t *p, *q; size_t base;
    DWORD attr;
    if (!ws || !rel || rel[0] == L'\\' || rel[0] == L'/') return FT_INVALID;
    if (wcslen(ws->root) + wcslen(rel) + 2 >= MAX_PATH) return FT_LIMIT;
    wcscpy(out, ws->root); base = wcslen(out);
    attr = GetFileAttributesW(out);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT) || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return FT_DENIED;
    if (!*rel) return FT_OK;
    p=rel;
    while (*p) {
        q=wcschr(p,L'\\'); if (!q) q=p+wcslen(p);
        if (!component_ok(p,(size_t)(q-p))) return FT_INVALID;
        out[base++]=L'\\'; wmemcpy(out+base,p,(size_t)(q-p)); base+=(size_t)(q-p); out[base]=0;
        attr=GetFileAttributesW(out);
        if (attr == INVALID_FILE_ATTRIBUTES) {
            if (missing_last && !*q && GetLastError()==ERROR_FILE_NOT_FOUND) return FT_OK;
            return FT_IO;
        }
        if (attr & FILE_ATTRIBUTE_REPARSE_POINT) return FT_DENIED;
        if (*q && !(attr & FILE_ATTRIBUTE_DIRECTORY)) return FT_INVALID;
        if (!*q) break;
        p=q+1; if (!*p) return FT_INVALID;
    }
    return FT_OK;
}
ft_result ft_open(ft_workspace *ws, const wchar_t *root, unsigned permissions) {
    wchar_t full[MAX_PATH], prefix[MAX_PATH]; DWORD n, attr; size_t i;
    if (!ws || !root || (permissions & ~(FT_READ|FT_LIST|FT_WRITE|FT_EXEC))) return FT_INVALID;
    n=GetFullPathNameW(root, MAX_PATH, full, NULL);
    if (!n || n>=MAX_PATH || n<3 || full[1]!=L':') return FT_INVALID;
    while (n>3 && full[n-1]==L'\\') full[--n]=0;
    /* Reject observed reparse points in every existing ancestor, not only root. */
    wcscpy(prefix,full);
    for(i=3;prefix[i];i++) if(prefix[i]==L'\\') {
        prefix[i]=0; attr=GetFileAttributesW(prefix); prefix[i]=L'\\';
        if(attr==INVALID_FILE_ATTRIBUTES || (attr&FILE_ATTRIBUTE_REPARSE_POINT) || !(attr&FILE_ATTRIBUTE_DIRECTORY)) return FT_DENIED;
    }
    attr=GetFileAttributesW(full);
    if (attr==INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY) || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return FT_DENIED;
    wcscpy(ws->root,full); ws->permissions=permissions; return FT_OK;
}
ft_result ft_read(const ft_workspace *ws,const wchar_t *path,void *out,size_t cap,size_t *len) {
    wchar_t p[MAX_PATH]; HANDLE h; LARGE_INTEGER sz; DWORD got; ft_result r;
    if (!ws || !len || (!out && cap) || cap>0xffffffffu) return FT_INVALID;
    *len=0; if (!(ws->permissions & FT_READ)) return FT_DENIED;
    r=resolve(ws,path,0,p); if(r) return r;
    h=CreateFileW(p,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(h==INVALID_HANDLE_VALUE) return FT_IO;
    { BY_HANDLE_FILE_INFORMATION info; if (!GetFileInformationByHandle(h,&info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))) { CloseHandle(h); return FT_DENIED; } }
    if (!GetFileSizeEx(h,&sz) || sz.QuadPart<0) { CloseHandle(h); return FT_IO; }
    if ((unsigned long long)sz.QuadPart>cap) { CloseHandle(h); return FT_LIMIT; }
    if (!ReadFile(h,out,(DWORD)sz.QuadPart,&got,NULL)) { CloseHandle(h); return FT_IO; }
    CloseHandle(h); *len=got; return FT_OK;
}
ft_result ft_write(const ft_workspace *ws,const wchar_t *path,const void *data,size_t len) {
    wchar_t p[MAX_PATH]; HANDLE h; DWORD written; ft_result r;
    if (!ws || (!data && len) || len>0xffffffffu || !path || !*path) return FT_INVALID;
    if (!(ws->permissions & FT_WRITE)) return FT_DENIED;
    r=resolve(ws,path,1,p); if(r) return r;
    /* CREATE_NEW prevents accidental overwrite and final-component link following. */
    h=CreateFileW(p,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(h==INVALID_HANDLE_VALUE) return FT_IO;
    if (!WriteFile(h,data,(DWORD)len,&written,NULL) || written!=len) { CloseHandle(h); return FT_IO; }
    CloseHandle(h); return FT_OK;
}
static int file_matches(HANDLE h,const void *expected,size_t expected_len) {
    LARGE_INTEGER size,zero; unsigned char buf[8192]; size_t offset=0; DWORD got;
    if(!GetFileSizeEx(h,&size)||size.QuadPart<0||(unsigned long long)size.QuadPart!=expected_len) return 0;
    zero.QuadPart=0; if(!SetFilePointerEx(h,zero,NULL,FILE_BEGIN)) return 0;
    while(offset<expected_len) {
        DWORD ask=(DWORD)((expected_len-offset)>sizeof(buf)?sizeof(buf):(expected_len-offset));
        if(!ReadFile(h,buf,ask,&got,NULL)||got!=ask||memcmp(buf,(const unsigned char*)expected+offset,got)) return 0;
        offset+=got;
    }
    return 1;
}
ft_result ft_replace_approved(const ft_workspace *ws,const wchar_t *path,
    const void *expected,size_t expected_len,const void *replacement,size_t replacement_len,int approved) {
    wchar_t target[MAX_PATH],temp[MAX_PATH],directory[MAX_PATH]; wchar_t *slash;
    HANDLE original=INVALID_HANDLE_VALUE,stage=INVALID_HANDLE_VALUE; DWORD written; ft_result r;
    if(!ws||!path||!*path||(!expected&&expected_len)||(!replacement&&replacement_len)||
       expected_len>0xffffffffu||replacement_len>0xffffffffu) return FT_INVALID;
    if(!(ws->permissions&FT_WRITE)||!approved) return FT_DENIED;
    r=resolve(ws,path,0,target); if(r) return r;
    original=CreateFileW(target,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(original==INVALID_HANDLE_VALUE) return FT_IO;
    { BY_HANDLE_FILE_INFORMATION info;
      if(!GetFileInformationByHandle(original,&info)) { CloseHandle(original); return FT_IO; }
      if(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) { CloseHandle(original); return FT_DENIED; }
    }
    if(!file_matches(original,expected,expected_len)) { CloseHandle(original); return FT_STALE; }
    wcscpy(directory,target); slash=wcsrchr(directory,L'\\'); if(!slash) { CloseHandle(original); return FT_INVALID; } *slash=0;
    if(!GetTempFileNameW(directory,L"ft",0,temp)) { CloseHandle(original); return FT_IO; }
    stage=CreateFileW(temp,GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(stage==INVALID_HANDLE_VALUE) { CloseHandle(original); DeleteFileW(temp); return FT_IO; }
    { BY_HANDLE_FILE_INFORMATION info;
      if(!GetFileInformationByHandle(stage,&info)|| (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))) { CloseHandle(stage); CloseHandle(original); DeleteFileW(temp); return FT_DENIED; }
    }
    if(replacement_len&&(!WriteFile(stage,replacement,(DWORD)replacement_len,&written,NULL)||written!=replacement_len)) { CloseHandle(stage); CloseHandle(original); DeleteFileW(temp); return FT_IO; }
    if(!FlushFileBuffers(stage)) { CloseHandle(stage); CloseHandle(original); DeleteFileW(temp); return FT_IO; }
    CloseHandle(stage); stage=INVALID_HANDLE_VALUE;
    /* Recheck immediately before replacement; this narrows, but cannot eliminate, TOCTOU. */
    if(!file_matches(original,expected,expected_len)) { CloseHandle(original); DeleteFileW(temp); return FT_STALE; }
    CloseHandle(original); original=INVALID_HANDLE_VALUE;
    if(!ReplaceFileW(target,temp,NULL,0,NULL,NULL)) { DeleteFileW(temp); return FT_IO; }
    return FT_OK;
}
static ft_result append(char *out,size_t cap,size_t *len,const char *s,size_t n) {
    if(n>cap-*len) return FT_LIMIT; memcpy(out+*len,s,n); *len+=n; return FT_OK;
}
ft_result ft_list(const ft_workspace *ws,const wchar_t *path,char *out,size_t cap,size_t *len) {
    wchar_t p[MAX_PATH]; WIN32_FIND_DATAW fd; HANDLE h; ft_result r; char name[MAX_PATH*4]; int n; DWORD err;
    if (!ws || !len || (!out && cap)) return FT_INVALID; *len=0;
    if (!(ws->permissions & FT_LIST)) return FT_DENIED;
    r=resolve(ws,path,0,p); if(r) return r;
    if (wcslen(p)+3>=MAX_PATH) return FT_LIMIT; wcscat(p,L"\\*");
    h=FindFirstFileW(p,&fd); if(h==INVALID_HANDLE_VALUE) return FT_IO;
    do {
        if (!wcscmp(fd.cFileName,L".") || !wcscmp(fd.cFileName,L"..")) continue;
        n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,fd.cFileName,-1,name,sizeof(name),NULL,NULL);
        if (!n) { FindClose(h); return FT_IO; }
        r=append(out,cap,len,name,(size_t)n-1); if (!r) r=append(out,cap,len,"\n",1);
        if(r) { FindClose(h); return r; }
    } while(FindNextFileW(h,&fd));
    err=GetLastError(); FindClose(h); return err==ERROR_NO_MORE_FILES ? FT_OK : FT_IO;
}
ft_result ft_search(const ft_workspace *ws,const wchar_t *path,const char *needle,char *out,size_t cap,size_t *len) {
    char *buf, *p, *end, *line; size_t n; ft_result r;
    if (!needle || !*needle || !len || (!out && cap)) return FT_INVALID; *len=0;
    buf=(char*)malloc(1024*1024+1); if(!buf) return FT_IO;
    r=ft_read(ws,path,buf,1024*1024,&n); if(r) { free(buf); return r; }
    if (memchr(buf,0,n)) { free(buf); return FT_INVALID; } buf[n]=0;
    p=buf; end=buf+n;
    while(p<end) { line=p; while(p<end && *p!='\n') p++; if(p<end) p++; { char save=*p; *p=0; if(strstr(line,needle)) { r=append(out,cap,len,line,(size_t)(p-line)); } *p=save; if(r) break; } }
    free(buf); return r;
}
typedef struct { HANDLE pipe; char *out; size_t cap, len; int overflow; } ft_reader;
static DWORD WINAPI drain_pipe(void *arg) {
    ft_reader *r=(ft_reader*)arg; char chunk[4096]; DWORD got;
    while(ReadFile(r->pipe,chunk,sizeof(chunk),&got,NULL) && got) {
        size_t take=got; if(take>r->cap-r->len) { take=r->cap-r->len; r->overflow=1; }
        if(take) memcpy(r->out+r->len,chunk,take); r->len+=take;
    }
    return 0;
}
ft_result ft_run_approved_cancelable(const ft_workspace *ws,const wchar_t *exe,
    const wchar_t *const *allowlist,size_t allowlist_count,int approved,int allow_mutation,
    const wchar_t *args,DWORD timeout,volatile int *cancel,char *stdout_buf,size_t stdout_cap,size_t *stdout_len,
    char *stderr_buf,size_t stderr_cap,size_t *stderr_len,DWORD *exit_code) {
    STARTUPINFOEXW sx; PROCESS_INFORMATION pi; JOBOBJECT_EXTENDED_LIMIT_INFORMATION ji;
    SECURITY_ATTRIBUTES sa; HANDLE job=NULL,outread=NULL,outwrite=NULL,errread=NULL,errwrite=NULL,ot=NULL,et=NULL;
    wchar_t *cmd=NULL; size_t n,i; DWORD wait; SIZE_T attr_bytes=0; ft_result result=FT_IO; ft_reader ro,re; HANDLE inherited[2]; LPPROC_THREAD_ATTRIBUTE_LIST attrs=NULL; int attrs_ready=0;
    if(!ws||!exe||!args||!stdout_len||!stderr_len||!exit_code||(!stdout_buf&&stdout_cap)||(!stderr_buf&&stderr_cap)||
       !timeout||timeout==INFINITE||!allowlist||!allowlist_count||wcslen(exe)<3||exe[1]!=L':'||exe[2]!=L'\\'||wcschr(exe,L'"')) return FT_INVALID;
    *stdout_len=*stderr_len=0;
    if(!(ws->permissions&FT_EXEC)||!approved||!allow_mutation) return FT_DENIED;
    if(cancel&&*cancel) return FT_CANCELLED;
    for(i=0;i<allowlist_count;i++) if(allowlist[i]&&!_wcsicmp(exe,allowlist[i])) break;
    if(i==allowlist_count) return FT_DENIED;
    n=wcslen(exe)+wcslen(args)+4; if(n>32767) return FT_LIMIT;
    cmd=(wchar_t*)malloc(n*sizeof(wchar_t)); if(!cmd) return FT_IO; swprintf(cmd,n,L"\"%ls\" %ls",exe,args);
    memset(&sa,0,sizeof(sa)); sa.nLength=sizeof(sa); sa.bInheritHandle=TRUE;
    if(!CreatePipe(&outread,&outwrite,&sa,0)||!SetHandleInformation(outread,HANDLE_FLAG_INHERIT,0)||
       !CreatePipe(&errread,&errwrite,&sa,0)||!SetHandleInformation(errread,HANDLE_FLAG_INHERIT,0)) goto done;
    ro.pipe=outread; ro.out=stdout_buf; ro.cap=stdout_cap; ro.len=0; ro.overflow=0;
    re.pipe=errread; re.out=stderr_buf; re.cap=stderr_cap; re.len=0; re.overflow=0;
    ot=CreateThread(NULL,0,drain_pipe,&ro,0,NULL); et=CreateThread(NULL,0,drain_pipe,&re,0,NULL);
    if(!ot||!et) goto done;
    job=CreateJobObjectW(NULL,NULL); if(!job) goto done;
    memset(&ji,0,sizeof(ji)); ji.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&ji,sizeof(ji))) goto done;
    memset(&sx,0,sizeof(sx)); sx.StartupInfo.cb=sizeof(sx); sx.StartupInfo.dwFlags=STARTF_USESTDHANDLES; sx.StartupInfo.hStdOutput=outwrite; sx.StartupInfo.hStdError=errwrite; sx.StartupInfo.hStdInput=NULL;
    inherited[0]=outwrite; inherited[1]=errwrite;
    InitializeProcThreadAttributeList(NULL,1,0,&attr_bytes); attrs=(LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attr_bytes); if(!attrs) goto done;
    if(!InitializeProcThreadAttributeList(attrs,1,0,&attr_bytes)) goto done; attrs_ready=1;
    if(!UpdateProcThreadAttribute(attrs,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),NULL,NULL)) goto done;
    sx.lpAttributeList=attrs; memset(&pi,0,sizeof(pi));
     if(!CreateProcessW(exe,cmd,NULL,NULL,TRUE,CREATE_SUSPENDED|CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,NULL,ws->root,&sx.StartupInfo,&pi)) goto done;
     CloseHandle(outwrite); outwrite=NULL; CloseHandle(errwrite); errwrite=NULL;
     if(!AssignProcessToJobObject(job,pi.hProcess)) { TerminateProcess(pi.hProcess,1); WaitForSingleObject(pi.hProcess,INFINITE); goto child_done; }
     if(cancel&&*cancel) { TerminateJobObject(job,1); WaitForSingleObject(pi.hProcess,INFINITE); result=FT_CANCELLED; goto child_done; }
    if(ResumeThread(pi.hThread)==(DWORD)-1) { TerminateJobObject(job,1); goto child_done; }
     { DWORD elapsed=0; for(;;) {
         DWORD slice=timeout-elapsed; if(slice>10) slice=10;
         wait=WaitForSingleObject(pi.hProcess,slice);
         if(wait==WAIT_OBJECT_0) break;
         if(wait!=WAIT_TIMEOUT) goto child_done;
         if(cancel&&*cancel) { TerminateJobObject(job,1); WaitForSingleObject(pi.hProcess,INFINITE); result=FT_CANCELLED; break; }
         elapsed+=slice; if(elapsed>=timeout) { TerminateJobObject(job,1); WaitForSingleObject(pi.hProcess,INFINITE); result=FT_TIMEOUT; break; }
       } }
     if(wait==WAIT_OBJECT_0&&GetExitCodeProcess(pi.hProcess,exit_code)) result=FT_OK;
child_done: CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    /* Closing a kill-on-close job also ends descendants which inherited stdio handles. */
    CloseHandle(job); job=NULL;
    CloseHandle(outread); outread=NULL; CloseHandle(errread); errread=NULL;
    WaitForSingleObject(ot,INFINITE); WaitForSingleObject(et,INFINITE);
    *stdout_len=ro.len; *stderr_len=re.len;
    if(result==FT_OK&&(ro.overflow||re.overflow)) result=FT_LIMIT;
done:
    if(attrs) { if(attrs_ready) DeleteProcThreadAttributeList(attrs); free(attrs); }
    if(outread) CloseHandle(outread); if(outwrite) CloseHandle(outwrite); if(errread) CloseHandle(errread); if(errwrite) CloseHandle(errwrite);
    if(ot) { WaitForSingleObject(ot,INFINITE); CloseHandle(ot); } if(et) { WaitForSingleObject(et,INFINITE); CloseHandle(et); }
    if(job) CloseHandle(job); free(cmd); return result;
}
ft_result ft_run_approved(const ft_workspace *ws,const wchar_t *exe,
    const wchar_t *const *allowlist,size_t allowlist_count,int approved,int allow_mutation,
    const wchar_t *args,DWORD timeout,char *stdout_buf,size_t stdout_cap,size_t *stdout_len,
    char *stderr_buf,size_t stderr_cap,size_t *stderr_len,DWORD *exit_code) {
    return ft_run_approved_cancelable(ws,exe,allowlist,allowlist_count,approved,allow_mutation,
        args,timeout,NULL,stdout_buf,stdout_cap,stdout_len,stderr_buf,stderr_cap,stderr_len,exit_code);
}
ft_result ft_run(const ft_workspace *ws,const wchar_t *exe,const wchar_t *args,DWORD timeout,DWORD *exit_code) {
    (void)ws;(void)exe;(void)args;(void)timeout;(void)exit_code; return FT_DENIED;
}
