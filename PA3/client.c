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
int port, reqnum;

typedef struct
{
	char payload[MAX_PAYLOAD_SIZE];
} packet_form;

int main(int argc, char* argv[])
{
	int i, thrnum;
	pthread_t *tid;

	if (argc != 5) {                           //invalid arguments
		printf("ERROR: invalid arguments\n");
		exit(0);
	}
	host = argv[1];
	port = atoi(argv[2]);
	thrnum = atoi(argv[3]);
	reqnum = atoi(argv[4]);   //set information

	tid = (pthread_t *)calloc(thrnum, sizeof(pthread_t));
	for (i = 0; i < thrnum; i++) {
		pthread_create(&tid[i], NULL, thread, NULL);
	}
	for (i = 0; i < thrnum; i++) {
		pthread_join(tid[i], NULL);          //join thread
	}
	
	free(tid);
	exit(0);
}

void *thread(void *vargp)
{
	int res, i, client_socket;
	struct sockaddr_in server_addr;

	memset(&server_addr, 0, sizeof(server_addr));   //set socket
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = inet_addr(host);

	packet_form packet_org;
	packet_form *packet;
	packet = &packet_org;

	char finish[21];         //finish socket's payload
	memset(finish, '0', 20);  

	for (i = 0; i < reqnum; i++) {

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

		memset(packet, 0, sizeof(packet_form));
		sprintf(packet->payload, "GET /index.html/ HTTP/1.1\nHost: index.html");
		write(client_socket, packet, strlen(packet->payload) + 1);       //send http request

		memset(packet, 0, sizeof(packet_form));
		read(client_socket, packet, sizeof(packet_form));      //receive http response

		while(memcmp(packet->payload, finish, 20)) {         //read until finish socket
			memset(packet, 0, sizeof(packet_form));
			read(client_socket, packet, sizeof(packet_form));
		}                                                 //read index.html

		close(client_socket);              //close socket
	}
	return ;
}