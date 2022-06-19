#include "tcp.h"
#include "tcp_sock.h"
#include "tcp_timer.h"

#include "log.h"
#include "ring_buffer.h"

#include <stdlib.h>
// update the snd_wnd of tcp_sock
//
// if the snd_wnd before updating is zero, notify tcp_sock_send (wait_send)
static inline void tcp_update_window(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	//init
	if(tsk->state == TCP_SYN_RECV || tsk->state == TCP_SYN_SENT){
		tsk->snd_una = cb->ack;
		tsk->adv_wnd = cb->rwnd;
		tsk->cwnd ++;
		tsk->snd_wnd = min(tsk->adv_wnd/TCP_MSS, tsk->cwnd);
		return ;
	}
	//normal state
	pthread_mutex_lock(&tsk->wnd_lock);
	u16 old_snd_wnd = tsk->snd_wnd;
	if(cb->ack > tsk->snd_una){
		tsk->adv_wnd = cb->rwnd;
		tsk->snd_una = cb->ack;
	}

	if(tsk->recovery_point==0 && tsk->frpacks==-1){
		if(tsk->cwnd < tsk->ssthresh){
			//**Slow Start**
			tsk->cwnd ++;
		}else{
			//**Congestion Avoidance**
			tsk->capacks ++;
			if(tsk->capacks >= tsk->cwnd){
				tsk->cwnd ++;
				tsk->capacks = 0;
			}
		}
		if(cb->seq == tsk->duseq){
			tsk->dupacks ++;
			if(tsk->dupacks >= 3){
				//**Fast Retransmission**
				if(!list_empty(&tsk->send_buf)){
					struct send_packet *resend = list_entry(tsk->send_buf.next, struct send_packet, list);
					char *packet = (char *)malloc(resend->len);
					memcpy(packet, resend->packet, resend->len);
					ip_send_packet(packet, resend->len);
				}
				tsk->ssthresh = tsk->cwnd / 2;
				tsk->duseq = 0;
				tsk->dupacks = 0;
				tsk->frpacks = tsk->cwnd;
			}
		}else{
			tsk->duseq = cb->seq;
			tsk->dupacks = 1;
		}
	}else if(tsk->recovery_point == 0){
		tsk->frpacks --;
		if(tsk->frpacks % 2){
			tsk->cwnd --;
		}
		if(tsk->frpacks == 0){
			tsk->frpacks = -1;
			tsk->recovery_point = tsk->snd_nxt;
		}
	}

	tsk->snd_wnd = min(tsk->adv_wnd/TCP_MSS, tsk->cwnd + tsk->temp_cwnd);

	//log
	struct timeval now;
	gettimeofday(&now, NULL);
	fprintf(tsk->fd, "%ld%06ld		%d\n",now.tv_sec, now.tv_usec, tsk->cwnd);

	pthread_mutex_unlock(&tsk->wnd_lock);
	wake_up(tsk->wait_send);
}

// update the snd_wnd safely: cb->ack should be between snd_una and snd_nxt
static inline void tcp_update_window_safe(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	if (less_or_equal_32b(tsk->snd_una, cb->ack) && less_or_equal_32b(cb->ack, tsk->snd_nxt))
		tcp_update_window(tsk, cb);
}

#ifndef max
#	define max(x,y) ((x)>(y) ? (x) : (y))
#endif

// check whether the sequence number of the incoming packet is in the receiving
// window
static inline int is_tcp_seq_valid(struct tcp_sock *tsk, struct tcp_cb *cb)
{
	u32 rcv_end = tsk->rcv_nxt + max(tsk->rcv_wnd, 1);
	if (less_than_32b(cb->seq, rcv_end) && less_or_equal_32b(tsk->rcv_nxt, cb->seq_end)) {
		return 1;
	}
	else {
		log(ERROR, "received packet with invalid seq, drop it.");
		tcp_send_control_packet(tsk, TCP_ACK);
		return 0;
	}
}

// recv data
int tcp_sock_recv(struct tcp_sock *tsk, struct tcp_cb *cb){
	//judge if have recvd
	if(less_or_equal_32b(cb->seq_end, tsk->rcv_nxt)){
		tcp_send_control_packet(tsk, TCP_ACK);
		return -1;
	}
	struct recv_packet *recvd;
	list_for_each_entry(recvd, &tsk->rcv_ofo_buf, list){
		if(recvd->seq == cb->seq){
			tcp_send_control_packet(tsk, TCP_ACK);
			return -1;
		}
	}
	if(cb->pl_len == 0 && !(cb->flags & TCP_FIN)){
		return 0;
	}

	if(tsk->rcv_nxt == cb->seq){
		if(cb->pl_len){
			while(ring_buffer_free(tsk->rcv_buf) < cb->pl_len){
				wake_up(tsk->wait_recv);
				sleep_on(tsk->wait_recv);
			}
			write_ring_buffer(tsk->rcv_buf, cb->payload, cb->pl_len);
		}
		if(cb->flags & TCP_FIN){
			switch (tsk->state)
			{
			case TCP_ESTABLISHED:
				tcp_set_state(tsk, TCP_CLOSE_WAIT);
				wake_up(tsk->wait_recv);
				break;
			case TCP_FIN_WAIT_1:
			case TCP_FIN_WAIT_2:
				tcp_set_state(tsk, TCP_TIME_WAIT);
				tcp_set_timewait_timer(tsk);
			default:
				break;
			}
		}
		tsk->rcv_nxt = cb->seq_end;
	}else{
		//add to ofo buffer
		struct recv_packet *pend = (struct recv_packet*)malloc(sizeof(struct recv_packet));
		if(cb->pl_len){
			char *packet = (char *)malloc(cb->pl_len);
			memcpy(packet, cb->payload, cb->pl_len);
			pend->packet = packet;
		}else{
			pend->packet = NULL;
		}
		pend->len = cb->pl_len;
		pend->seq = cb->seq;
		pend->seq_end = cb->seq_end;
		pend->flags = cb->flags;
		struct recv_packet *entry, *q;
		list_for_each_entry_safe(entry, q, &tsk->rcv_ofo_buf, list){
			if(greater_than_32b(entry->seq, pend->seq)){
				list_insert(&pend->list, entry->list.prev, &entry->list);
				break;
			}
		}
		if(&entry->list == &tsk->rcv_ofo_buf)
			list_add_tail(&pend->list, &tsk->rcv_ofo_buf);
	}
	//write to rcv buff
	do{
		if(list_empty(&tsk->rcv_ofo_buf)){
			break;
		}
		struct recv_packet *pend = list_entry(tsk->rcv_ofo_buf.next, struct recv_packet, list);
		if(tsk->rcv_nxt != pend->seq){
			break;
		}
		if(pend->len){
			while(ring_buffer_free(tsk->rcv_buf) < pend->len){
				wake_up(tsk->wait_recv);
				sleep_on(tsk->wait_recv);
			}
			write_ring_buffer(tsk->rcv_buf, pend->packet, pend->len);
			free(pend->packet);
		}
		if(pend->flags & TCP_FIN){
			switch (tsk->state)
			{
			case TCP_ESTABLISHED:
				tcp_set_state(tsk, TCP_CLOSE_WAIT);
				wake_up(tsk->wait_recv);
				break;
			case TCP_FIN_WAIT_1:
			case TCP_FIN_WAIT_2:
				tcp_set_state(tsk, TCP_TIME_WAIT);
				tcp_set_timewait_timer(tsk);
			default:
				break;
			}

		}

		tsk->rcv_nxt = pend->seq_end;
		
		list_delete_entry(&pend->list);
		free(pend);
	}while(1);

	tsk->rcv_wnd = ring_buffer_free(tsk->rcv_buf);
	wake_up(tsk->wait_recv);

	tcp_send_control_packet(tsk, TCP_ACK);

	return cb->pl_len;
}

// ack send buf
void tcp_ack_send_buf(struct tcp_sock *tsk, struct tcp_cb *cb){
	if(less_or_equal_32b(cb->ack, tsk->snd_una)){
		return ;
	}
	struct send_packet *send, *q;
	int changed = 0;
	list_for_each_entry_safe(send, q, &tsk->send_buf, list){
		if(greater_or_equal_32b(cb->ack, send->seq_end)){
			free(send->packet);
			list_delete_entry(&send->list);
			free(send);
			changed = 1;
		}
	}
	pthread_mutex_lock(&tsk->wnd_lock);
	if(tsk->recovery_point!=0){
		//**Fast Recovery**
		if(changed == 0){
			//infligt--;
			tsk->temp_cwnd ++;
			tsk->snd_wnd = min(tsk->adv_wnd/TCP_MSS, tsk->cwnd + tsk->temp_cwnd);
			wake_up(tsk->wait_send);
		}else if(cb->ack < tsk->recovery_point){
			//partail ack
			if(!list_empty(&tsk->send_buf)){
				struct send_packet *resend = list_entry(tsk->send_buf.next, struct send_packet, list);
				char *packet = (char *)malloc(resend->len);
				memcpy(packet, resend->packet, resend->len);
				ip_send_packet(packet, resend->len);
			}
		}else{
			//full ack
			tsk->recovery_point = 0;
			tsk->temp_cwnd = 0;
			tsk->snd_wnd = min(tsk->adv_wnd/TCP_MSS, tsk->cwnd + tsk->temp_cwnd);
		}
	}
	pthread_mutex_unlock(&tsk->wnd_lock);
	if(list_empty(&tsk->send_buf)){
		tcp_unset_retrans_timer(tsk);
	}else if(changed){
		tcp_set_retrans_timer(tsk, 0);
	}
}

// Process the incoming packet according to TCP state machine. 
void tcp_process(struct tcp_sock *tsk, struct tcp_cb *cb, char *packet)
{
	//fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);
	if(tsk==NULL){
		//sock not exist
		tcp_send_reset(cb);
		return ;
	}

	if(cb->flags == (TCP_RST | TCP_ACK)){
		//rst
		tcp_set_state(tsk, TCP_CLOSED);
		tcp_unset_retrans_timer(tsk);
		tcp_unhash(tsk);
		tcp_bind_unhash(tsk);
		return ;
	}

	switch (tsk->state)
	{
	case TCP_LISTEN:
		if(cb->flags == TCP_SYN){
			struct tcp_sock *csk = alloc_tcp_sock();
			csk->sk_sip = cb->daddr;
			csk->sk_dip = cb->saddr;
			csk->sk_sport = cb->dport;
			csk->sk_dport = cb->sport;
			csk->parent = tsk;
			list_add_tail(&csk->list, &tsk->listen_queue);
			tcp_set_state(csk, TCP_SYN_RECV);
			tcp_hash(csk);
			csk->iss = tcp_new_iss();
			csk->snd_una = csk->iss;
			csk->snd_nxt = csk->iss;
			csk->rcv_nxt = cb->seq_end;
			tcp_send_control_packet(csk, TCP_SYN|TCP_ACK);
			tcp_set_retrans_timer(csk, 1);
		}
		break;
	case TCP_SYN_SENT:
		if(cb->flags == (TCP_SYN | TCP_ACK)){
			tsk->rcv_nxt = cb->seq_end;
			tcp_update_window_safe(tsk, cb);
			tcp_set_state(tsk, TCP_ESTABLISHED);
			tcp_ack_send_buf(tsk, cb);
			tcp_send_control_packet(tsk, TCP_ACK);
			wake_up(tsk->wait_connect);
		}
		break;
	case TCP_SYN_RECV:
		if(cb->flags == TCP_ACK){
			if(!is_tcp_seq_valid(tsk, cb)){
				return ;
			}
			tcp_update_window_safe(tsk, cb);
			tcp_ack_send_buf(tsk, cb);
			if(tcp_sock_accept_queue_full(tsk->parent)){
				tcp_set_state(tsk, TCP_CLOSED);
				tcp_send_reset(cb);
				list_delete_entry(&tsk->list);
				tcp_unset_retrans_timer(tsk);
				tcp_unhash(tsk);
			}else{
				tcp_set_state(tsk, TCP_ESTABLISHED);
				tcp_sock_accept_enqueue(tsk);
				wake_up(tsk->parent->wait_accept);
			}
		}
		break;
	case TCP_ESTABLISHED:
		if(!is_tcp_seq_valid(tsk, cb)){
			return ;
		}
		if(cb->flags & TCP_ACK){
			int rlen = tcp_sock_recv(tsk, cb);
			tcp_update_window_safe(tsk, cb);
			tcp_ack_send_buf(tsk, cb);
		}
		break;
	case TCP_FIN_WAIT_1:
		if(!is_tcp_seq_valid(tsk, cb)){
			return ;
		}
		if(cb->flags & TCP_ACK){
			int rlen = tcp_sock_recv(tsk, cb);
			tcp_update_window_safe(tsk, cb);
			tcp_ack_send_buf(tsk, cb);
			tcp_set_state(tsk, TCP_FIN_WAIT_2);
		}
		break;
	case TCP_FIN_WAIT_2:
		if(!is_tcp_seq_valid(tsk, cb)){
			return ;
		}
		if(cb->flags & TCP_ACK){
			int rlen = tcp_sock_recv(tsk, cb);
			tcp_update_window_safe(tsk, cb);
			tcp_ack_send_buf(tsk, cb);
		}
		break;
	case TCP_TIME_WAIT:
		if(!is_tcp_seq_valid(tsk, cb)){
			return ;
		}
		if(cb->flags & TCP_ACK){
			int rlen = tcp_sock_recv(tsk, cb);
			tcp_update_window_safe(tsk, cb);
			tcp_ack_send_buf(tsk, cb);
		}
		break;
	case TCP_CLOSE_WAIT:
		if(!is_tcp_seq_valid(tsk, cb)){
			return ;
		}
		if(cb->flags & TCP_ACK){
			int rlen = tcp_sock_recv(tsk, cb);
			tcp_update_window_safe(tsk, cb);
			tcp_ack_send_buf(tsk, cb);
		}
		break;
	case TCP_LAST_ACK:
		if(!is_tcp_seq_valid(tsk, cb)){
			return ;
		}
		if(cb->flags & TCP_ACK){
			tcp_update_window_safe(tsk, cb);
			tcp_ack_send_buf(tsk, cb);
			if(list_empty(&tsk->send_buf)){
				tcp_set_state(tsk, TCP_CLOSED);
				tcp_unhash(tsk);
				tcp_bind_unhash(tsk);
			}
		}
		break;
	default:
		break;
	}
}
