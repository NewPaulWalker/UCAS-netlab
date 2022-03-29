#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
int main(){
	int sockfd, n;
	char buffer[1024];
	struct hostent *server;
	struct sockaddr_in serv_addr;

	const char *req = "GET / HTTP/1.0\r\nHost: www.ict.ac.cn\r\n\r\n";

	server = gethostbyname("www.ict.ac.cn");
	serv_addr.sin_family = AF_INET;
	bcopy(server->h_addr, &serv_addr.sin_addr.s_addr, server->h_length);
	serv_addr.sin_port = htons(80);

	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	connect(sockfd,(struct sockaddr *)&serv_addr, sizeof(serv_addr));
	send(sockfd, req, strlen(req), 0);
	while ((n = recv(sockfd, buffer, sizeof(buffer), 0)) > 0)
		fwrite(buffer, 1, n, stdout);

	close(sockfd);
}
