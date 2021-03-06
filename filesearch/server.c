﻿#include "env.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#ifdef WIN32
    #include <aclapi.h>
    #include <io.h>
    #include <process.h>
#else
#include "posix_port.h"
#endif

#include "util.h"
#include "common.h"
#include "global.h"
#include "drive.h"
#include "sharelib.h"
#include "suffix.h"
#include "search.h"
#include "suffix.h"
#include "write.h"
#include "server.h"
#include "main.h"
#include "download.h"

static BOOL loaded_offline=0;

#ifndef WIN32

#include <errno.h>
ssize_t	writen(int fd, const void *vptr, size_t n){
	size_t		nleft;
	ssize_t		nwritten;
	const char	*ptr;

	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
		if ( (nwritten = write(fd, ptr, nleft)) <= 0) {
			if (errno == EINTR)
				nwritten = 0;		/* and call write() again */
			else
				return(-1);			/* error */
		}
		nleft -= nwritten;
		ptr   += nwritten;
	}
	return(n);
}
#endif

#ifdef WIN32
	#define WriteSock(fd,resp,len,sizet) WriteFile(fd, resp, len, &sizet, NULL);
#else
	#define WriteSock(fd,resp,len,sizet) sizet=writen(fd, resp, len);
#endif

static char * write_file(char *buffer, pFileEntry file){
	char *p = buffer;
	*p++ = '{';
	{
		memcpy(p,"\"name\":\"",8);
		p += 8;
		memcpy(p,file->FileName,file->us.v.FileNameLength);
		p += file->us.v.FileNameLength;
		*p++ ='"';
		*p++ =',';
	}
	{
		memcpy(p,"\"path\":\"",8);
		p += 8;
		p += print_path_str(file, p);
		*p++ ='"';
		*p++ =',';
	}
	{
		memcpy(p,"\"type\":\"",8);
		p += 8;
		p += print_suffix_type_by_file(file, p);
		*p++ ='"';
		*p++ =',';
	}
	if(!IsDir(file)){
		FSIZE size = GET_SIZE(file);
		int sizea = file_size_amount(size);
		int sizeu = file_size_unit(size);
		memcpy(p,"\"size\":\"",8);
		p += 8;
		p += sprintf(p,"%d",sizea);
		switch(sizeu){
			case 1: memcpy(p," KB", 3);break;
			case 2: memcpy(p," MB", 3);break;
			case 3: memcpy(p," GB", 3);break;
			default:memcpy(p,"  B", 3);break;
		}
		p +=3;
		*p++ ='"';
		*p++ =',';
	}
	{
		memcpy(p,"\"time\":\"",8);
		p += 8;
		p += print_time_str(file,p);
		*p++ ='"';
	}
	*p++ = '}';
	return p;
}


static void send_response_search(SockOut hNamedPipe, pSearchRequest req, pFileEntry *result, int count){
	char buffer[MAX_RESPONSE_LEN], *p1=buffer+sizeof(int), *p=p1;
	pFileEntry *start = result+req->from;
	int i;
	*p++ = '[';
	for(i=0;i<req->rows && i<count;i++){
		pFileEntry file = *(start+i);
		p = write_file(p,file);
		*p++ = ',';
		if((p-p1)>MAX_RESPONSE_LEN-100) break; //prevent buffer overflow
	}
	if(*(p-1)==',') p--;
	*p++ = ']';
	{
		DWORD nXfer;
		pSearchResponse resp = (pSearchResponse)buffer;
		resp->len = (p-p1);
		WriteSock(hNamedPipe, resp, (p-buffer), nXfer);
	}
}

static void send_response_stat(SockOut hNamedPipe, pSearchRequest req){
	char buffer[4096], *p1=buffer+sizeof(int), *p=p1;
	int *stats = statistic(req->str, &(req->env) );
	p += print_stat(stats,p);
	{
		DWORD nXfer;
		pSearchResponse resp = (pSearchResponse)buffer;
		resp->len = (p-p1);
		WriteSock(hNamedPipe, resp, (p-buffer), nXfer);
	}
}

static char * print_drive_info(char *buffer,int id){
	char *p = buffer;
	*p++ = '{';
	{
		memcpy(p,"\"id\":\"",6);
		p += 6;
		p += sprintf(p,"%d",id);
		*p++ ='"';
		*p++ =',';
	}
	{
		memcpy(p,"\"type\":\"",8);
		p += 8;
		p += sprintf(p,"%d",g_VolsInfo[id].type);
		*p++ ='"';
		*p++ =',';
	}
	{
		memcpy(p,"\"serialNumber\":\"",16);
		p += 16;
		p += sprintf(p,"%x",g_VolsInfo[id].serialNumber);
		*p++ ='"';
		*p++ =',';
	}
	{
		memcpy(p,"\"totalMB\":\"",11);
		p += 11;
		p += sprintf(p,"%d",g_VolsInfo[id].totalMB);
		*p++ ='"';
		*p++ =',';
	}
	{
		memcpy(p,"\"totalFreeMB\":\"",15);
		p += 15;
		p += sprintf(p,"%d",g_VolsInfo[id].totalFreeMB);
		*p++ ='"';
		*p++ =',';
	}
	{
		int str_len = strlen(g_VolsInfo[id].volumeName);
		memcpy(p,"\"volumeName\":\"",14);
		p += 14;
		memcpy(p,g_VolsInfo[id].volumeName,str_len);
		p += str_len;
		*p++ ='"';
		*p++ =',';
	}
	{
		int str_len = strlen(g_VolsInfo[id].fsName);
		memcpy(p,"\"fsName\":\"",10);
		p += 10;
		memcpy(p,g_VolsInfo[id].fsName,str_len);
		p += str_len;
		*p++ ='"';
	}
	*p++ = '}';
	return p;
}

static void send_response_index_status(SockOut hNamedPipe){
	char buffer[8192], *p1=buffer+sizeof(int), *p=p1;
	int i;
	*p++ = '[';
	for(i=0;i<DIRVE_COUNT_OFFLINE;i++){
		if(!g_loaded[i]) continue;
		p = print_drive_info(p,i);
		*p++ = ',';
		if((p-p1)>8192-100) break; //prevent buffer overflow
	}
	if(*(p-1)==',') p--;
	*p++ = ']';	
	{
		DWORD nXfer;
		pSearchResponse resp = (pSearchResponse)buffer;
		resp->len = (p-p1);
		WriteSock(hNamedPipe, resp, (p-buffer), nXfer);
	}
}

static BOOL print_db_visitor(char *db_name, void *data){
		char **pp = (char **)data;
		char *p = *pp;
	*p++ = '{';
	{
		memcpy(p,"\"name\":\"",8);
		p += 8;
		memcpy(p,db_name,strlen(db_name));
		p += strlen(db_name);
		*p++ ='"';
		*p++ =',';
	}
	{
		memcpy(p,"\"time\":\"",8);
		p += 8;
		{
			struct stat statbuf;
			stat(db_name, &statbuf);
			//p += sprintf(p,"%s",ctime(&statbuf.st_mtime));
			p += sprintf(p,"%d",(int)statbuf.st_mtime);
		}
		*p++ ='"';
	}
	*p++ = '}';
	*p++ =',';
		*pp = p;
		return 1;
}


static void send_response_cache_dbs(SockOut hNamedPipe){
	char buffer[8192], *p1=buffer+sizeof(int), *p=p1;
	*p++ = '[';
	DbIterator(print_db_visitor,&p);
	if(*(p-1)==',') p--;
	*p++ = ']';	
	{
		DWORD nXfer;
		pSearchResponse resp = (pSearchResponse)buffer;
		resp->len = (p-p1);
		WriteSock(hNamedPipe, resp, (p-buffer), nXfer);
	}
}

static void send_response_get_drives(SockOut hNamedPipe){
	char buffer[8192], *p1=buffer+sizeof(int), *p=p1;
	int i;
	*p++ = '[';
	for(i=0;i<DIRVE_COUNT;i++){
		if(!g_bVols[i]) continue;
		p = print_drive_info(p,i);
		*p++ = ',';
		if((p-p1)>8192-100) break; //prevent buffer overflow
	}
	if(*(p-1)==',') p--;
	*p++ = ']';	
	{
		DWORD nXfer;
		pSearchResponse resp = (pSearchResponse)buffer;
		resp->len = (p-p1);
		WriteSock(hNamedPipe, resp, (p-buffer), nXfer);
	}
}
static void send_response_char(SockOut hNamedPipe,char c){
	char buffer[8192], *p1=buffer+sizeof(int), *p=p1;
	*p++ = c;
	{
		DWORD nXfer;
		pSearchResponse resp = (pSearchResponse)buffer;
		resp->len = (p-p1);
		WriteSock(hNamedPipe, resp, (p-buffer), nXfer);
	}
}

static void send_response_file_type(SockOut hNamedPipe,char *file){
	char buffer[8192], *p1=buffer+sizeof(int), *p=p1;
	p+= print_suffix_type(suffix_type_by_filename(file,strlen(file)), p);
	{
		DWORD nXfer;
		pSearchResponse resp = (pSearchResponse)buffer;
		resp->len = (p-p1);
		WriteSock(hNamedPipe, resp, (p-buffer), nXfer);
	}
}

static void send_response_ok(SockOut hNamedPipe){
	send_response_char(hNamedPipe,'1');
}

static void load_offline_dbs_t(void *p){
	load_offline_dbs();
}

static void rescan_t(int *p){
	int i = *p;
	rescan(i);
}

static BOOL write_update_status_file(char status, char *fname){
    FILE *file;
    if ((file = fopen (UPDATE_CHECH_FILE, "w")) == NULL) return 0;
    fwrite(&status,sizeof(char),1,file);
    if(fname!=NULL) fwrite(fname,sizeof(char),strlen(fname),file);
    fclose (file);
    return 1;
}

static BOOL update_status(char status){
    return write_update_status_file(status,NULL);
}

static char get_update_status(){
	BOOL one_day_ago = file_passed_one_day(UPDATE_CHECH_FILE);
	if(one_day_ago){
		return UPDATE_CHECH_UNKNOWN;
	}else{
		FILE *file;
		char status=UPDATE_CHECH_UNKNOWN;
		if ((file = fopen(UPDATE_CHECH_FILE, "r")) == NULL){
			return UPDATE_CHECH_UNKNOWN;
		}
		fread(&status,sizeof(char),1,file);
		fclose (file);
		return status;
	}
}

static void send_response_update_file(SockOut hNamedPipe){
	char buffer[8192], *p1=buffer+sizeof(int), *p=p1;
	char status=UPDATE_CHECH_UNKNOWN;
	FILE *file;
	if ((file = fopen(UPDATE_CHECH_FILE, "r")) != NULL){
		if(fread(&status,sizeof(char),1,file)==1){
			if(status==UPDATE_CHECH_NEW){
				char fname[MAX_PATH] = {0};
				size_t ret = fread(fname,sizeof(char),MAX_PATH,file);
				if(ret>0 || feof(file) ){
					memcpy(p,fname,strlen(fname));
					//TODO: 发送全路径
					/*
					char fullpath[MAX_PATH] = {0};
					get_abs_path(fname,fullpath);
					memcpy(p,fname,strlen(fullpath));
                    p += strlen(fullpath);
					*/
                    p += strlen(fname);
				}
			}
		}
		fclose (file);
	}
	{
		DWORD nXfer;
		pSearchResponse resp = (pSearchResponse)buffer;
		resp->len = (p-p1);
		WriteSock(hNamedPipe, resp, (p-buffer), nXfer);
	}
}

static void download_t(void *str){// "http://host/filename?hash&version"
	WCHAR *filename;
	WCHAR *url =(WCHAR *)str;
	WCHAR *p = wcsrchr(url,L'?');
	WCHAR *p2 = wcsrchr(url,L'&');
	WCHAR *hash = p+1;
	WCHAR *version = p2+1;
	*p = L'\0';
	*p2 = L'\0';
	filename = wcsrchr(url,L'/')+1;
	if(download(url,filename)){
		char md5_2[MD5_LEN*2+1];
		char fname[MAX_PATH]={0};
		char md5[MAX_PATH];	
		wcstombs(fname,filename,MAX_PATH);
		wcstombs(md5,hash,MAX_PATH);
		MD5File(fname,md5_2);
		if(strncmp(md5,md5_2,MD5_LEN*2)==0){
            write_update_status_file(UPDATE_CHECH_NEW, fname);
		}else{
			printf("hash %s != %s.\n",md5,md5_2);
		}
	}
}

static void command_exec(WCHAR *command, SockOut hNamedPipe){
    printf("command: %ls\n",command);
	if(wcsncmp(command,L"index_status",wcslen(L"index_status"))==0){
		send_response_index_status(hNamedPipe);
	}else if(wcsncmp(command,L"cache_dbs",wcslen(L"cache_dbs"))==0){
		send_response_cache_dbs(hNamedPipe);
	}else if(wcsncmp(command,L"get_drives",wcslen(L"get_drives"))==0){
		send_response_get_drives(hNamedPipe);
	}else if(wcsncmp(command,L"load_offline_db",wcslen(L"load_offline_db"))==0){
		if(!loaded_offline){
			loaded_offline=1;
			_beginthread(load_offline_dbs_t,0,NULL);
		}
		send_response_ok(hNamedPipe);
	}else if(wcsncmp(command,L"rescan",wcslen(L"rescan"))==0){
		int i = *(command+wcslen(L"rescan")) - L'0';
		_beginthread(rescan_t,0, &i);
		send_response_ok(hNamedPipe);
	}else if(wcsncmp(command,L"del_offline_db",wcslen(L"del_offline_db"))==0){
		int i0 = *(command+wcslen(L"del_offline_db")) - L'0';
		int i1 = *(command+wcslen(L"del_offline_db?")) - L'0';
		del_offline_db(i0*10+i1);
		send_response_ok(hNamedPipe);
	}else if(wcsncmp(command,L"upgrade",wcslen(L"upgrade"))==0){
		WCHAR *url = command+wcslen(L"upgrade");
		if(wcsncmp(url,L"_none",wcslen(L"_none"))==0){
			update_status(UPDATE_CHECH_DONE);
			send_response_ok(hNamedPipe);
		}else if(wcsncmp(url,L"_status",wcslen(L"_status"))==0){
			send_response_char(hNamedPipe,get_update_status());
		}else if(wcsncmp(url,L"_file",wcslen(L"_file"))==0){
			send_response_update_file(hNamedPipe);
		}else if(wcsncmp(url,L"_test",wcslen(L"_test"))==0){
            char fname[MAX_PATH]={0};
            wcstombs(fname,url+6,MAX_PATH);
            write_update_status_file(*(url+5), fname);
			send_response_ok(hNamedPipe);
		}else{
			update_status(UPDATE_CHECH_DOWNLOADING);
			_beginthread(download_t,0,url);
			send_response_ok(hNamedPipe);
		}
	}else if(wcsncmp(command,L"type",wcslen(L"type"))==0){
        char buffer[MAX_PATH]={0};
        wchar_t *filename = command+wcslen(L"type");
        wchar_to_utf8_nocheck(filename, wcslen(filename), buffer, MAX_PATH);
		send_response_file_type(hNamedPipe,buffer);
	}else{
		send_response_ok(hNamedPipe);
	}
}

void process(SearchRequest req, SockOut out){
	SockOut hNamedPipe = out; 
	if(req.rows==-1){
		send_response_stat(hNamedPipe, &req);
	}else if(wcsncmp(req.str,L"[///",4)==0){
		WCHAR *command = req.str + 4;
		command_exec(command, hNamedPipe);
	}else{
		pFileEntry *result=NULL;
		int count = search(req.str,&(req.env),&result);
		send_response_search(hNamedPipe,&req,result,count);
		free_search(result);
	}
}