#include "mac.h"
#include "log.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

mac_port_map_t mac_port_map;

// initialize mac_port table
void init_mac_port_table()
{
	bzero(&mac_port_map, sizeof(mac_port_map_t));

	for (int i = 0; i < HASH_8BITS; i++) {
		init_list_head(&mac_port_map.hash_table[i]);
	}

	pthread_mutex_init(&mac_port_map.lock, NULL);

	pthread_create(&mac_port_map.thread, NULL, sweeping_mac_port_thread, NULL);
}

// destroy mac_port table
void destory_mac_port_table()
{
	pthread_mutex_lock(&mac_port_map.lock);
	mac_port_entry_t *entry, *q;
	for (int i = 0; i < HASH_8BITS; i++) {
		list_for_each_entry_safe(entry, q, &mac_port_map.hash_table[i], list) {
			list_delete_entry(&entry->list);
			free(entry);
		}
	}
	pthread_mutex_unlock(&mac_port_map.lock);
}

// lookup the mac address in mac_port table
iface_info_t *lookup_port(u8 mac[ETH_ALEN])
{
	// TODO: implement the lookup process here
	//fprintf(stdout, "TODO: implement the lookup process here.\n");
	u8 u8_idx = hash8((char*)mac, ETH_ALEN);
	int idx = (int)u8_idx;
	mac_port_entry_t *entry;
	pthread_mutex_lock(&mac_port_map.lock);
	list_for_each_entry(entry, &(mac_port_map.hash_table[idx]), list){
		int i;
		for(i=0;i<ETH_ALEN;i++){
			if(entry->mac[i]!=mac[i])
				break;
		}
		if(i==ETH_ALEN){
			iface_info_t *temp = entry->iface;
			pthread_mutex_unlock(&mac_port_map.lock);
			return temp;
		}
	}
	pthread_mutex_unlock(&mac_port_map.lock);
	return NULL;
}

// insert the mac -> iface mapping into mac_port table
void insert_mac_port(u8 mac[ETH_ALEN], iface_info_t *iface)
{
	// TODO: implement the insertion process here
	//fprintf(stdout, "TODO: implement the insertion process here.\n");

	u8 u8_idx = hash8((char*)mac, ETH_ALEN);
	int idx = (int)u8_idx;
	mac_port_entry_t *entry;
	pthread_mutex_lock(&(mac_port_map.lock));
	list_for_each_entry(entry, &(mac_port_map.hash_table[idx]), list){
		int i;
		for(i=0;i<ETH_ALEN;i++){
			if(mac[i]!=entry->mac[i])
				break;
		}
		if(i==ETH_ALEN){
			if(entry->iface!=iface)
				entry->iface = iface;
			entry->visited = MAC_PORT_TIMEOUT;
			pthread_mutex_unlock(&(mac_port_map.lock));
			return ;
		}
	}
	mac_port_entry_t *new = malloc(sizeof(mac_port_entry_t));
	new->iface = iface;
	new->visited = MAC_PORT_TIMEOUT;
	for(int i=0;i<ETH_ALEN;i++)
		new->mac[i] = mac[i];
	list_add_head(&new->list, &(mac_port_map.hash_table[idx]));
	pthread_mutex_unlock(&(mac_port_map.lock));
	return ;
}

// dumping mac_port table
void dump_mac_port_table()
{
	mac_port_entry_t *entry = NULL;
	time_t now = time(NULL);

	fprintf(stdout, "dumping the mac_port table:\n");
	pthread_mutex_lock(&mac_port_map.lock);
	for (int i = 0; i < HASH_8BITS; i++) {
		list_for_each_entry(entry, &mac_port_map.hash_table[i], list) {
			fprintf(stdout, ETHER_STRING " -> %s, %d\n", ETHER_FMT(entry->mac), \
					entry->iface->name, (int)(now - entry->visited));
		}
	}

	pthread_mutex_unlock(&mac_port_map.lock);
}

// sweeping mac_port table, remove the entry which has not been visited in the
// last 30 seconds.
int sweep_aged_mac_port_entry()
{
	// TODO: implement the sweeping process here
	//fprintf(stdout, "TODO: implement the sweeping process here.\n");
	int n = 0;
	mac_port_entry_t *entry, *q;
	pthread_mutex_lock(&mac_port_map.lock);
	for(int i=0;i<HASH_8BITS;i++){
		list_for_each_entry_safe(entry, q, &mac_port_map.hash_table[i], list){
			entry->visited --;
			if(entry->visited==0){
				list_delete_entry(&entry->list);
				free(entry);
				n++;
			}
		}
	}
	pthread_mutex_unlock(&mac_port_map.lock);
	return n;
}

// sweeping mac_port table periodically, by calling sweep_aged_mac_port_entry
void *sweeping_mac_port_thread(void *nil)
{
	while (1) {
		sleep(1);
		int n = sweep_aged_mac_port_entry();

		if (n > 0)
			log(DEBUG, "%d aged entries in mac_port table are removed.", n);
	}

	return NULL;
}
