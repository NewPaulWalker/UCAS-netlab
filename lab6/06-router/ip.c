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

	struct ether_header *eh = (struct ether_header *)packet;
	struct iphdr *iph =  packet_to_ip_hdr(packet);
	struct icmphdr *icmph = IP_DATA(iph);

	u32 daddr = ntohl(iph->daddr);
	u8 protocol = iph->protocol;
	u8 type = icmph->type;

	if((daddr==iface->ip) && (protocol==IPPROTO_ICMP) && (type==ICMP_ECHOREQUEST)){
		//send ICMP echo reply
		icmp_send_packet(packet, len, ICMP_ECHOREPLY, 0);
		return ;
	}

	//forward the packet

	//ttl-1
	iph->ttl --;
	if(iph->ttl<=0){
		icmp_send_packet(packet, len, ICMP_TIME_EXCEEDED, ICMP_EXC_TTL);
		return ;
	}
	//checksum
	iph->checksum = ip_checksum(iph);
	//lookup rtable
	rt_entry_t *match = longest_prefix_match(daddr);
	if(match==NULL){
		icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_NET_UNREACH);
		return ;
	}
	//get next ip addr
	u32 next_ip = match->gw ? match->gw : daddr;
	//forward
	iface_send_packet_by_arp(match->iface, next_ip, packet, len);
}
