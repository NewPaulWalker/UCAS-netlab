#include "nat.h"
#include "ip.h"
#include "icmp.h"
#include "tcp.h"
#include "rtable.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static struct nat_table nat;

// get the interface from iface name
static iface_info_t *if_name_to_iface(const char *if_name)
{
	iface_info_t *iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		if (strncmp(iface->name, if_name, strlen(iface->name)) == 0)
			return iface;
	}

	log(ERROR, "Could not find the desired interface according to if_name '%s'", if_name);
	return NULL;
}

// determine the direction of the packet, DIR_IN / DIR_OUT / DIR_INVALID
static int get_packet_direction(char *packet)
{
	//fprintf(stdout, "TODO: determine the direction of this packet.\n");
	struct iphdr *iph = packet_to_ip_hdr(packet);
	u32 saddr = ntohl(iph->saddr);
	u32 daddr = ntohl(iph->daddr);
	rt_entry_t *rt_entry;
	rt_entry = longest_prefix_match(saddr);
	if(rt_entry->iface == nat.internal_iface){
		rt_entry = longest_prefix_match(daddr);
		if(rt_entry->iface == nat.external_iface){
			return DIR_OUT;
		}
	}else if(rt_entry->iface == nat.external_iface && daddr == nat.external_iface->ip){
		return DIR_IN;
	}
	return DIR_INVALID;
}

// do translation for the packet: replace the ip/port, recalculate ip & tcp
// checksum, update the statistics of the tcp connection
void do_translation(iface_info_t *iface, char *packet, int len, int dir)
{
	//fprintf(stdout, "TODO: do translation for this packet.\n");
	pthread_mutex_lock(&nat.lock);
	struct iphdr *iph = packet_to_ip_hdr(packet);
	struct tcphdr *tcph = packet_to_tcp_hdr(packet);
	u32 remote_ip;
	u16 remote_port;
	u32 external_ip;
	u16 external_port;
	u32 internal_ip;
	u16 internal_port;
	u8 key;
	if(dir==DIR_IN){
		remote_ip = ntohl(iph->saddr);
		remote_port = ntohs(tcph->sport);
		external_ip = ntohl(iph->daddr);
		external_port = ntohs(tcph->dport);
		key = hash8((char*)&iph->saddr, 4) ^ hash8((char*)&tcph->sport, 2);
	}else{
		remote_ip = ntohl(iph->daddr);
		remote_port = ntohs(tcph->dport);
		internal_ip = ntohl(iph->saddr);
		internal_port = ntohs(tcph->sport);
		key = hash8((char*)&iph->daddr, 4) ^ hash8((char*)&tcph->dport, 2);
	}
	struct nat_mapping *mapping;
	if(dir==DIR_IN){
		list_for_each_entry(mapping, &nat.nat_mapping_list[key], list){
			if( mapping->remote_ip == remote_ip && \
				mapping->remote_port == remote_port && \
				mapping->external_ip == external_ip && \
				mapping->external_port ==external_port){
					break;
				}
		}
	}else{
		list_for_each_entry(mapping, &nat.nat_mapping_list[key], list){
			if( mapping->remote_ip == remote_ip && \
				mapping->remote_port == remote_port && \
				mapping->internal_ip == internal_ip && \
				mapping->internal_port ==internal_port){
					break;
				}
		}
	}
	if(&mapping->list == &nat.nat_mapping_list[key]){
		mapping = (struct nat_mapping*)malloc(sizeof(struct nat_mapping));
		//ip & port mapping
		mapping->remote_ip = remote_ip;
		mapping->remote_port = remote_port;
		if(dir==DIR_IN){
			mapping->external_ip = external_ip;
			mapping->external_port = external_port;
			//find fules
			struct dnat_rule *rule;
			list_for_each_entry(rule, &nat.rules, list){
				if(rule->external_ip == external_ip && rule->external_port == external_port){
					mapping->internal_ip = rule->internal_ip;
					mapping->internal_port = rule->internal_port;
					break;
				}
			}
			if(&rule->list == &nat.rules){
				printf("no rules exist!\r\n");
				free(mapping);
				icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH);
				free(packet);
				return ;
			}
		}else{
			mapping->internal_ip = internal_ip;
			mapping->internal_port = internal_port;
			mapping->external_ip = nat.external_iface->ip;
			//assign port
			int i;
			for(i=NAT_PORT_MIN;i<=NAT_PORT_MAX;i++){
				if(nat.assigned_ports[i]==0){
					nat.assigned_ports[i]=1;
					mapping->external_port = i;
					break;
				}
			}
			if(i>NAT_PORT_MAX){
				printf("donot have rest port !\r\n");
				free(mapping);
				icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH);
				free(packet);
				return ;
			}
		}
		list_add_tail(&mapping->list, &nat.nat_mapping_list[key]);
		//init connection
		mapping->conn.internal_fin = 0;
		mapping->conn.external_fin = 0;
		mapping->conn.internal_seq_end = 0;
		mapping->conn.external_seq_end = 0;
		mapping->conn.internal_ack = 0;
		mapping->conn.external_ack = 0;
	}
	//translation
	if(dir==DIR_IN){
		iph->daddr = htonl(mapping->internal_ip);
		tcph->dport = htons(mapping->internal_port);
	}else{
		iph->saddr = htonl(mapping->external_ip);
		tcph->sport = htons(mapping->external_port);
	}
	//update connection
	mapping->update_time = 0;
	u32 seq = ntohl(tcph->seq);
	u32 ack = ntohl(tcph->ack);
	if(dir==DIR_IN){
		mapping->conn.external_fin = TCP_FIN & tcph->flags;
		if(seq > mapping->conn.external_seq_end)
			mapping->conn.external_seq_end = seq;
		if(ack > mapping->conn.external_ack)
			mapping->conn.external_ack = ack;
	}else{
		mapping->conn.internal_fin = TCP_FIN & tcph->flags;
		if(seq > mapping->conn.internal_seq_end)
			mapping->conn.internal_seq_end = seq;
		if(ack > mapping->conn.internal_ack)
			mapping->conn.internal_ack = ack;
	}
	pthread_mutex_unlock(&nat.lock);
	//update checksum
	tcph->checksum = tcp_checksum(iph, tcph);
	iph->checksum = ip_checksum(iph);
	//forward == ip_forward_packet
	//ttl-1
	iph->ttl --;
	if(iph->ttl<=0){
		icmp_send_packet(packet, len, ICMP_TIME_EXCEEDED, ICMP_EXC_TTL);
		free(packet);
		return ;
	}
	//checksum
	iph->checksum = ip_checksum(iph);
	//lookup rtable
	u32 daddr = ntohl(iph->daddr);
	rt_entry_t *match = longest_prefix_match(daddr);
	if(match==NULL){
		icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_NET_UNREACH);
		free(packet);
		return ;
	}
	//get next ip addr
	u32 next_ip = match->gw ? match->gw : daddr;
	//forward
	iface_send_packet_by_arp(match->iface, next_ip, packet, len);
}

void nat_translate_packet(iface_info_t *iface, char *packet, int len)
{
	int dir = get_packet_direction(packet);
	if (dir == DIR_INVALID) {
		log(ERROR, "invalid packet direction, drop it.");
		icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH);
		free(packet);
		return ;
	}

	struct iphdr *ip = packet_to_ip_hdr(packet);
	if (ip->protocol != IPPROTO_TCP) {
		log(ERROR, "received non-TCP packet (0x%0hhx), drop it", ip->protocol);
		free(packet);
		return ;
	}

	do_translation(iface, packet, len, dir);
}

// check whether the flow is finished according to FIN bit and sequence number
// XXX: seq_end is calculated by `tcp_seq_end` in tcp.h
static int is_flow_finished(struct nat_connection *conn)
{
    return (conn->internal_fin && conn->external_fin) && \
            (conn->internal_ack >= conn->external_seq_end) && \
            (conn->external_ack >= conn->internal_seq_end);
}

// nat timeout thread: find the finished flows, remove them and free port
// resource
void *nat_timeout()
{
	while (1) {
		//fprintf(stdout, "TODO: sweep finished flows periodically.\n");
		sleep(1);
		pthread_mutex_lock(&nat.lock);
		struct nat_mapping *mapping, *q;
		for(int i=0;i<HASH_8BITS;i++){
			list_for_each_entry_safe(mapping, q, &nat.nat_mapping_list[i], list){
				mapping->update_time++;
				if(mapping->update_time >= TCP_ESTABLISHED_TIMEOUT || is_flow_finished(&mapping->conn)){
					list_delete_entry(&mapping->list);
					nat.assigned_ports[mapping->external_port] = 0;
					free(mapping);
				}
			}
		}
		pthread_mutex_unlock(&nat.lock);
	}

	return NULL;
}

int parse_config(const char *filename)
{
	//fprintf(stdout, "TODO: parse config file, including i-iface, e-iface (and dnat-rules if existing).\n");
	FILE *fp = fopen(filename, "r");
	if(fp){
		char buf[1000] = {0};
		fseek(fp,0L,SEEK_END);
		int size = ftell(fp);
		fseek(fp, 0, 0);
		fread(buf, 1, size, fp);
		//get internal and external iface
		char *internal = strstr(buf, "internal-iface: ");
		nat.internal_iface = if_name_to_iface(internal + 16);
		char *external = strstr(buf, "external-iface: ");
		nat.external_iface = if_name_to_iface(external + 16);
		//get dnat-rules
		char *dnat = buf;
		do{
			dnat = dnat + 1;
			dnat = strstr(dnat, "dnat-rules: ");
			//get rules
			if(dnat){
				struct dnat_rule *rule = (struct dnat_rule*)malloc(sizeof(struct dnat_rule));
				list_add_tail(&rule->list, &nat.rules);
				int num;
				int ip;
				int port;
				char *external_ip = dnat + 12;
				ip = 0;
				for(int i=0;i<4;i++){
					ip = ip << 8;
					num = 0;
					while(*external_ip!='.' && *external_ip!=':'){
						num = num * 10 + *external_ip - '0';
						external_ip ++;
					}
					external_ip++;
					ip |= num;
				}
				rule->external_ip = ip;
				port=0;
				while(*external_ip!=' '){
					port = port * 10 + *external_ip - '0';
					external_ip++;
				}
				rule->external_port = port;
				char *internal_ip = strstr(dnat, "-> ");
				internal_ip = internal_ip + 3;
				ip = 0;
				for(int i=0;i<4;i++){
					ip = ip << 8;
					num = 0;
					while(*internal_ip!='.' && *internal_ip!=':'){
						num = num * 10 + *internal_ip - '0';
						internal_ip ++;
					}
					internal_ip++;
					ip |= num;
				}
				rule->internal_ip = ip;
				port=0;
				while(*internal_ip >= '0' && *internal_ip <= '9'){
					port = port * 10 + *internal_ip - '0';
					internal_ip++;
				}
				rule->internal_port = port;
			}
		}while(dnat);
		fclose(fp);
	}else{
		printf("config file not exist!\n");
		exit(1);
	}
	return 0;
}

// initialize
void nat_init(const char *config_file)
{
	memset(&nat, 0, sizeof(nat));

	for (int i = 0; i < HASH_8BITS; i++)
		init_list_head(&nat.nat_mapping_list[i]);

	init_list_head(&nat.rules);

	// seems unnecessary
	memset(nat.assigned_ports, 0, sizeof(nat.assigned_ports));

	parse_config(config_file);

	pthread_mutex_init(&nat.lock, NULL);

	pthread_create(&nat.thread, NULL, nat_timeout, NULL);
}

void nat_exit()
{
	//fprintf(stdout, "TODO: release all resources allocated.\n");
	pthread_mutex_lock(&nat.lock);
	pthread_kill(nat.thread, SIGKILL);
	pthread_mutex_unlock(&nat.lock);
	pthread_mutex_destroy(&nat.lock);
	struct nat_mapping *mapping, *q;
	for(int i=0;i<HASH_8BITS;i++){
		list_for_each_entry_safe(mapping, q, &nat.nat_mapping_list[i], list){
			list_delete_entry(&mapping->list);
			free(mapping);
		}
	}
	struct dnat_rule *rule, *q1;
	list_for_each_entry_safe(rule, q1, &nat.rules, list){
		list_delete_entry(&rule->list);
		free(rule);
	}
}
