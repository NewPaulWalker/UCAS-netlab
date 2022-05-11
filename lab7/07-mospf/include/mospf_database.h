#ifndef __MOSPF_DATABASE_H__
#define __MOSPF_DATABASE_H__

#include "base.h"
#include "list.h"

#include "mospf_proto.h"

extern struct list_head mospf_db;

typedef struct {
	struct list_head list;
	u32	rid;
	u16	seq;
	int nadv;
	int alive;
	struct mospf_lsa *array;
} mospf_db_entry_t;

pthread_mutex_t rtable_lock;

void init_mospf_db();
void init_net_list();
void add_net(rt_net_note *pre_net, u32 network);
int net_added(u32 network);
void add_route(u32 dest, u32 mask, u32 pre_net);
void update_rtable();
#endif
