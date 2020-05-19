#include "process.h"

state_t s;

void alarm_handler(int sig) {
    signal(SIGALRM, SIG_IGN);
	printf("alarm_handler(): timeout\n");
	if(s.server_type == LEADER){
		printf("alarm_handler(): leader sending heartbeat\n");
		raft_send_heartbeat();

		s.timeout = rand() % 2 + 5;
	}else if(s.server_type == FOLLOWER){
		s.server_type = CANDIDATE;
		s.cur_term++;
		s.granted = 0;
		s.num_votes = 0;
		raft_send_requestvote();

		s.timeout = rand_time();
	}else if(s.server_type == CANDIDATE){
		s.cur_term++;
		s.granted = 0;
		s.num_votes = 0;
		raft_send_requestvote();

		s.timeout = rand_time();
	}
	
	// close(s.sockfd);
	// exit(0);

    signal(SIGALRM, alarm_handler);
    alarm(s.timeout);

    // alarm(5);
}

void stub_test_msg(int num, int server_id) {
	for (int i = 1; i <= num; ++ i) {
		message msg;
		msg.message_type = NEWMSG;
		// msg.server_id = server_id;
		msg.from = server_id;
		msg.origin = server_id;
		msg.seq_num = i;
		memset(msg.msg, 0, MSGLEN);
		snprintf(msg.msg, MSGLEN, "Hello%d", i);
		// strcpy(msg.payload, "Hello");
		printf("%s\n", msg.msg);
		s.client_msg = msg;
		s.climsg_committed = 0;
	}

	// printf("%d", s.num_rumors);
	// printf("%s", s.rumors[2].rumor_msg);
	// for (int i = 0; i < s.num_servers; ++ i)
	// 	printf("%d", s.status[i]);
	fflush(stdout);
}

void raft_init(int server_id, int port, int num_servers) {
	srand((unsigned)time(0));
	s.timeout = rand_time();
	printf("time: %d\n", s.timeout);

	s.server_id = server_id;
	s.port = port;
	s.server_type = FOLLOWER;
	s.num_servers = num_servers;
	s.cur_term = 0;
	s.granted = 0;
	s.num_msg = 0;
	s.num_committed = 0;
	s.climsg_committed = 1;
	
	s.num_stored = 1;

	s.seq_num = 1; // starting from 1
	memset(s.status, 0, sizeof(s.status));
	memset(s.msg_log, 0, sizeof(s.msg_log));
	memset(&s.client_msg, 0, sizeof(s.client_msg));
	for (int i = 0; i < s.num_servers; ++ i)
		s.status[i] = 0; 
	raft_setup_udpsocket(server_id);
	raft_setup_tcpsocket(port);

	s.leader = s.udp_socket;
}

void raft_setup_tcpsocket(int port) {
	int sockfd = guard(socket(AF_INET, SOCK_STREAM, 0),
		"raft_setup_tcpsocket(): error in socket().");

	memset(&s.tcp_socket, 0, sizeof(struct sockaddr_in));
	s.tcp_socket.sin_family      = AF_INET;
	s.tcp_socket.sin_addr.s_addr = htonl(INADDR_ANY);
	s.tcp_socket.sin_port        = htons(port);

	int option = 1;
	guard(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &option,
		sizeof(option)), "raft_setup_tcpsocket(): error in setsockopt().");
	guard(bind(sockfd, (struct sockaddr *)&s.tcp_socket,
		sizeof(struct sockaddr_in)), "raft_setup_tcpsocket(): error in bind().");

	guard(listen(sockfd, 1), "raft_setup_tcpsocket(): error in listen().");

	s.socklen = sizeof(struct sockaddr_in);
	s.tcp_sockfd = guard(accept(sockfd, (struct sockaddr *)&(s.client), &(s.socklen)),
		"raft_setup_tcpsocket(): error in accept().");
	printf("raft_setup_tcpsocket(): port %d success.\n", port);
	fflush(stdout);
}

void raft_setup_udpsocket(int server_id) {
	s.udp_sockfd = guard(socket(AF_INET, SOCK_DGRAM, 0),
		"raft_setup_udpsocket(): error in socket().");

	memset(&s.udp_socket, 0, sizeof(struct sockaddr_in));
	s.udp_socket.sin_family      = AF_INET;
	s.udp_socket.sin_addr.s_addr = htonl(INADDR_ANY);
	s.udp_socket.sin_port        = htons(PORTBASE + server_id);

	int option = 1;
	guard(setsockopt(s.udp_sockfd, SOL_SOCKET, SO_REUSEADDR, &option,
		sizeof(option)), "raft_setup_udpsocket(): error in setsockopt().");
	guard(fcntl(s.udp_sockfd, F_SETFL, fcntl(s.udp_sockfd, F_GETFL, 0) | O_NONBLOCK),
		"raft_setup_udpsocket(): error in fcntl()");
	guard(bind(s.udp_sockfd, (struct sockaddr *)&s.udp_socket,
		sizeof(struct sockaddr_in)), "raft_setup_udpsocket(): error in bind().");
	printf("raft_setup_udpsocket(): success.\n");
	fflush(stdout);
}

void raft_msg_from_proxy(char* buf, int n) {
	char *temp;
	temp = strchr(buf, ' ');

	int first = (int)(temp - buf); // first space idx
	int second = first + 1; // second space idx
	while (second < n && buf[second] != ' ') {
		second ++;
	}

	int end = second;
	int start = first + 1;
	char *label = (char *)calloc(1, end - start + 1);
	strncpy(label, buf + start, end - start); // seq_num

	end = n - 1; // not including '\n'
	start = second + 1;

	// after sync, update s.seq_num
	// s.seq_num = s.status[s.server_id];

	message msg;
	msg.message_type = NEWMSG;
	msg.msg_len = end - start;
	// msg.server_id = s.server_id;
	msg.origin = s.server_id;
	msg.from = s.server_id;
	msg.seq_num = atoi(label);
	memset(msg.msg, 0, MSGLEN);
	strncpy(msg.msg, buf + start, end - start);
	s.client_msg = msg;
	s.client_msg.cur_term = s.cur_term;
	s.climsg_committed = 0;
	printf("raft_msg_from_proxy(): receive msg from proxy: len = %d, seq = %d, msg = %s\n",
		s.client_msg.msg_len, s.client_msg.seq_num, s.client_msg.msg);
	fflush(stdout);

	if(s.server_type == FOLLOWER){
		raft_newmsg_to_leader();
	}else if(s.server_type == LEADER && (s.num_msg == s.num_committed)){ 
		// check if there's not uncommitted msg
		s.msg_log[s.num_msg] = s.client_msg;
		s.num_msg++;
		s.num_stored = 1;
		raft_app_to_peers();
	}


	// s.rumors[s.num_rumors] = msg;
	// s.status[s.server_id] ++;
	// s.num_rumors ++;
	// s.seq_num ++;

	// s.last_server = msg.origin;
	// s.last_seqnum = msg.seq_num;
}

void raft_msg_to_proxy() {
	char send_buf[MSGLEN * (s.num_committed + 1) + 10];
	memset(send_buf, 0, sizeof(send_buf));
	strcat(send_buf, "chatLog ");
	int num_to_send = 8;
	for (int i = 0; i < s.num_committed; ++ i) {
		strncat(send_buf, s.msg_log[i].msg, s.msg_log[i].msg_len);
		strcat(send_buf, ",");
		num_to_send += s.msg_log[i].msg_len + 1;
	}
	strcat(send_buf, "\n");
	num_to_send += 1;
	printf("raft_msg_to_proxy(): send chatLog to proxy: len = %d, msg = %s\n", num_to_send, send_buf);
	fflush(stdout);
	guard(sendto(s.tcp_sockfd, send_buf, num_to_send, 0,
		(struct sockaddr *)&s.client, sizeof(s.client)),
		"raft_msg_to_proxy(): error in sendto().");
}

void raft_ack_to_proxy(int msg_id, int seq_id){
	char send_buf[20];
	memset(send_buf, 0, sizeof(send_buf));
	sprintf(send_buf, "ack %d %d\n", msg_id, seq_id);

	int num_to_send = 0;
	while(num_to_send < 20 && send_buf[num_to_send] != '\n'){
		num_to_send++;
	}
	num_to_send++;

	printf("raft_ack_to_proxy(): send ack to proxy: msg = %s\n", send_buf);
	fflush(stdout);
	guard(sendto(s.tcp_sockfd, send_buf, num_to_send, 0,
		(struct sockaddr *)&s.client, sizeof(s.client)),
		"raft_ack_to_proxy(): error in sendto().");
}

void raft_send_requestvote() {
	message req;

	s.num_votes = 1;
	s.granted = 1;

	req.from = s.server_id;
	req.origin = s.server_id;
	req.message_type = REQVOTE;
	req.cur_term = s.cur_term;
	req.server_type = CANDIDATE;
	if(s.num_committed == 0){
		req.pre_term = -1;
		req.pre_index = -1;
	}else{
		req.pre_term = s.msg_log[s.num_committed-1].cur_term;
		req.pre_index = s.num_committed-1;
	}

	for(int i = 0; i < s.num_servers; i++){
		if(i != s.server_id){
			struct sockaddr_in dest = s.udp_socket;
			dest.sin_port = htons(PORTBASE + i);

			printf("raft_send_requestvote(): term%d send REQVOTE to server%d.\n", s.cur_term, i);

			guard(sendto(s.udp_sockfd, &req, sizeof(req), 0,
				(struct sockaddr *)&dest, sizeof(dest)),
				"raft_send_requestvote(): error in sendto().");
		}
	}

	printf("raft_send_requestvote(): term%d send REQVOTE to all peers.\n", s.cur_term);
	fflush(stdout);
}


void raft_send_vote(message msg) {
	printf("raft_send_vote(): term%d reqvote from server%d.\n", msg.cur_term, msg.origin);
	if(s.num_committed != 0){
		if(s.msg_log[s.num_committed-1].cur_term > msg.pre_term){
			return;
		}else if(s.msg_log[s.num_committed-1].cur_term == msg.pre_term && s.num_committed-1 > msg.pre_index){
			return;
		}
	}
	s.granted = 1;

	message vote;

	vote.from = s.server_id;
	vote.origin = s.server_id;
	vote.message_type = VOTE;
	vote.cur_term = s.cur_term;
	vote.server_type = s.server_type;

	struct sockaddr_in dest = s.udp_socket;
	dest.sin_port = htons(PORTBASE + msg.origin);
	printf("raft_send_vote(): term%d send VOTE to server%d.\n", s.cur_term, msg.origin);
	fflush(stdout);

	guard(sendto(s.udp_sockfd, &vote, sizeof(vote), 0,
		(struct sockaddr *)&dest, sizeof(dest)),
		"raft_send_vote(): error in sendto().");

}

void raft_send_heartbeat() {
	message msg;

	msg.from = s.server_id;
	msg.origin = s.server_id;
	msg.message_type = HEARTBEAT;
	msg.cur_term = s.cur_term;
	msg.server_type = LEADER;

	if(s.num_committed == 0){
		msg.pre_term = -1;
		msg.pre_index = -1;
	}else{
		msg.pre_term = s.msg_log[s.num_committed-1].cur_term;
		msg.pre_index = s.num_committed-1;
	}

	for(int i = 0; i < s.num_servers; i++){
		if(i != s.server_id){
			struct sockaddr_in dest = s.udp_socket;
			dest.sin_port = htons(PORTBASE + i);

			guard(sendto(s.udp_sockfd, &msg, sizeof(msg), 0,
				(struct sockaddr *)&dest, sizeof(dest)),
				"raft_send_heartbeat(): error in sendto().");
		}
	}

	printf("raft_send_heartbeat(): send HEARTBEAT to all followers.\n");
	fflush(stdout);
}

void raft_newmsg_to_leader(){
	message msg = s.client_msg;
	msg.message_type = NEWMSG;
	msg.server_type = s.server_type;
	msg.cur_term = s.cur_term;
	msg.commit_index = s.num_committed-1;
	
	if(s.num_committed == 0){
		msg.pre_term = -1;
		msg.pre_index = -1;
	}else{
		msg.pre_term = s.msg_log[s.num_committed-1].cur_term;
		msg.pre_index = s.num_committed-1;
	}


	guard(sendto(s.udp_sockfd, &msg, sizeof(msg), 0,
		(struct sockaddr *)&s.leader, sizeof(s.leader)),
		"raft_newmsg_to_leader(): error in sendto().");

	printf("raft_newmsg_to_leader(): client msg send to leader. msg = %s\n", msg.msg);
	printf("%d\n", ntohs(s.leader.sin_port));
	fflush(stdout);
}

void raft_leader_recv_newmsg(message msg){
	message app;
	printf("raft_leader_recv_newmsg(): msg from server%d preindex: %d preterm: %d msg: %s\n", msg.from, msg.pre_index, msg.pre_term, msg.msg);

	if(s.num_committed == 0){
		if(msg.pre_index == -1){
			s.msg_log[s.num_msg] = msg;
			s.num_msg++;
			s.num_stored = 1;
			raft_app_to_peers();
			return;
		}
		// else{
		// 	message app;

		// 	app.from = s.server_id;
		// 	app.origin = s.server_id;
		// 	app.message_type = APPENTRY;
		// 	app.server_type = s.server_type;
		// 	app.cur_term = s.cur_term;
		// 	app.pre_term = -1;
		// 	app.pre_index = -1;
		// 	app.commit_index = s.num_committed-1;
		// }
	}else{
		printf("raft_leader_recv_newmsg(): s.num_committed-1: %d msgterm: %d\n", s.num_committed-1, s.msg_log[s.num_committed-1].cur_term);
		if(s.num_committed-1 == msg.pre_index && s.msg_log[s.num_committed-1].cur_term == msg.pre_term){
			s.msg_log[s.num_msg] = msg;
			s.num_msg++;
			s.num_stored = 1;
			raft_app_to_peers();
			return;
		}else{
			// log doesn't match, decrement nextIndex and send app
			//TODO update status 
			s.status[msg.from] = msg.commit_index+1;
			app = s.msg_log[s.status[msg.from]];
		
			app.from = s.server_id;
			app.message_type = APPENTRY;
			app.server_type = s.server_type;
			app.cur_term = s.cur_term;
			
			if(s.status[msg.from] == 0){
				app.pre_term = -1;
				app.pre_index = -1;
			}else{
				app.pre_term = s.msg_log[s.status[msg.from]-1].cur_term;
				app.pre_index = s.status[msg.from]-1;
			}
			app.commit_index = s.num_committed-1;
		}
	}

	struct sockaddr_in dest = s.udp_socket;
	dest.sin_port = htons(PORTBASE + msg.from);
	

	guard(sendto(s.udp_sockfd, &app, sizeof(app), 0,
		(struct sockaddr *)&dest, sizeof(dest)),
		"raft_leader_recv_newmsg(): error in sendto().");

	printf("raft_leader_recv_newmsg(): doesn't match with pre_index, send app to sync. preindex = %d\n", app.pre_index);
	fflush(stdout);

}

void raft_app_to_peers(){
	message msg = s.msg_log[s.num_msg-1];

	msg.server_type = s.server_type;
	msg.message_type = APPENTRY;
	msg.from = s.server_id;
	msg.cur_term = s.cur_term;
	msg.commit_index = s.num_committed-1;

	s.msg_log[s.num_msg-1] = msg;

	if(s.num_committed == 0){
		msg.pre_term = -1;
		msg.pre_index = -1;
	}else{
		msg.pre_term = s.msg_log[s.num_committed-1].cur_term;
		msg.pre_index = s.num_committed-1;
	}

	for(int i = 0; i < s.num_servers; i++){
		if(i != s.server_id){
			struct sockaddr_in dest = s.udp_socket;
			dest.sin_port = htons(PORTBASE + i);

			guard(sendto(s.udp_sockfd, &msg, sizeof(msg), 0,
				(struct sockaddr *)&dest, sizeof(dest)),
				"raft_app_to_peers(): error in sendto().");
		}
	}

	printf("raft_app_to_peers(): send APPENTRY to all followers. msg = %s\n", msg.msg);
	fflush(stdout);

}

void raft_follo_recv_app(message msg){
	printf("raft_follo_recv_app(): enter msg: %s\n", msg.msg);

	// delete extra log
	if(s.num_committed > msg.commit_index+1){
		s.num_committed = msg.commit_index+1;
		s.num_msg = s.num_committed;
	}

	message app = msg;

	if(s.num_committed == 0){
		if(msg.pre_index == -1){ //success
			// s.msg_log[s.num_msg] = msg;
			// s.num_msg++;

			if(msg.pre_index == msg.commit_index){
				// new uncommitted msg NEWMSG
				printf("raft_follo_recv_app(): num_committed=0 new uncommitted msg NEWMSG\n");
				
				s.msg_log[s.num_msg] = msg;
				s.num_msg ++;
				app.from = s.server_id;
				app.origin = s.server_id;
				app.message_type = APPENTRY;
				app.server_type = s.server_type;
				app.cur_term = s.cur_term;
				app.pre_index = s.num_committed-1;
				app.pre_term = s.msg_log[s.num_committed-1].cur_term;
				app.commit_index = msg.commit_index;
				app.success = 1;
				
			}else{
				// old committed msg
				// in the process of log recovery
				printf("raft_follo_recv_app(): num_committed=0 old committed msg\n");

				s.msg_log[s.num_msg] = msg;
				s.num_committed++;
				s.num_msg++;

				app.from = s.server_id;
				app.origin = s.server_id;
				app.message_type = APPENTRY;
				app.server_type = s.server_type;
				app.cur_term = s.cur_term;
				app.pre_index = s.num_committed-1;
				app.pre_term = s.msg_log[s.num_committed-1].cur_term;
				app.commit_index = s.num_committed-1;
				app.success = 0; // differentiate from new uncommitted msg
			}
		}else{
			// pre_index > commit_index, server has missing entries
			// send commit_index
			printf("raft_follo_recv_app(): num_committed=0 server has missing entries\n");
			app.from = s.server_id;
			app.origin = s.server_id;
			app.message_type = APPENTRY;
			app.server_type = s.server_type;
			app.cur_term = s.cur_term;
			app.pre_term = -1;
			app.pre_index = -1;
			app.success = 0;
			app.commit_index = s.num_committed-1;
		}
	}else{ 
		// s.num_committed != 0
		if(msg.pre_index < s.num_committed){
			if(msg.pre_index == -1 || s.msg_log[msg.pre_index].cur_term == msg.pre_term){
				// previous log matches
				if(msg.pre_index == msg.commit_index){
					// new uncommitted msg NEWMSG
					printf("raft_follo_recv_app(): num_committed!=0 new uncommitted msg NEWMSG\n");
					// update msg.pre_index rather than s.num_msg to avoid duplicate same app from leader
					s.msg_log[msg.pre_index+1] = msg;
					s.num_msg = msg.pre_index+2;

					app.from = s.server_id;
					app.origin = s.server_id;
					app.message_type = APPENTRY;
					app.server_type = s.server_type;
					app.cur_term = s.cur_term;
					app.pre_index = s.num_committed-1;
					app.pre_term = s.msg_log[s.num_committed-1].cur_term;
					app.commit_index = msg.commit_index;
					app.success = 1;

				}else{
					// old committed msg
					// maybe in the process of log recovery or duplicate
					// if duplicate old msg, update the num_msg and resync anyway
					printf("raft_follo_recv_app(): num_committed!=0 old committed msg\n");
					s.num_msg = msg.pre_index+1;
					s.msg_log[s.num_msg] = msg;
					s.num_msg++;
					s.num_committed = s.num_msg;

					app.from = s.server_id;
					app.origin = s.server_id;
					app.message_type = APPENTRY;
					app.server_type = s.server_type;
					app.cur_term = s.cur_term;
					app.pre_index = s.num_committed-1;
					app.pre_term = s.msg_log[s.num_committed-1].cur_term;
					app.commit_index = s.num_committed-1;
					app.success = 0; // differentiate from new uncommitted msg
				}
			}else{
				// previous log doesn't matches, send unsucessful msg
				printf("raft_follo_recv_app(): num_committed!=0 previous log doesn't matches\n");
				s.num_committed--;
				s.num_msg--;

				app.from = s.server_id;
				app.origin = s.server_id;
				app.message_type = APPENTRY;
				app.server_type = s.server_type;
				app.cur_term = s.cur_term;
				app.pre_term = -1;
				app.pre_index = -1;
				app.success = 0;
				app.commit_index = s.num_committed-1;
			}
		}else{
			// pre_index > commit_index, server has missing entries
			// send commit_index
			printf("raft_follo_recv_app(): num_committed!=0 pre_index > commit_index, server has missing entries\n");
			app.from = s.server_id;
			app.origin = s.server_id;
			app.message_type = APPENTRY;
			app.server_type = s.server_type;
			app.cur_term = s.cur_term;
			app.pre_term = -1;
			app.pre_index = -1;
			app.success = 0;
			app.commit_index = s.num_committed-1;

		}
	}

	guard(sendto(s.udp_sockfd, &app, sizeof(app), 0,
		(struct sockaddr *)&s.leader, sizeof(s.leader)),
		"raft_follo_recv_app(): error in sendto().");


	printf("raft_follo_recv_app(): follower send app to leader. msgtype%d success = %d\n", app.message_type, app.success);
	printf("%d\n", ntohs(s.leader.sin_port));
	fflush(stdout);
}

void raft_leader_recv_app(message msg){
	if(msg.success == 1){
		//sucess send commit
		s.status[msg.from]++;
		printf("raft_leader_recv_app(): recv success from server%d.\n", msg.from);
		if(msg.commit_index == s.num_committed-1){
			s.num_stored++;

			if(s.num_stored > s.num_servers/2){
				s.num_committed++;
				raft_send_commit();
				// if it's the sender
				if(s.server_id == s.msg_log[s.num_committed-1].origin){
					s.climsg_committed = 1;
					raft_ack_to_proxy(s.msg_log[s.num_committed-1].seq_num, s.num_committed-1);
				}
			}
		}	
	}else if(msg.success == 0){
		// update nextIndex and send the next msg
		printf("raft_leader_recv_app(): recv no success from server%d. commit_index%d\n", msg.from, msg.commit_index);
		s.status[msg.from] = msg.commit_index+1;
		// follower recovered last msg
		if(msg.commit_index == s.num_committed-1){
			return;
		}
		printf("status: %d\n", s.status[msg.from]);

		message app = s.msg_log[s.status[msg.from]];
		app.from = s.server_id;
		app.message_type = APPENTRY;
		app.server_type = s.server_type;
		app.cur_term = s.msg_log[s.status[msg.from]].cur_term;

		if(s.status[msg.from] == 0){
			app.pre_term = -1;
			app.pre_index = -1;
		}else{
			app.pre_term = s.msg_log[s.status[msg.from]-1].cur_term;
			app.pre_index = s.status[msg.from]-1;
		}
		app.commit_index = s.num_committed-1;

		struct sockaddr_in dest = s.udp_socket;
		dest.sin_port = htons(PORTBASE + msg.from);

		guard(sendto(s.udp_sockfd, &app, sizeof(app), 0,
		(struct sockaddr *)&dest, sizeof(dest)),
		"raft_leader_recv_app(): error in sendto().");

		printf("raft_leader_recv_app(): leader send app to server%d. pre_index%d msg%s\n", msg.from, app.pre_index, app.msg);
		fflush(stdout);
	}
}

void raft_send_commit(){
	message msg;

	msg.from = s.server_id;
	msg.origin = s.msg_log[s.num_committed-1].origin;
	msg.message_type = COMMIT;
	msg.cur_term = s.cur_term;
	msg.server_type = s.server_type;
	msg.commit_index = s.num_committed-1;


	for(int i = 0; i < s.num_servers; i++){
		if(i != s.server_id){
			struct sockaddr_in dest = s.udp_socket;
			dest.sin_port = htons(PORTBASE + i);

			guard(sendto(s.udp_sockfd, &msg, sizeof(msg), 0,
				(struct sockaddr *)&dest, sizeof(dest)),
				"raft_send_commit(): error in sendto().");
		}
	}

	printf("raft_send_commit(): send COMMIT to all followers.\n");
	fflush(stdout);
}

void raft_follo_recv_commit(message msg){
	if(msg.commit_index == s.num_msg-1){
		printf("raft_follo_recv_commit: Committed commit_index%d.\n", msg.commit_index);
		s.num_committed++;
		// if it's the sender
		if(s.server_id == msg.origin){
			s.climsg_committed = 1;
			// TODO
			// send ack to proxy
			raft_ack_to_proxy(s.client_msg.seq_num, s.num_committed-1);
		}
	}else{
		return;
	}
}

void raft_send_sync_req() {
	message app;

	app.from = s.server_id;
	app.origin = s.server_id;
	app.message_type = APPENTRY;
	app.server_type = s.server_type;
	app.cur_term = s.cur_term;
	app.pre_term = -100;
	app.pre_index = -100;
	app.success = 0;
	app.commit_index = s.num_committed-1;

	guard(sendto(s.udp_sockfd, &app, sizeof(app), 0,
		(struct sockaddr *)&s.leader, sizeof(s.leader)),
		"raft_send_sync_req(): error in sendto().");


	printf("raft_send_sync_req(): follower send sync to leader.\n");
	fflush(stdout);

}


void raft_recv() {
	fd_set sockfd_set;
	struct timeval timeout = {5, 0}; // timeout for select

	int smax = s.udp_sockfd > s.tcp_sockfd ? s.udp_sockfd : s.tcp_sockfd;
	// int smax = s.udp_sockfd;

	signal(SIGALRM, alarm_handler);
	alarm(s.timeout);
	while (1) {
	    FD_ZERO(&sockfd_set);
	    FD_SET(s.tcp_sockfd, &sockfd_set);
	    FD_SET(s.udp_sockfd, &sockfd_set);

	    int retval = select(smax + 1, &sockfd_set, NULL, NULL, &timeout);
	    if (retval > 0) { // sockets readable (incoming msgs)
	        if (FD_ISSET(s.tcp_sockfd, &sockfd_set)) {
				// printf("here tcp\n");
				char buf[MSGLEN];
				int num_read = guard(read(s.tcp_sockfd, buf, MSGLEN),
					"raft_recv(): error in tcp_sockfd read().");
				if (num_read > 0) {
					if (buf[0] == 'g') { // get chatLog
						printf("raft_recv(): get chatLog.\n");
						fflush(stdout);
						raft_msg_to_proxy();
					} else if (buf[0] == 'c') { // crash
						printf("raft_recv(): server %d crashes.\n", s.server_id);
						fflush(stdout);
						exit(0);
					} else if (buf[0] == 'm') { // msg
						raft_msg_from_proxy(buf, num_read);
					}
				}
	        }
			if (FD_ISSET(s.udp_sockfd, &sockfd_set)) {

				message msg;
				int num_read = guard(read(s.udp_sockfd, (char *)&msg, sizeof(msg)),
					"raft_recv(): error in udp_sockfd read().");
				if (num_read > 0) {
					if(msg.from == s.server_id) continue;
					printf("raft_recv(): receive msg from server%d, servertype%d. msgtype%d, term%d\n", msg.from, msg.server_type, msg.message_type, msg.cur_term);
					printf("curserver: servertype%d cur_term%d climsg_committed%d\n", s.server_type, s.cur_term, s.climsg_committed);
					if(s.cur_term < msg.cur_term){
						// printf("here udp cur_term > 0\n");
						alarm(TIMEOUT); // reset timer
						s.cur_term = msg.cur_term;
						s.server_type = FOLLOWER;
						s.granted = 0;
						s.num_votes = 0;
					}
					// if receive msg from leader, reset timer and update leader info
					if(msg.server_type == LEADER){
						alarm(TIMEOUT); // reset timer
		
						s.server_type = FOLLOWER;
						s.leader = s.udp_socket;
						s.leader.sin_port = htons(PORTBASE + msg.from);
						printf("from leader msgfrom:%d leader no. %d\n", msg.from, ntohs(s.leader.sin_port));
					}

					if(s.server_type == LEADER){
						printf("here udp leader\n");
						if(msg.message_type == REQVOTE){
							raft_send_heartbeat();
						}else if(msg.message_type == NEWMSG){
							// only one msg send one time
							if(s.num_msg == s.num_committed){	
								raft_leader_recv_newmsg(msg);
							}else{
								// receive duplicate NEWMSG, resend APP to peers
								if(msg.from == s.msg_log[s.num_msg-1].origin){
									raft_app_to_peers();
								}
							}
						}else if(msg.message_type == APPENTRY){
							raft_leader_recv_app(msg);
						}
					}else if(s.server_type == CANDIDATE){
						printf("here udp candidate\n");
						// printf("raft_recv(): haha term%d from server%d. msgtype: %d\n", s.cur_term, msg.origin, msg.message_type);
						if(msg.message_type == VOTE){
							printf("raft_recv(): term%d get VOTE from server%d. msgtype: %d\n", s.cur_term, msg.origin, msg.message_type);
							s.num_votes++;
							fflush(stdout);
							if(s.num_votes > s.num_servers/2){
								s.server_type = LEADER;
								raft_send_heartbeat();
								for (int i = 0; i < s.num_servers; i++)
									s.status[i] = s.num_committed;
							}
						}
					}else if(s.server_type == FOLLOWER){
						printf("here udp follower, msg.message_type = %d\n", msg.message_type);
						if(msg.message_type == REQVOTE){
							if(s.granted == 0){
								raft_send_vote(msg);
								alarm(TIMEOUT); // reset timer
							}
						}else if(msg.message_type == APPENTRY && msg.server_type == LEADER){
							raft_follo_recv_app(msg);
						}else if(msg.message_type == COMMIT && msg.server_type == LEADER){
							raft_follo_recv_commit(msg);
						}else if(msg.message_type == HEARTBEAT){
							raft_send_sync_req();
						}
					}
					fflush(stdout);

				}
	        }
	    } else if (retval == 0) { // timeout & receive nothing, send randomly
			
			if(s.server_type == CANDIDATE){
				printf("raft_recv(): resend REQUEST VOTE\n");
				raft_send_requestvote();
			}else if(s.server_type == FOLLOWER){
				// resend new msg to leader
				if(s.climsg_committed == 0){
					printf("raft_recv(): resend raft_newmsg_to_leader()\n");
					raft_newmsg_to_leader();
				}
			}else if(s.server_type == LEADER){
				// resend its own new msg
				if(s.climsg_committed == 0 && (s.num_msg == s.num_committed)){
					printf("raft_recv(): resend leader's climsg again raft_app_to_peers()\n");
					s.msg_log[s.num_msg] = s.client_msg;
					s.num_msg++;
					s.num_stored = 1;
					raft_app_to_peers();
				}else if(s.num_msg != s.num_committed){
					// resend
					raft_app_to_peers();
				}
			}
			fflush(stdout);
			
		} else if (retval < 0) {
			if(errno == EINTR){
				perror("raft_recv(): EINTR");
				continue;
			}else{
				perror("raft_recv(): error in select().");
				exit(1);
			}
			fflush(stdout);
	    }
	}
}

int main(int argc, char *argv[]) {
	// ./test 0 4 1234  vs. 0 start 4 1234 (./process 0 4 1234)
	// 0 msg 1 hi; 0 msg 2 i am bob;
	// 0 crash
	// 0 get chatLog
	// server_id starts from 0,1,2,3
	int server_id = atoi(argv[1]);
	int num_servers = atoi(argv[2]);
	int port = atoi(argv[3]);
	

	char filename[19];
	sprintf(filename, "./DEBUG/debug%d.txt", server_id);
	setbuf(stdout, NULL);
	stdout = freopen(filename, "w", stdout);

	raft_init(server_id, port, num_servers);
	// if(server_id == 1){
	// 	stub_test_msg(1, 1);
	// }
	raft_recv();
}
