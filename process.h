#include<sys/types.h>
#include<sys/socket.h>
#include<sys/ioctl.h>
#include<signal.h>
#include<unistd.h>
#include<fcntl.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<netinet/in.h>
#include<errno.h>
#include<netdb.h>
#include<time.h>
#include<stdbool.h>
/*
uint8_t message_type where
STATUS=0
GOSSIP=1

The second attribute would be: "server_id". This would correspond to either the "server_id" of the origin of this message, or the "server_id" of your neighbor that is sending you its current status. (0~3)
uint8_t server_id

The third attribute would be the payload of the message
uint16_t payload[201]

For each of the two cases listed above, we would have to parse the payload in different ways:
1. Gossip Message parsing, where we just copy the payload into the buffer. But the payload would also have the "seq_num" and we would have to agree on the exact structure (Most likely the first element of the array).
2. Status Message parsing, where we would have the current status of the sender. This would use the first four elements of the array from 0~3 (maximum 4 P2P servers)
I'm electing to use uint16_t here because sequence number can go upto 1000.
*/
#define MSGLEN   201
#define LOGLEN   1001
#define PORTBASE 20000
#define PEERNUM  5
#define TIMEOUT  15

enum MSG_TYPE {
    APPENTRY=0,
	REQVOTE=1,
    VOTE=2,
	NEWMSG=3,
	COMMIT=4,
	HEARTBEAT=5
};

enum SERVER_TYPE {
    LEADER=0,
    FOLLOWER=1,
	CANDIDATE=2
};

typedef struct {
    enum MSG_TYPE message_type;
	enum SERVER_TYPE server_type;
	int msg_len;
    // int server_id;
	int origin; /* msg origin */
	int from;  /* sent from */
	int seq_num;

	int cur_term;
	int pre_term;
	int pre_index;
	int commit_index;
	int success;
    // int vector_clock[PEERNUM];
	char msg[MSGLEN];
	// uint16_t payload[MSGLEN];
} __attribute__((packed)) message;


typedef struct state_t {
	int sockfd;
	int tcp_sockfd;
	int udp_sockfd;
	int port;

	int cur_term; 
	int timeout; /* timeout for each period*/
	int granted;
	int num_votes;
	enum SERVER_TYPE server_type;
	int server_id;

	struct sockaddr_in tcp_socket, udp_socket, client;
	struct sockaddr_in leader;
	int leader_id;
	socklen_t socklen;

	int num_servers;
	int seq_num;
	// message status;
	int status[PEERNUM]; /* next log entry to send */
	

	message client_msg; /* record msg from clients */
	int climsg_committed; /* record if the msg is committed*/

	int num_stored; /* record number of servers that store a msg*/

	message msg_log[LOGLEN];
	int num_msg;
	int num_committed;
} state_t;

void raft_init(int server_id, int port, int num_servers);
void raft_setup_udpsocket(int server_id);
void raft_setup_tcpsocket(int server_id);
void raft_recv();
void raft_msg_from_proxy(char* buf, int n);
void raft_msg_to_proxy();
void raft_send_requestvote();
void raft_send_vote(message msg);
void raft_send_heartbeat();

void raft_newmsg_to_leader();
void raft_leader_recv_newmsg(message msg);
void raft_app_to_peers();
void raft_follo_recv_app(message msg);
void raft_leader_recv_app(message msg);
void raft_send_commit();
void raft_follo_recv_commit(message msg);
void raft_send_sync_req();
void raft_ack_to_proxy(int msg_id, int seq_id);



// void p2p_rumor_to_peer(int peer_id, int msg_server_id, int total);

int guard(int n, char *err) { if (n == -1) { perror(err); exit(-1); } return n; }
int left_id(int server_id, int num_servers) { return (server_id + num_servers - 1) % num_servers; }
int right_id(int server_id, int num_servers) { return (server_id + 1) % num_servers; }
int rand_time() {return rand() % 10 + 6;}
