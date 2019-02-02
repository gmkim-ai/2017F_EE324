#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#define MAX_PAYLOAD_SIZE 1024  //maximum size of each socket's data
void *thread(void *vargp);
char *host;                 //global variable for thread

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

int main(int argc, char* argv[])
{
	int i, reqnum;
	pthread_t *tid;

	if (argc != 3) {                           //invalid arguments
		printf("ERROR: invalid arguments\n");
		exit(0);
	}
	host = argv[1];
	reqnum = atoi(argv[2]);
	int *iarr;

	iarr = (int *)calloc(reqnum, sizeof(int));
	for (i = 0; i < reqnum; i++) {
		iarr[i] = i;
	}

	tid = (pthread_t *)calloc(reqnum, sizeof(pthread_t));
	for (i = 0; i < reqnum; i++) {
		pthread_create(&tid[i], NULL, thread, &iarr[i]);
		sleep(0.1);
	}
	for (i = 0; i < reqnum; i++) {
		pthread_join(tid[i], NULL);
	}

	free(iarr);
	free(tid);
	exit(0);
}

int check_packet(packet_form *packet) 
{
	if (packet->header.version != 0x04) return -1;
	if (packet->header.user_ID != 0x08) return -1;
	return 0;
}

void *thread(void *vargp)
{
	int myid = *((int *)vargp);
	int res, port = 12345;
	uint16_t seq;
	int client_socket;
	struct sockaddr_in server_addr;

	memset(&server_addr, 0, sizeof(server_addr));   //set socket
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = inet_addr(host);

	client_socket = socket(AF_INET, SOCK_STREAM, 0); //make socket
	if (client_socket == -1) {
		printf("socket 생성 실패\n");
		exit(0);
	}
	res = connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
	if (res == -1) {
		printf("connect 실패\n");
		exit(0);
	}

	packet_form *packet;
	packet = (packet_form *)calloc(1, sizeof(packet_form));
	srand(time(NULL));
	seq = (rand() % 4000) + 4000;

	packet->header.version = 0x04;
	packet->header.user_ID = 0x08;
	packet->header.sequence = htons(seq);
	packet->header.length = htons(sizeof(Header) + MAX_PAYLOAD_SIZE);
	packet->header.command = htons(0x0001);
	write(client_socket, packet, ntohs(packet->header.length));    //send client hello

	memset(packet, 0, sizeof(packet_form));
	read(client_socket, packet, sizeof(packet_form));
	res = check_packet(packet);
	if (res == -1 || packet->header.command != htons(0x0002) || packet->header.sequence != htons(seq)) {
		printf("ERROR: invalid version or user ID or command or sequence number\n");
		packet->header.version = 0x04;
		packet->header.user_ID = 0x08;;
		packet->header.length = htons(sizeof(Header) + MAX_PAYLOAD_SIZE);
		packet->header.command = htons(0x0005);
		write(client_socket, packet, ntohs(packet->header.length));
		free(packet);
		close(client_socket);
		exit(0);
	}
	seq++;

	sprintf(packet->payload, "I am a thread %d", myid + 1);
	packet->header.command = htons(0x0003);
	packet->header.sequence = htons(seq);
	packet->header.length = htons(sizeof(Header) + MAX_PAYLOAD_SIZE);
	write(client_socket, packet, ntohs(packet->header.length));
	seq++;
	memset(packet->payload, 0, MAX_PAYLOAD_SIZE);

	packet->header.sequence = htons(seq);
	packet->header.length = htons(sizeof(Header) + MAX_PAYLOAD_SIZE);
	packet->header.command = htons(0x0004);
	
	sprintf(packet->payload, "%d.txt", myid + 1);	
	write(client_socket, packet, ntohs(packet->header.length));	

	free(packet);
	close(client_socket);              //close socket
	return NULL;
}