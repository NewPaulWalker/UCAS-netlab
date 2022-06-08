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

	char *prefix = "server echoes: ";
	int len = strlen(prefix);
	char recv[100] = {0};
	char send[100] = {0};
	while(1){
		int rlen = tcp_sock_read(csk, recv, 100);
		if(rlen==0){
			log(DEBUG, "transmission finish.");
			break;		
		}else if(rlen > 0){
			memcpy(send, prefix, len);
			memcpy(send + len, recv, rlen);
			log(DEBUG, "%s.",send);
			int err = tcp_sock_write(csk, send, strlen(send));
			if(err < 0){
				log(DEBUG, "send failed.");
				break;
			}
		}else{
			log(DEBUG, "recv failed.");
			break;
		}
	}

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

	char *data = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
	int len = strlen(data);
	char send[100] = {0};
	char recv[100] = {0};
	for(int i = 0;i<10;i++){
		memcpy(send, data + i, len - i);
		memcpy(send + len - i, data, i);
		int err = tcp_sock_write(tsk, send, strlen(send));
		if(err < 0){
			log(DEBUG, "send failed.");
			break;
		}
		int rlen = tcp_sock_read(tsk, recv, 100);
		if(rlen==0){
			log(DEBUG, "transmission finish.");
			break;
		}else if(rlen > 0){
			log(DEBUG, "%s.", recv);
		}else{
			log(DEBUG, "recv failed.");
			break;
		}
		sleep(1);
	}

	tcp_sock_close(tsk);

	log(DEBUG, "client finish.");

	return NULL;
}
