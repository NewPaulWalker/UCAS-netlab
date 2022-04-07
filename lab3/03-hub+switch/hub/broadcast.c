#include "base.h"
#include <stdio.h>

extern ustack_t *instance;

// the memory of ``packet'' will be free'd in handle_packet().
void broadcast_packet(iface_info_t *iface, const char *packet, int len)
{
	// TODO: broadcast packet 
	//fprintf(stdout, "TODO: broadcast packet.\n");
	iface_info_t *entry;
	list_for_each_entry(entry, &(instance->iface_list), list) {
		if(entry->fd != iface->fd)
			iface_send_packet(entry, packet, len);
	}
}
