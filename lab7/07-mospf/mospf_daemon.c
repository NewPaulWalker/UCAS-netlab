#include "mospf_daemon.h"
#include "mospf_proto.h"
#include "mospf_nbr.h"
#include "mospf_database.h"

#include "ip.h"

#include "list.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

extern ustack_t *instance;

pthread_mutex_t mospf_lock;

void mospf_init()
{
	pthread_mutex_init(&mospf_lock, NULL);

	instance->area_id = 0;
	// get the ip address of the first interface
	iface_info_t *iface = list_entry(instance->iface_list.next, iface_info_t, list);
	instance->router_id = iface->ip;
	instance->sequence_num = 0;
	instance->lsuint = 0;

	iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		iface->helloint = MOSPF_DEFAULT_HELLOINT;
		iface->num_nbr = 0;
		init_list_head(&iface->nbr_list);
	}

	init_mospf_db();
}

void *sending_mospf_hello_thread(void *param);
void *sending_mospf_lsu_thread(void *param);
void *checking_nbr_thread(void *param);
void *checking_database_thread(void *param);
void *print_db_thread(void *param);
void *print_nbr_thread(void *param);

void mospf_run()
{
	pthread_t hello, lsu, nbr, db, print_db, print_nbr;
	pthread_create(&hello, NULL, sending_mospf_hello_thread, NULL);
	pthread_create(&lsu, NULL, sending_mospf_lsu_thread, NULL);
	pthread_create(&nbr, NULL, checking_nbr_thread, NULL);
	pthread_create(&db, NULL, checking_database_thread, NULL);
	pthread_create(&print_db, NULL, print_db_thread, NULL);
	pthread_create(&print_nbr, NULL, print_nbr_thread, NULL);
}

void *print_nbr_thread(void *param){
	sleep(50);
	pthread_mutex_lock(&mospf_lock);
	iface_info_t *iface;
	list_for_each_entry(iface, &instance->iface_list, list){
		mospf_nbr_t *nbr;
		list_for_each_entry(nbr, &iface->nbr_list, list){
			printf(IP_FMT "\t" IP_FMT "\t" IP_FMT "\t" IP_FMT "\r\n",\
							HOST_IP_FMT_STR(iface->ip),\
							HOST_IP_FMT_STR(nbr->nbr_id),\
							HOST_IP_FMT_STR(nbr->nbr_ip),\
							HOST_IP_FMT_STR(nbr->nbr_mask));
		}
	}
	fflush(stdout);
	pthread_mutex_unlock(&mospf_lock);
	return NULL;
}

void *print_db_thread(void *param){
	sleep(60);
	pthread_mutex_lock(&mospf_lock);
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
	pthread_mutex_unlock(&mospf_lock);
	return NULL;
}

void *sending_mospf_hello_thread(void *param)
{
	//fprintf(stdout, "TODO: send mOSPF Hello message periodically.\n");
	while(1){
		sleep(MOSPF_DEFAULT_HELLOINT);
		iface_info_t *entry;
		list_for_each_entry(entry, &instance->iface_list, list){
			//malloc
			char *packet = (char*)malloc(ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE);
			//hello
			struct mospf_hello *hello = (struct mospf_hello*)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE);
			mospf_init_hello(hello, entry->mask);
			//mospf header
			struct mospf_hdr *mospfh = (struct mospf_hdr*)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE);
			mospf_init_hdr(mospfh, MOSPF_TYPE_HELLO, MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE, instance->router_id, instance->area_id);
			mospfh->checksum = mospf_checksum(mospfh);
			//ip header
			struct iphdr *iph = (struct iphdr *)(packet + ETHER_HDR_SIZE);
			ip_init_hdr(iph, entry->ip, MOSPF_ALLSPFRouters, IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE, IPPROTO_MOSPF);
			//ether header
			struct ether_header *eh = (struct ether_header*)packet;
			eh->ether_type = htons(ETH_P_IP);
			char mospf_hello_mac[ETH_ALEN] = {0x1, 0x0, 0x5e, 0x0, 0x0, 0x5};
			memcpy(eh->ether_dhost, mospf_hello_mac, ETH_ALEN);
			memcpy(eh->ether_shost, entry->mac, ETH_ALEN);
			iface_send_packet(entry, packet, ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE);
		}
	}

	return NULL;
}

void *sending_mospf_lsu_thread(void *param)
{
	//fprintf(stdout, "TODO: send mOSPF LSU message periodically.\n");
	while(1){
		sleep(1);
		pthread_mutex_lock(&mospf_lock);
		instance->lsuint ++;
		if(instance->lsuint > MOSPF_DEFAULT_LSUINT){
			instance->lsuint = 0;
			iface_info_t *iface;
			int nadv = 0;
			list_for_each_entry(iface, &instance->iface_list, list){
				if(iface->num_nbr){
					nadv += iface->num_nbr;
				}else{
					nadv ++;
				}
			}
			char *packet = (char*)malloc(MOSPF_HDR_SIZE + MOSPF_LSU_SIZE + nadv * MOSPF_LSA_SIZE);
			struct mospf_lsa *lsa = (struct mospf_lsa*)(packet + MOSPF_HDR_SIZE + MOSPF_LSU_SIZE);
			int iadv = 0;
			list_for_each_entry(iface, &instance->iface_list, list){
				if(iface->num_nbr){
					mospf_nbr_t *nbr;
					list_for_each_entry(nbr, &iface->nbr_list, list){
						lsa[iadv].network = htonl(iface->ip & iface->mask);
						lsa[iadv].mask = htonl(iface->mask);
						lsa[iadv].rid = htonl(nbr->nbr_id);
						iadv++;
					}
				}else{
					lsa[iadv].network = htonl(iface->ip & iface->mask);
					lsa[iadv].mask = htonl(iface->mask);
					lsa[iadv].rid = 0;
					iadv++;
				}
			}
			struct mospf_lsu *lsu = (struct mospf_lsu*)(packet + MOSPF_HDR_SIZE);
			mospf_init_lsu(lsu, nadv);
			instance->sequence_num ++;
			struct mospf_hdr *mospfh = (struct mospf_hdr*)packet;
			mospf_init_hdr(mospfh, MOSPF_TYPE_LSU, MOSPF_HDR_SIZE + MOSPF_LSU_SIZE + nadv * MOSPF_LSA_SIZE, instance->router_id, instance->area_id);
			mospfh->checksum = mospf_checksum(mospfh);
			//forward
			list_for_each_entry(iface, &instance->iface_list, list){
				mospf_nbr_t *nbr;
				list_for_each_entry(nbr, &iface->nbr_list, list){
					char *pkt = (char*)malloc(ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + ntohs(mospfh->len));
					memcpy(pkt + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE, packet, ntohs(mospfh->len));
					struct iphdr *iph = packet_to_ip_hdr(pkt);						
					ip_init_hdr(iph, iface->ip, nbr->nbr_ip, IP_BASE_HDR_SIZE + ntohs(mospfh->len), IPPROTO_MOSPF);
					ip_send_packet(pkt, ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + ntohs(mospfh->len));
				}
			}
			free(packet);
		}
		pthread_mutex_unlock(&mospf_lock);
	}

	return NULL;
}

void *checking_nbr_thread(void *param)
{
	//fprintf(stdout, "TODO: neighbor list timeout operation.\n");
	while(1){
		sleep(1);
		pthread_mutex_lock(&mospf_lock);
		iface_info_t *iface;
		list_for_each_entry(iface, &instance->iface_list, list){
			mospf_nbr_t *nbr, *q;
			list_for_each_entry_safe(nbr, q, &iface->nbr_list, list){
				nbr->alive ++;
				if(nbr->alive >= 3*MOSPF_DEFAULT_HELLOINT){
					list_delete_entry(&nbr->list);
					free(nbr);
					iface->num_nbr --;
					instance->lsuint += MOSPF_DEFAULT_LSUINT;
				}
			}
		}
		pthread_mutex_unlock(&mospf_lock);
	}

	return NULL;
}

void *checking_database_thread(void *param)
{
	//fprintf(stdout, "TODO: link state database timeout operation.\n");
	while(1){
		sleep(1);
		pthread_mutex_lock(&mospf_lock);
		mospf_db_entry_t *db_entry, *q;
		list_for_each_entry_safe(db_entry, q, &mospf_db, list){
			db_entry->alive++;
			if(db_entry->alive > MOSPF_DATABASE_TIMEOUT){
				free(db_entry->array);
				list_delete_entry(&db_entry->list);
				free(db_entry);
			}
		}
		pthread_mutex_unlock(&mospf_lock);
	}

	return NULL;
}

void handle_mospf_hello(iface_info_t *iface, const char *packet, int len)
{
	//fprintf(stdout, "TODO: handle mOSPF Hello message.\n");
	pthread_mutex_lock(&mospf_lock);
	struct iphdr *iph = packet_to_ip_hdr(packet);
	struct mospf_hdr *mospfh = (struct mospf_hdr*)IP_DATA(iph);
	struct mospf_hello *hello = (struct mospf_hello*)(IP_DATA(iph) + MOSPF_HDR_SIZE);
	u32 nbr_id = ntohl(mospfh->rid);
	u32 nbr_ip = ntohl(iph->saddr);
	u32 nbr_mask = ntohl(hello->mask);
	mospf_nbr_t *nbr;
	list_for_each_entry(nbr, &iface->nbr_list, list){
		if(nbr->nbr_id==nbr_id)
			break;
	}
	if(&nbr->list == &iface->nbr_list){
		nbr = (mospf_nbr_t*)malloc(sizeof(mospf_nbr_t));
		nbr->nbr_id = nbr_id;
		list_add_tail(&nbr->list, &iface->nbr_list);
		iface->num_nbr++;
		instance->lsuint += MOSPF_DEFAULT_LSUINT;
	}
	nbr->nbr_ip = nbr_ip;
	nbr->nbr_mask = nbr_mask;
	nbr->alive = 0;
	pthread_mutex_unlock(&mospf_lock);
}

void handle_mospf_lsu(iface_info_t *iface, char *packet, int len)
{
	//fprintf(stdout, "TODO: handle mOSPF LSU message.\n");
	pthread_mutex_lock(&mospf_lock);
	struct iphdr *iph = packet_to_ip_hdr(packet);
	struct mospf_hdr *mospfh = (struct mospf_hdr*)IP_DATA(iph);
	struct mospf_lsu *lsu = (struct mospf_lsu*)(IP_DATA(iph) + MOSPF_HDR_SIZE);
	u32 rid = ntohl(mospfh->rid);
	u16 seq = ntohs(lsu->seq);
	int nadv = ntohl(lsu->nadv);
	if(rid!=instance->router_id){
		mospf_db_entry_t *db_entry;
		list_for_each_entry(db_entry, &mospf_db, list){
			if(db_entry->rid == rid){
				break;
			}
		}
		if(&db_entry->list == &mospf_db){
			db_entry = (mospf_db_entry_t*)malloc(sizeof(mospf_db_entry_t));
			list_add_tail(&db_entry->list, &mospf_db);
			db_entry->rid = rid;
			db_entry->seq = seq;
			db_entry->nadv = nadv;
			db_entry->alive = 0;
			db_entry->array = (struct mospf_lsa*)malloc(sizeof(struct mospf_lsa)*nadv);
			for(int i=0;i<nadv;i++){
				struct mospf_lsa *lsa = (struct mospf_lsa*)((char*)lsu + MOSPF_LSU_SIZE + i * MOSPF_LSA_SIZE);
				db_entry->array[i].network = ntohl(lsa->network);
				db_entry->array[i].mask = ntohl(lsa->mask);
				db_entry->array[i].rid = ntohl(lsa->rid);
			}
		}else if(db_entry->seq < seq){
			db_entry->seq = seq;
			db_entry->nadv = nadv;
			db_entry->alive = 0;
			free(db_entry->array);
			db_entry->array = (struct mospf_lsa*)malloc(sizeof(struct mospf_lsa)*nadv);
			for(int i=0;i<nadv;i++){
				struct mospf_lsa *lsa = (struct mospf_lsa*)((char*)lsu + MOSPF_LSU_SIZE + i * MOSPF_LSA_SIZE);
				db_entry->array[i].network = ntohl(lsa->network);
				db_entry->array[i].mask = ntohl(lsa->mask);
				db_entry->array[i].rid = ntohl(lsa->rid);
			}
		}
	}
	//ttl-1
	lsu->ttl--;
	//checksum
	mospfh->checksum = mospf_checksum(mospfh);
	//forward
	if(lsu->ttl>0){
		iface_info_t *iface_entry;
		list_for_each_entry(iface_entry, &instance->iface_list, list){
			if(iface!=iface_entry){
				mospf_nbr_t *nbr;
				list_for_each_entry(nbr, &iface_entry->nbr_list, list){
					char *pkt = (char*)malloc(ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + ntohs(mospfh->len));
					memcpy(pkt + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE, packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE, ntohs(mospfh->len));
					struct iphdr *iph = packet_to_ip_hdr(pkt);
					ip_init_hdr(iph, iface_entry->ip, nbr->nbr_ip, IP_BASE_HDR_SIZE + ntohs(mospfh->len), IPPROTO_MOSPF);
					ip_send_packet(pkt, ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + ntohs(mospfh->len));
				}
			}
		}
	}
	pthread_mutex_unlock(&mospf_lock);
}

void handle_mospf_packet(iface_info_t *iface, char *packet, int len)
{
	struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
	struct mospf_hdr *mospf = (struct mospf_hdr *)((char *)ip + IP_HDR_SIZE(ip));

	if (mospf->version != MOSPF_VERSION) {
		log(ERROR, "received mospf packet with incorrect version (%d)", mospf->version);
		return ;
	}
	if (mospf->checksum != mospf_checksum(mospf)) {
		log(ERROR, "received mospf packet with incorrect checksum");
		return ;
	}
	if (ntohl(mospf->aid) != instance->area_id) {
		log(ERROR, "received mospf packet with incorrect area id");
		return ;
	}

	switch (mospf->type) {
		case MOSPF_TYPE_HELLO:
			handle_mospf_hello(iface, packet, len);
			break;
		case MOSPF_TYPE_LSU:
			handle_mospf_lsu(iface, packet, len);
			break;
		default:
			log(ERROR, "received mospf packet with unknown type (%d).", mospf->type);
			break;
	}
}
