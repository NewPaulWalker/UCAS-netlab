#include "mospf_database.h"
#include "mospf_nbr.h"
#include "ip.h"
#include "rtable.h"
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>

struct list_head mospf_db;

void init_mospf_db()
{
	init_list_head(&mospf_db);
}

void add_net(rt_net_note *pre_net, u32 network){
	rt_net_note *net_node = (rt_net_note*)malloc(sizeof(rt_net_note));
	net_node->nid = network;
	net_node->dist = pre_net==NULL ? 0 : pre_net->dist + 1;
	if(list_empty(&net_list)){
		list_add_tail(&net_node->list, &net_list);
	}else{
		rt_net_note *after;
		list_for_each_entry(after, &net_list, list){
			if(after->dist < net_node->dist){
				continue;
			}else if(after->dist == net_node->dist){
				if(after->nid < net_node->nid){
					continue;
				}else{
					break;
				}
			}else{
				break;
			}
		}
		list_insert(&net_node->list, after->list.prev, &after->list);
	}
}

int net_added(u32 network){
	rt_net_note *net_node;
	list_for_each_entry(net_node, &net_list, list){
		if(net_node->nid == network)
			return 1;
	}
	return 0;
}

void add_route(u32 dest, u32 mask, u32 pre_net){
	u32 gw;
	iface_info_t *iface;
	rt_entry_t *entry;
	list_for_each_entry(entry, &rtable, list){
		if(pre_net == (entry->dest & entry->mask)){
			break;
		}
	}
	if(entry->gw){
		gw = entry->gw;
		iface = entry->iface;
	}else{
		iface_info_t *iface_entry;
		list_for_each_entry(iface_entry, &instance->iface_list, list){
			mospf_nbr_t *nbr;
			list_for_each_entry(nbr, &iface_entry->nbr_list, list){
				if(pre_net == (nbr->nbr_ip & nbr->nbr_mask)){
					gw = nbr->nbr_ip;
					iface = iface_entry;
					break;
				}
			}
			if(&nbr->list != &iface_entry->nbr_list)
				break;
		}
	}
	rt_entry_t *new = new_rt_entry(dest, mask, gw, iface);
	add_rt_entry(new);
}

void print_net_list(){
	rt_net_note *net_node;
	printf("---------------\r\n");
	list_for_each_entry(net_node, &net_list, list){
		printf(IP_FMT"\tdist:%d\r\n",HOST_IP_FMT_STR(net_node->nid), net_node->dist);
	}
	printf("----------------\r\n");
}

void print_db(){
	mospf_db_entry_t * db_entry;
	list_for_each_entry(db_entry, &mospf_db, list) {
		for (int i = 0; i < db_entry->nadv; i++) {
			printf(IP_FMT "\t" IP_FMT "\t" IP_FMT "\t" IP_FMT"\r\n",	HOST_IP_FMT_STR(db_entry->rid), \
												HOST_IP_FMT_STR(db_entry->array[i].network), \
												HOST_IP_FMT_STR(db_entry->array[i].mask), \
												HOST_IP_FMT_STR(db_entry->array[i].rid));
		}
	}
	fflush(stdout);
}

void update_rtable(){
	pthread_mutex_lock(&rtable_lock);
	//clear learned entry & init net list
	clear_rtable();
	load_rtable_from_kernel();
	rt_entry_t * rt_entry;
	list_for_each_entry(rt_entry, &rtable, list){
		add_net(NULL, rt_entry->dest & rt_entry->mask);
	}
	//generate new entry
	rt_net_note *net_node, *net_q;
	list_for_each_entry(net_node, &net_list, list){
		mospf_db_entry_t *db_entry;
		list_for_each_entry(db_entry, &mospf_db, list){
			int i,j;
			for(i=0;i<db_entry->nadv;i++){
				if(net_node->nid == db_entry->array[i].network && db_entry->array[i].rid!=0){
					break;
				}
			}
			if(i==db_entry->nadv)
				continue;
			for(j=0;j<db_entry->nadv;j++){
				if(j!=i){
					if(!net_added(db_entry->array[j].network)){
						add_net(net_node, db_entry->array[j].network);
						add_route(db_entry->array[j].network, db_entry->array[j].mask, net_node->nid);
					}
				}
			}
		}
	}
	list_for_each_entry_safe(net_node, net_q, &net_list, list){
		list_delete_entry(&net_node->list);
		free(net_node);
	}
	pthread_mutex_unlock(&rtable_lock);
}
