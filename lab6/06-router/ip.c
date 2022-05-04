#include "ip.h"
#include "icmp.h"
#include "rtable.h"
#include "arp.h"
#include <stdio.h>
#include <stdlib.h>

// handle ip packet
//
// If the packet is ICMP echo request and the destination IP address is equal to
// the IP address of the iface, send ICMP echo reply; otherwise, forward the
// packet.
void handle_ip_packet(iface_info_t *iface, char *packet, int len)
{
	//fprintf(stderr, "TODO: handle ip packet.\n");

	struct iphdr *iph = packet_to_ip_hdr(packet);
	u32 dst_ip = ntohl(iph->daddr);

	//	send ICMP echo reply
	if(dst_ip == iface->ip){
		if(iph->protocol == IPPROTO_ICMP){
			handle_icmp_packet(packet, len);
		}else{
			free(packet);
		}
		return ;
	}

	//	TTL
	iph->ttl --;
	if(iph->ttl <=0){
		icmp_send_packet(packet, len, ICMP_TIME_EXCEEDED, ICMP_EXC_TTL);
		free(packet);
		return ;
	}

	//	checksum
	iph->checksum = ip_checksum(iph);

	//	search
	rt_entry_t *match = longest_prefix_match(dst_ip);
	if(!match){
		icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_NET_UNREACH);
		free(packet);
		return ;
	}

	//	next ip
	u32 next_ip;
	if(match->gw == 0)
		next_ip = dst_ip;
	else
		next_ip = match->gw;

	//	check dest ip
	if(dst_ip == match->iface->ip){
		if(iph->protocol == IPPROTO_ICMP){
			handle_icmp_packet(packet, len);
		}else{
			free(packet);
		}
		return ;
	}

	//	forward
	iface_send_packet_by_arp(match->iface, next_ip, packet, len);

	return ;
}
