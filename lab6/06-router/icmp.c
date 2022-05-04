#include "icmp.h"
#include "ip.h"
#include "rtable.h"
#include "arp.h"
#include "base.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// send icmp packet
void icmp_send_packet(const char *in_pkt, int len, u8 type, u8 code)
{
	fprintf(stderr, "TODO: malloc and send icmp packet.\n");

	struct iphdr *iph = packet_to_ip_hdr(in_pkt);
	char *ipdata = IP_DATA(iph);

	char *res;
	int res_len = 0, icmp_len = 0;

	//	length
	if (type == ICMP_ECHOREPLY) {
		log(DEBUG, "ICMP_ECHOREPLY.");
		icmp_len = ntohs(iph->tot_len) - IP_HDR_SIZE(iph);
		res_len = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + icmp_len;
	} else if (type == ICMP_DEST_UNREACH || type == ICMP_TIME_EXCEEDED) {
		log(DEBUG, "ICMP_DEST_UNREACH || ICMP_TIME_EXCEEDED.");
		icmp_len = ICMP_HDR_SIZE + IP_HDR_SIZE(iph) + ICMP_COPIED_DATA_LEN;
		res_len = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + icmp_len;
	}
	log(DEBUG, "icmp_len = %d, res_len = %d", icmp_len, res_len);
	res = malloc(res_len);
	memset(res, 0, res_len);

	// init res_iph
	struct iphdr *res_iph = packet_to_ip_hdr(res);
	if (type == ICMP_ECHOREPLY) {
		ip_init_hdr(res_iph, ntohl(iph->daddr), ntohl(iph->saddr), 
				IP_BASE_HDR_SIZE + icmp_len, IPPROTO_ICMP);
	} else if (type == ICMP_DEST_UNREACH || type == ICMP_TIME_EXCEEDED) {
		rt_entry_t *match = longest_prefix_match(ntohl(iph->saddr));
		if (!match) {
			free(res);
			return;
		}
		ip_init_hdr(res_iph, match->iface->ip, ntohl(iph->saddr), 
				IP_BASE_HDR_SIZE + icmp_len, IPPROTO_ICMP);
	}

	// init icmp
	char * res_ipdata = IP_DATA(res_iph);
	struct icmphdr * res_icmph = (void *)res_ipdata;
	if (type == ICMP_ECHOREPLY) {
		memcpy(res_ipdata, ipdata, icmp_len);
	} else if (type == ICMP_DEST_UNREACH || type == ICMP_TIME_EXCEEDED) {
		res_icmph->icmp_identifier = 0;
		res_icmph->icmp_sequence = 0;
		memcpy(res_ipdata + ICMP_HDR_SIZE, iph, icmp_len - ICMP_HDR_SIZE);
	}
	res_icmph->type = type;
	res_icmph->code = code;
	res_icmph->checksum = icmp_checksum(res_icmph, icmp_len);
	
	//	send
	ip_send_packet(res, res_len);
}

void handle_icmp_packet(char *packet, int len) {
	struct iphdr * ip_hdr = packet_to_ip_hdr(packet);
	struct icmphdr * icmp_hdr = (void *)IP_DATA(ip_hdr);
	if (icmp_hdr->type == ICMP_ECHOREQUEST) {
		icmp_send_packet(packet, len, ICMP_ECHOREPLY, 0);
	}
	free(packet);
	return ;
}
