#include <errno.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <resolv.h>
#include <pthread.h>
#include "openssl/ssl.h"
#include "openssl/err.h"

void *listen_http_request(void *arg);
void *listen_https_request(void *arg);
void *handle_http_request(void *arg);
void *handle_https_request(void *arg);

int main(){
    //two threads
    //one for http, and another/main for https
	pthread_t http_thread;
	int status = pthread_create(&http_thread,NULL,(void *)listen_http_request,NULL);
	if (status!=0){
		perror("Create http thread failed");
		exit(1);
	}
	listen_https_request(NULL);
	return 0;
}

void *listen_http_request(void *arg){
	// init socket, listening to port 80
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("Opening socket failed");
		exit(1);
	}
	int enable = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
		perror("setsockopt(SO_REUSEADDR) failed");
		exit(1);
	}

	struct sockaddr_in addr;
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(80);

	if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		perror("Bind failed");
		exit(1);
	}
	listen(sock, 10);

    while(1) {
		struct sockaddr_in caddr;
		socklen_t len;
		int csock = accept(sock, (struct sockaddr*)&caddr, &len);
		if (csock < 0) {
			perror("Accept failed");
			exit(1);
		}
        // create a new thread to exec handle_https_request(csock);
		// use PTHREAD_CREATE_DETACHED to recycling resources when thread end
		pthread_t new_thread;
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		int status = pthread_create(&new_thread,&attr,(void *)handle_http_request,(void *)(long)csock);
		if (status!=0){
			perror("Create handle_http_request thread failed");
			exit(1);
		}
    }
	close(sock);
	return NULL;
}
void *listen_https_request(void *arg){
    // init SSL Library
	SSL_library_init();
	OpenSSL_add_all_algorithms();
	SSL_load_error_strings();

	// enable TLS method
	const SSL_METHOD *method = TLS_server_method();
	SSL_CTX *ctx = SSL_CTX_new(method);

	// load certificate and private key
	if (SSL_CTX_use_certificate_file(ctx, "./keys/cnlab.cert", SSL_FILETYPE_PEM) <= 0) {
		perror("load cert failed");
		exit(1);
	}
	if (SSL_CTX_use_PrivateKey_file(ctx, "./keys/cnlab.prikey", SSL_FILETYPE_PEM) <= 0) {
		perror("load prikey failed");
		exit(1);
	}

	// init socket, listening to port 443
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("Opening socket failed");
		exit(1);
	}
	int enable = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
		perror("setsockopt(SO_REUSEADDR) failed");
		exit(1);
	}

	struct sockaddr_in addr;
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(443);

	if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		perror("Bind failed");
		exit(1);
	}
	listen(sock, 10);

    while(1) {
		struct sockaddr_in caddr;
		socklen_t len;
		int csock = accept(sock, (struct sockaddr*)&caddr, &len);
		if (csock < 0) {
			perror("Accept failed");
			exit(1);
		}
		SSL *ssl = SSL_new(ctx); 
		SSL_set_fd(ssl, csock);
        // create a new thread to exec handle_https_request(ssl);
		// use PTHREAD_CREATE_DETACHED to recycling resources when thread end
		pthread_t new_thread;
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		int status = pthread_create(&new_thread,&attr,(void *)handle_https_request,(void *)ssl);
		if (status!=0){
			perror("Create handle_http_request thread failed");
			exit(1);
		}   
	}

	close(sock);
	SSL_CTX_free(ctx);
	return NULL;
}
void *handle_http_request(void *arg){
	long csock = (long)arg;
	char buf[1024] = {0};
	int bytes = recv((int)csock,buf,sizeof(buf),0);
	if (bytes < 0) {
		perror("recv failed");
		exit(1);
	}
	// deal with request and generate response
	int relative_url;
	char temp_url[50] = {0};
	char http_version[9] = {0};
	char host[100] = {0};


	int idx = 4;	//because only GET
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
		idx++;
		i++;
	}
	if(relative_url){
		do{
			idx+=2;
			char temp = buf[idx+5];
			buf[idx+5] = '\0';
			if(strcmp(&buf[idx],"Host:")!=0){
				buf[idx+5] = temp;
				while(buf[idx]!='\r')
					idx++;
				continue;
			}else{
				buf[idx+5] = temp;
				idx+=6;
				i=0;
				while(buf[idx]!='\r'){
					host[i] = buf[idx];
					idx++;
					i++;
				}
				break;
			}
		}while(idx<1024);
	}

	memset(buf,0,1024);
	strcat(buf,http_version);
	strcat(buf," 301 Moved Permanently\r\nLocation: ");
	strcat(buf,"https://");
	if(relative_url){
		strcat(buf,host);
		strcat(buf,temp_url);
	}else{
		strcat(buf,&temp_url[7]);
	}
	strcat(buf,"\r\n\r\n\r\n\r\n");
	send(csock,buf,strlen(buf),0);
	close(csock);
    return NULL;
}
void *handle_https_request(void *arg){
	SSL* ssl = (SSL*)arg;
	if (SSL_accept(ssl) == -1){
		perror("SSL_accept failed");
		exit(1);
	}else{
		int keep_alive = 1;
		char buf[1024];
		while(keep_alive){
			memset(buf,0,1024);
			int bytes = SSL_read(ssl,buf,sizeof(buf));
			if (bytes < 0) {
				perror("SSL_read failed");
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
			}while(idx<1024);

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
				memset(buf,0,1024);
				strcat(buf,http_version);
				strcat(buf, " 404 Not Found\r\n\r\n\r\n\r\n");
				SSL_write(ssl,buf,strlen(buf));
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
				SSL_write(ssl,response,strlen(response));

				fclose(fp);

				if(range==1 && range_end==-1)
					break;
			}
		}
	}
	int sock = SSL_get_fd(ssl);
	SSL_free(ssl);
	close(sock);
	return NULL;
}
