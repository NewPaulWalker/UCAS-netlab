#include "tcp_sock.h"
#include "tcp_apps.h"
#include "log.h"

#include <unistd.h>

// tcp server application, listens to port (specified by arg) and serves many
// connection request
void *tcp_server(void *arg)
{
	u16 port = *(u16 *)arg;
	struct tcp_sock *tsk = alloc_tcp_sock();
	struct sock_addr addr;
	addr.ip = htonl(0);
	addr.port = port;
	if (tcp_sock_bind(tsk, &addr) < 0) {
		log(ERROR, "tcp_sock bind to port %hu failed", ntohs(port));
		exit(1);
	}
	if (tcp_sock_listen(tsk, 10) < 0) {
		log(ERROR, "tcp_sock listen failed");
		exit(1);
	}

	while(1){
		struct tcp_sock *csk = tcp_sock_accept(tsk);
		pthread_t new_thread;
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		int status = pthread_create(&new_thread,&attr,(void *)handle_http_request,(void *)csk);
		if (status!=0){
			perror("Create handle_http_request thread failed");
			exit(1);
		}
	}

	tcp_sock_close(tsk);

	return NULL;
}
void *handle_http_request(void *arg){
	struct tcp_sock *tsk = (struct tcp_sock *)arg;
	int keep_alive = 1;
	char buf[2000];
	while(keep_alive){
		memset(buf,0,2000);
		int bytes = tcp_sock_read(tsk, buf, 2000);
		if (bytes < 0) {
			exit(1);
		}
		// deal with request and generate response
		if(buf[0]=='\0')
			break;
		int relative_url;
		char temp_url[50] = {0};
		char http_version[9] = {0};
		char file_path[100] = {0};
		int range = 0;
		int range_begin,range_end;

		int idx = 4;	//only GET
		if(buf[idx]=='/')
			relative_url = 1;
		else
			relative_url = 0;
		int i;
		i=0;
		while(buf[idx]!=' '){
			temp_url[i] = buf[idx];
			i++;
			idx++;
		}
		idx++;
		i=0;
		while(buf[idx]!='\r'){
			http_version[i] = buf[idx];
			i++;
			idx++;
		}
		do{
			idx+=2;
			//end
			if(buf[idx]=='\r')
				break;
			//range
			char temp = buf[idx+6];
			buf[idx+6] = '\0';
			if(strcmp(&buf[idx],"Range:")==0){
				buf[idx+6] = temp;
				range = 1;
				idx+=13;	//Range: bytes=100-200
				range_begin=0;
				while(buf[idx]>='0' && buf[idx]<='9'){
					range_begin = range_begin * 10 + (buf[idx]-'0');
					idx++;
				}
				idx++;
				if(buf[idx]<'0' || buf[idx]>'9'){
					range_end = -1;
				}else{
					range_end = 0;
					while(buf[idx]>='0' && buf[idx]<='9'){
						range_end = range_end * 10 + (buf[idx]-'0');
						idx++;
					}
				}
				continue;
			}
			buf[idx+6] = temp;
			//connection
			temp = buf[idx+11];
			buf[idx+11] = '\0';
			if(strcmp(&buf[idx],"Connection:")==0){
				buf[idx+11] = temp;
				idx +=12;
				if(buf[idx]=='k')
					keep_alive = 1;
				else if(buf[idx]=='c')
					keep_alive = 0;
			}
			buf[idx+11] = temp;
			while(buf[idx]!='\r')
				idx++;
		}while(idx<2000);

		file_path[0] = '.';
		if(relative_url){
			strcat(file_path, temp_url);
		}else{
			i=0;
			int times = 3;
			while(times){
				if(temp_url[i]=='/')
					times--;
				i++;
			}
			i--;
			strcat(file_path, &temp_url[i]);
		}
		FILE *fp = fopen(file_path,"r");
		if(fp==NULL){
			memset(buf,0,2000);
			strcat(buf,http_version);
			strcat(buf, " 404 Not Found\r\n\r\n\r\n\r\n");
			tcp_sock_write(tsk, buf, strlen(buf));
			break;
		}else{
			char header[200] = {0};
			strcat(header,http_version);
			if(range)
				strcat(header, " 206 Partial Content\r\n");
			else
				strcat(header, " 200 OK\r\n");
			int size,begin;
			if(range){
				if(range_end==-1){
					fseek(fp,0L,SEEK_END);
					size = ftell(fp) - range_begin + 1;
					begin = range_begin;
				}else{
					size = range_end - range_begin + 1;
					begin = range_begin;
				}
			}else{
				fseek(fp,0L,SEEK_END);
				size = ftell(fp);
				begin = 0;
			}
			// static  using content-length
			strcat(header, "Content-Length: ");
			fseek(fp,begin,0);
			int temp = size;
			char size_s[64] = {0};
			i=0;
			while(temp){
				size_s[i] = (temp % 10) + '0';
				temp /= 10;
				i++;
			}
			i--;
			for(int j=0;j<=i/2;j++){
				char temp = size_s[j];
				size_s[j] = size_s[i-j];
				size_s[i-j] = temp;
			}
			char response[size + 200];
			memset(response,0,size+200);
			strcat(response,header);
			strcat(response,size_s);
			strcat(response,"\r\nConnection: ");
			if(keep_alive)
				strcat(response, "keep-alive");
			else
				strcat(response, "close");
			strcat(response,"\r\n\r\n");
			fread(&(response[strlen(response)]),1,size,fp);
			tcp_sock_write(tsk, response, strlen(response));
			fclose(fp);
			if(range==1 && range_end==-1)
				break;
		}
	}
	
	tcp_sock_close(tsk);
	return NULL;
}
