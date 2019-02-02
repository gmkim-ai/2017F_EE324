#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
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

int main(int argc, char* argv[])
{
	int res, port, count, size;
	uint16_t seq;
	int server_socket;
	struct sockaddr_in server_addr;
	int client_socket, addr_size;
	struct sockaddr_in client_addr;

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

	addr_size = sizeof(client_addr);       //accept socket
	client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_size);
	if (client_socket == -1) {
		printf("ERROR: client accept 실패\n");
		exit(0);
	}

	//recieve data from client
	packet_form *packet;
	packet = (packet_form *)calloc(1, sizeof(packet_form));

	close(server_socket);                   //process don't need to listen

	read(client_socket, packet, sizeof(packet_form));   //receive client hello
	res = check_packet(packet);                 
	if (res == -1 || packet->header.command != htons(0x0001)) {
		printf("ERROR: invalid version or user ID or command\n");
		packet->header.version = 0x04;
		packet->header.user_ID = 0x08;;
		packet->header.length = htons(sizeof(Header) + MAX_PAYLOAD_SIZE);
		packet->header.command = htons(0x0005);
		write(client_socket, packet, ntohs(packet->header.length));
		free(packet);
		close(client_socket);
		exit(0);
	}
	seq = ntohs(packet->header.sequence);

	packet->header.command = htons(0x0002);
	packet->header.length = htons(sizeof(Header) + MAX_PAYLOAD_SIZE);
	write(client_socket, packet, ntohs(packet->header.length));   //send server hello

	memset(packet, 0, sizeof(packet_form));
	read(client_socket, packet, sizeof(packet_form));
	res = check_packet(packet);
	if (res == -1 || packet->header.command != htons(0x0003) || packet->header.sequence != htons(seq + 1)) {
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

	char *data;
	data = (char *)calloc(1, MAX_PAYLOAD_SIZE);
	memcpy(data, packet->payload, strlen(packet->payload));
	count = 1;
	size = 1;

	memset(packet, 0, sizeof(packet_form));
	read(client_socket, packet, sizeof(packet_form));

	while(packet->header.command == htons(0x0003)) {
		res = check_packet(packet);
		if (res == -1 || packet->header.sequence != htons(seq + 1)) {
			printf("ERROR: invalid version or user ID or sequence number\n");
			packet->header.version = 0x04;
			packet->header.user_ID = 0x08;;
			packet->header.length = htons(sizeof(Header) + MAX_PAYLOAD_SIZE);
			packet->header.command = htons(0x0005);
			write(client_socket, packet, ntohs(packet->header.length));
			free(packet);
			free(data);
			close(client_socket);
			exit(0);
		}
		seq++;
		if (count == size) {
			data = realloc(data, MAX_PAYLOAD_SIZE * size * 2);
			memset(data + MAX_PAYLOAD_SIZE * size, 0, MAX_PAYLOAD_SIZE * size);
			size *= 2;
		}

		memcpy( data + count * (MAX_PAYLOAD_SIZE - 1), packet->payload, strlen(packet->payload));
		count++;

		memset(packet, 0, sizeof(packet_form));
		read(client_socket, packet, sizeof(packet_form));
	}                //finish 0x0003

	res = check_packet(packet);
	if (res == -1 || packet->header.command != htons(0x0004) || packet->header.sequence != htons(seq + 1)) {
		printf("ERROR: invalid version or user ID or command or sequence number\n");
		packet->header.version = 0x04;
		packet->header.user_ID = 0x08;;
		packet->header.length = htons(sizeof(Header) + MAX_PAYLOAD_SIZE);
		packet->header.command = htons(0x0005);
		write(client_socket, packet, ntohs(packet->header.length));
		free(packet);
		free(data);
		close(client_socket);
		exit(0);
	}

	FILE *fp;
	fp = fopen(packet->payload, "w");
	fprintf(fp, "%s", data);

	fclose(fp);
	close(client_socket);
	free(packet);
	free(data);
	return 0;
}

int check_packet(packet_form *packet) 
{
	if (packet->header.version != 0x04) return -1;
	if (packet->header.user_ID != 0x08) return -1;
	return 0;
}