#include "forge_tools.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(void) {
    wchar_t temp[MAX_PATH], root[MAX_PATH], cmd[MAX_PATH], path[MAX_PATH];
    ft_workspace ws, ro; char out[256], err[256]; size_t n, on, en; DWORD code;
    CHECK(GetTempPathW(MAX_PATH,temp)>0);
    CHECK(GetTempFileNameW(temp,L"ft",0,root)!=0);
    CHECK(DeleteFileW(root)); CHECK(CreateDirectoryW(root,NULL));
    CHECK(ft_open(&ws,root,FT_READ|FT_LIST|FT_WRITE|FT_EXEC)==FT_OK);
    CHECK(ft_open(&ro,root,FT_READ)==FT_OK);
    CHECK(ft_write(&ro,L"a.txt","abc",3)==FT_DENIED);
    CHECK(ft_write(&ws,L"a.txt","abc\ndef\n",8)==FT_OK);
    CHECK(ft_write(&ws,L"a.txt","x",1)==FT_IO);
    CHECK(ft_read(&ws,L"a.txt",out,sizeof(out),&n)==FT_OK && n==8 && !memcmp(out,"abc\ndef\n",8));
    CHECK(ft_read(&ws,L"a.txt",out,2,&n)==FT_LIMIT);
    CHECK(ft_read(&ws,L"..\\x",out,sizeof(out),&n)==FT_INVALID);
    CHECK(ft_read(&ws,L"C:\\x",out,sizeof(out),&n)==FT_INVALID);
    CHECK(ft_write(&ws,L"NUL.txt","x",1)==FT_INVALID);
    CHECK(ft_list(&ro,L"",out,sizeof(out),&n)==FT_DENIED);
    CHECK(ft_list(&ws,L"",out,sizeof(out),&n)==FT_OK && n==6 && !memcmp(out,"a.txt\n",6));
    CHECK(ft_search(&ws,L"a.txt","def",out,sizeof(out),&n)==FT_OK && n==4 && !memcmp(out,"def\n",4));
    CHECK(ft_write(&ws,L"edit.txt","original",8)==FT_OK);
    CHECK(ft_replace_approved(&ro,L"edit.txt","original",8,"updated",7,1)==FT_DENIED);
    CHECK(ft_replace_approved(&ws,L"edit.txt","original",8,"updated",7,0)==FT_DENIED);
    CHECK(ft_replace_approved(&ws,L"edit.txt","stale!!!",8,"updated",7,1)==FT_STALE);
    CHECK(ft_write(&ws,L"edit.txt","no",2)==FT_IO); /* create-only API never overwrites */
    CHECK(ft_read(&ws,L"edit.txt",out,sizeof(out),&n)==FT_OK && n==8 && !memcmp(out,"original",8));
    { wchar_t editpath[MAX_PATH]; HANDLE lock;
      swprintf(editpath,MAX_PATH,L"%ls\\edit.txt",root);
      lock=CreateFileW(editpath,GENERIC_READ,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL); CHECK(lock!=INVALID_HANDLE_VALUE);
      CHECK(ft_replace_approved(&ws,L"edit.txt","original",8,"updated",7,1)==FT_IO);
      CloseHandle(lock);
    }
    CHECK(ft_read(&ws,L"edit.txt",out,sizeof(out),&n)==FT_OK && n==8 && !memcmp(out,"original",8));
    CHECK(ft_replace_approved(&ws,L"edit.txt","original",8,"updated",7,1)==FT_OK);
    CHECK(ft_read(&ws,L"edit.txt",out,sizeof(out),&n)==FT_OK && n==7 && !memcmp(out,"updated",7));
    CHECK(GetSystemDirectoryW(cmd,MAX_PATH)>0); wcscat(cmd,L"\\cmd.exe");
    { const wchar_t *allow[]={cmd};
      CHECK(ft_run(&ro,cmd,L"/c exit 0",1000,&code)==FT_DENIED);
      CHECK(ft_run_approved(&ws,cmd,allow,1,0,1,L"/c echo blocked",1000,out,sizeof(out),&on,err,sizeof(err),&en,&code)==FT_DENIED);
      CHECK(ft_run_approved(&ws,cmd,allow,1,1,0,L"/c echo blocked",1000,out,sizeof(out),&on,err,sizeof(err),&en,&code)==FT_DENIED);
      CHECK(ft_run_approved(&ws,cmd,allow,0,1,1,L"/c echo blocked",1000,out,sizeof(out),&on,err,sizeof(err),&en,&code)==FT_INVALID);
      CHECK(ft_run_approved(&ws,cmd,allow,1,1,1,L"/c echo captured",5000,out,sizeof(out),&on,err,sizeof(err),&en,&code)==FT_OK && code==0 && on>=9 && strstr(out,"captured")!=NULL);
      CHECK(ft_run_approved(&ws,cmd,allow,1,1,1,L"/c echo diagnostic 1>&2",5000,out,sizeof(out),&on,err,sizeof(err),&en,&code)==FT_OK && en>=10 && strstr(err,"diagnostic")!=NULL);
      CHECK(ft_run_approved(&ws,cmd,allow,1,1,1,L"/c echo 123456789",5000,out,2,&on,err,sizeof(err),&en,&code)==FT_LIMIT);
      CHECK(ft_run_approved(&ws,cmd,allow,1,1,1,L"/c ping -n 5 127.0.0.1",100,out,sizeof(out),&on,err,sizeof(err),&en,&code)==FT_TIMEOUT);
    }
    /* An observed junction at the workspace root is refused. */
    { wchar_t link[MAX_PATH], target[MAX_PATH]; swprintf(link,MAX_PATH,L"%ls\\junction",root);
      CHECK(GetWindowsDirectoryW(target,MAX_PATH)>0);
      if(CreateSymbolicLinkW(link,target,SYMBOLIC_LINK_FLAG_DIRECTORY|SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) || CreateSymbolicLinkW(link,target,SYMBOLIC_LINK_FLAG_DIRECTORY)) {
          CHECK(ft_open(&ro,link,FT_READ)==FT_DENIED); RemoveDirectoryW(link);
      } /* Symlink creation can require Developer Mode/admin; skip only that case. */
    }
    swprintf(path,MAX_PATH,L"%ls\\a.txt",root); CHECK(DeleteFileW(path));
    swprintf(path,MAX_PATH,L"%ls\\edit.txt",root); CHECK(DeleteFileW(path)); CHECK(RemoveDirectoryW(root));
    return 0;
}
