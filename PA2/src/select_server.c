#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#define MAX_PAYLOAD_SIZE 1024  //maximum size of each socket's data

typedef struct
{
	uint8_t version;
	uint8_t user_ID;
	uint16_t sequence;
	uint16_t length;
	uint16_t command;
} Header;

typedef struct
{
	Header header;
	char payload[MAX_PAYLOAD_SIZE];
} packet_form;

typedef struct
{
	int maxfd;
	fd_set read_set;
	fd_set write_set;
	fd_set ready_set;
	int readynum;
	int maxi;
	int client_sock[FD_SETSIZE];
	int comstate[FD_SETSIZE];
	int seqstate[FD_SETSIZE];
	char *data[FD_SETSIZE];
} pool_t;

int main(int argc, char* argv[])
{
	int res, i, port, state, optval = 1;
	uint16_t seq;
	int server_socket;
	struct sockaddr_in server_addr;
	int client_socket, addr_size;
	struct sockaddr_in client_addr;
	pool_t pool;

	if (argc != 1) {                              //invalid argument
		printf("ERROR: invalid arguments, no need argument\n");
		exit(0);
	}
	port = 12345;

	server_socket = socket(AF_INET, SOCK_STREAM, 0);  //make socket
	if (server_socket == -1) {
		printf("ERROR: socket 생성 실패\n");
		exit(0);
	}
	if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int)) < 0) {
		printf("ERROR: socket option set 실패\n");
		exit(0);
	}
	if ((optval = fcntl(server_socket, F_GETFL)) < 0) {
		printf("ERROR: get socket option 실패\n");
	}
	optval = (optval | O_NONBLOCK);
	if (fcntl(server_socket, F_SETFL, optval)) {
		printf("ERROR: set socket option 실패\n");
	}

	memset(&server_addr, 0, sizeof(server_addr));     //set socket
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	//bind socket
	res = bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
	if (res == -1) {
		printf("ERROR: bind 실패\n");
		exit(0);
	}

	res = listen(server_socket, 5);       //listen socket
	if (res == -1) {
		printf("ERROR: listen 실패\n");
		exit(0);
	}
	addr_size = sizeof(client_addr); 

	pool.maxi = -1;
	for (i = 0; i < FD_SETSIZE; i++) {
		pool.client_sock[i] = -1;
		pool.comstate[i] = -1;
		pool.seqstate[i] = 0;
		pool.data[i] = NULL;
	}
	pool.maxfd = server_socket;
	FD_ZERO(&(pool.read_set));
	FD_SET(server_socket, &(pool.read_set));   //set pool informaion

	while(1) {
		pool.ready_set = pool.read_set;
		pool.readynum = select(pool.maxfd + 1, &(pool.ready_set), &(pool.write_set), NULL, NULL);

		if (FD_ISSET(server_socket, &(pool.ready_set))) {
			client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_size);
			if (client_socket == -1) {
				printf("ERROR: client accept 실패\n");
				exit(0);
			}
			pool.readynum--;
			for (i = 0; i < FD_SETSIZE; i++) {
				if (pool.client_sock[i] < 0) {
					pool.client_sock[i] = client_socket;
					pool.comstate[i] = 1;
					FD_SET(client_socket, &(pool.read_set));
					if (client_socket > pool.maxfd) pool.maxfd = client_socket;
					if (i > pool.maxi) pool.maxi = i;
					break;
				}
			}
			if (i == FD_SETSIZE) {
				printf("ERROR: Too many clients\n");
				exit(0);
			}
		}

		packet_form *packet;
		packet = (packet_form *)calloc(1, sizeof(packet_form));
		
		for (i = 0; (i <= pool.maxi) && (pool.readynum > 0); i++) {
			client_socket = pool.client_sock[i];
			state = pool.comstate[i];

			if ((client_socket > 0) && (FD_ISSET(client_socket, &(pool.ready_set)))) {
				pool.readynum--;
				switch (state) {
					case 1 : //read command 1 and write 2						
						read(client_socket, packet, sizeof(packet_form));   //receive client hello
						res = check_packet(packet);                 
						if (res == -1 || packet->header.command != htons(0x0001)) {
							printf("ERROR: invalid version or user ID or command\n");
							packet->header.version = 0x04;
							packet->header.user_ID = 0x08;;
							packet->header.length = htons(sizeof(Header) + MAX_PAYLOAD_SIZE);
							packet->header.command = htons(0x0005);
							write(client_socket, packet, ntohs(packet->header.length));
							close(client_socket);
							FD_CLR(client_socket, &(pool.read_set));
							pool.client_sock[i] = -1;
							break;
						}
						pool.seqstate[i] = ntohs(packet->header.sequence);

						packet->header.command = htons(0x0002);
						packet->header.length = htons(sizeof(Header) + MAX_PAYLOAD_SIZE);
						write(client_socket, packet, ntohs(packet->header.length));   //send server hello
						memset(packet, 0, sizeof(packet_form));
						pool.comstate[i] = 2;
						break;

					case 2 : //read command 3 
						read(client_socket, packet, sizeof(packet_form));
						res = check_packet(packet);
						if (res == -1 || packet->header.command != htons(0x0003) || packet->header.sequence != htons(pool.seqstate[i] + 1)) {
							printf("ERROR: invalid version or user ID or command or sequence number\n");
							packet->header.version = 0x04;
							packet->header.user_ID = 0x08;;
							packet->header.length = htons(sizeof(Header) + MAX_PAYLOAD_SIZE);
							packet->header.command = htons(0x0005);
							write(client_socket, packet, ntohs(packet->header.length));
							close(client_socket);
							FD_CLR(client_socket, &(pool.read_set));
							pool.client_sock[i] = -1;
							break;
						}
						pool.seqstate[i]++;

						pool.data[i] = (char *)calloc(1, MAX_PAYLOAD_SIZE);
						memcpy(pool.data[i], packet->payload, strlen(packet->payload));
						memset(packet, 0, sizeof(packet_form));
						pool.comstate[i] = 3;
						break;

					case 3 : //read comand 4 and finish
						read(client_socket, packet, sizeof(packet_form));
						res = check_packet(packet);
						if (res == -1 || packet->header.command != htons(0x0004) || packet->header.sequence != htons(pool.seqstate[i] + 1)) {
							printf("ERROR: invalid version or user ID or command or sequence number\n");
							packet->header.version = 0x04;
							packet->header.user_ID = 0x08;;
							packet->header.length = htons(sizeof(Header) + MAX_PAYLOAD_SIZE);
							packet->header.command = htons(0x0005);
							write(client_socket, packet, ntohs(packet->header.length));
							free(pool.data[i]);
							close(client_socket);
							FD_CLR(client_socket, &(pool.read_set));
							pool.client_sock[i] = -1;
							break;
						}

						FILE *fp;
						fp = fopen(packet->payload, "w");
						fprintf(fp, "%s", pool.data[i]);

						fclose(fp);
						close(client_socket);
						FD_CLR(client_socket, &(pool.read_set));
						pool.client_sock[i] = -1;
						free(pool.data[i]);
						break;
				}
			}
		}
		free(packet);
	}
}

int check_packet(packet_form *packet) 
{
	if (packet->header.version != 0x04) return -1;
	if (packet->header.user_ID != 0x08) return -1;
	return 0;
}