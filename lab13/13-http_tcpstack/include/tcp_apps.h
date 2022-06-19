#ifndef __TCP_APPS_H__
#define __TCP_APPS_H__


void *tcp_server(void *arg);
void *tcp_client(void *arg);
void *handle_http_request(void *arg);
#endif
