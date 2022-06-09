#include "tcp_sock.h"

#include "log.h"

#include <unistd.h>

// tcp server application, listens to port (specified by arg) and serves only one
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

	if (tcp_sock_listen(tsk, 3) < 0) {
		log(ERROR, "tcp_sock listen failed");
		exit(1);
	}

	log(DEBUG, "listen to port %hu.", ntohs(port));

	struct tcp_sock *csk = tcp_sock_accept(tsk);

	log(DEBUG, "accept a connection.");

	FILE *fd = fopen("server-output.dat", "wb");
	if(fd==NULL){
		log(ERROR, "open file failed.");
	}

	char buf[1024];
	int len = 0;
	int recv = 0;
	while(1){
		len = tcp_sock_read(csk, buf, 1000);
		if(len){
			fwrite(buf, 1, len, fd);
			recv += len;
			log(DEBUG, "recv %d Bytes.",recv);
		}else{
			log(DEBUG, "write done.");
			break;
		}
	}

	fclose(fd);

	tcp_sock_close(csk);

	log(DEBUG, "server finish.");
	
	return NULL;
}

// tcp client application, connects to server (ip:port specified by arg), each
// time sends one bulk of data and receives one bulk of data 
void *tcp_client(void *arg)
{
	struct sock_addr *skaddr = arg;

	struct tcp_sock *tsk = alloc_tcp_sock();

	if (tcp_sock_connect(tsk, skaddr) < 0) {
		log(ERROR, "tcp_sock connect to server ("IP_FMT":%hu)failed.", \
				NET_IP_FMT_STR(skaddr->ip), ntohs(skaddr->port));
		exit(1);
	}

	log(DEBUG, "connect success.");

	FILE *fd = fopen("client-input.dat", "rb");
	if(fd==NULL){
		log(ERROR, "open file failed.");
	}

	char buf[1024] = {0};
	int len = 0;
	int send = 0;
	while(1){
		len = fread(buf, 1, 1000, fd);
		if(len){
			send += len;
			tcp_sock_write(tsk, buf, len);
			log(DEBUG, "send %d Bytes.",send);
		}else{
			log(DEBUG, "file end.");
			break;
		}
	}

	fclose(fd);

	tcp_sock_close(tsk);

	log(DEBUG, "client finish.");

	return NULL;
}
