#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#define MAX_PAYLOAD_SIZE 1024  //maximum size of each socket's data

typedef struct
{
	uint32_t length;
	uint32_t node_ID;
	uint32_t MSG_type;    //Header
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
	int client_sock[FD_SETSIZE];        //pool for connect other child
} pool_t;

int is_binary(char *data, int len);     //check this is binary file or text file
void sigint_handler(int signo);
packet_form *packet;
pool_t pool;
int server_socket;

int main(int argc, char* argv[])
{
	int res, i, port, s_port, ID, optval = 1;
	struct in_addr temp_addr;
	int super_socket;
	struct sockaddr_in server_addr, super_addr;
	int client_socket, addr_size;
	struct sockaddr_in client_addr;
	char *s_ip = NULL, *name, *rename, *temp;
	char port_op[3] = "-p";
	char s_port_op[9] = "--s_port";
	char s_ip_op[7] = "--s_ip";
	char divi[3] = " \n";
	char str[100];
	DIR *dir;
	FILE *fp;
	char path[100];
	struct dirent *ent;
	struct stat *file_info;
	signal(SIGINT, sigint_handler);
	packet = (packet_form *)calloc(1, sizeof(packet_form));

	if (argc != 7) {                              //invalid argument
		printf("ERROR: invalid arguments, need 6 arguments\n");
		exit(0);
	}
	for (i = 1; i < argc; i = i + 2) {
		if (!strcmp(argv[i], port_op)) port = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], s_port_op)) s_port = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], s_ip_op)) s_ip = argv[i + 1];
		else printf("ERROR: invalid option\n");
	}

	server_socket = socket(AF_INET, SOCK_STREAM, 0);  //make server socket
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

	memset(&super_addr, 0, sizeof(super_addr));   //set super socket that connect with super node
	super_addr.sin_family = AF_INET;
	super_addr.sin_port = htons(s_port);
	super_addr.sin_addr.s_addr = inet_addr(s_ip);
	super_socket = socket(AF_INET, SOCK_STREAM, 0); //make super socket
	if (super_socket == -1) {
		printf("socket 생성 실패\n");
		exit(0);
	}
	res = connect(super_socket, (struct sockaddr *)&super_addr, sizeof(super_addr));
	if (res == -1) {
		printf("connect 실패\n");
		exit(0);
	}
	sprintf(packet->payload, "%d", port);
	packet->header.MSG_type = htonl(0x00000010);
	packet->header.length = htonl(sizeof(Header) + strlen(packet->payload));
	packet->header.node_ID = htonl(0);
	write(super_socket, packet, ntohl(packet->header.length));   //SEND HELLO FROM CHILD with port

	pool.maxi = -1;
	for (i = 0; i < FD_SETSIZE; i++) {
		pool.client_sock[i] = -1;
	}
	pool.maxfd = server_socket;
	FD_ZERO(&(pool.read_set));
	FD_SET(server_socket, &(pool.read_set));   //set pool informaion
	pool.client_sock[0] = super_socket;
	FD_SET(super_socket, &(pool.read_set));        //super socket should be set in read set for communicate with super node
	if (super_socket > pool.maxfd) pool.maxfd = super_socket;
	if (i > pool.maxi) pool.maxi = i;

	while(1) {
		pool.ready_set = pool.read_set;
		pool.readynum = select(pool.maxfd + 1, &(pool.ready_set), &(pool.write_set), NULL, NULL);    //select()

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
		
		for (i = 0; (i <= pool.maxi) && (pool.readynum > 0); i++) {
			client_socket = pool.client_sock[i];

			if ((client_socket > 0) && (FD_ISSET(client_socket, &(pool.ready_set)))) {
				pool.readynum--;
				memset(packet, 0, sizeof(packet_form));
				read(client_socket, packet, sizeof(Header));

				switch (ntohl(packet->header.MSG_type)) {
					case 0x00000011:                 //HELLO from SUPER
						ID = ntohl(packet->header.node_ID);       //RECV ID info from super node, this is my ID for now.
						memset(packet->payload, 0, MAX_PAYLOAD_SIZE);

						dir = opendir("./data");                //open ./data dir and send this info to super node
						while ((ent = readdir(dir)) != NULL) {
							sprintf(path, "./data/%s", ent->d_name);
							stat(path, file_info);
							sprintf(str, "%s %ld ", ent->d_name, file_info->st_size);
							strcat(packet->payload, str);
						} 
						closedir(dir);
						packet->header.MSG_type = htonl(0x00000020);     //SEND FILE INFO
						packet->header.length = htonl(sizeof(Header) + strlen(packet->payload));
						packet->header.node_ID = htonl(ID);
						write(client_socket, packet, ntohl(packet->header.length));

						if (fork() == 0) {            //Use 2 process for input some "get" command and call select for other child node's REQUEST
							time_t start;           //this process is charge of "get" command from STDIN
							double res;
							while (1) {
								memset(packet->payload, 0, MAX_PAYLOAD_SIZE);
								printf("Input command: ");
								memset(str, 0, 100);
								fgets(str, sizeof(str), stdin);      //input command
								start = clock();
								temp = strtok(str, divi);
								name = strtok(NULL, divi);          //file name
								rename = strtok(NULL, divi);     
								sprintf(packet->payload, "%s", name);
								packet->header.MSG_type = htonl(0x00000030);     //SEND SEARCH QUERY to super node
								packet->header.length = htonl(sizeof(Header) + strlen(packet->payload));
								packet->header.node_ID = htonl(ID);
								write(super_socket, packet, ntohl(packet->header.length));

								memset(packet->payload, 0, MAX_PAYLOAD_SIZE);
								read(super_socket, packet, sizeof(Header));
								if (ntohl(packet->header.MSG_type) != 0x00000031) {
									printf("ERROR: SEARCH ANS FAIL\n");
								}
								else {
									read(super_socket, packet->payload, ntohl(packet->header.length) - sizeof(Header));
									temp = strtok(packet->payload, divi);        //Get port and address information in payload
									port = atoi(strtok(NULL, divi));
									res = (double)(clock() - start) / CLOCKS_PER_SEC;
									if (res > 2) {                         //If this take more than 2 secs, than print error message
										printf("WRONG! Network Transaction is handled more than 2 sec\n");
									}
									memset(&client_addr, 0, sizeof(client_addr));   //set client socket
									client_addr.sin_family = AF_INET;
									client_addr.sin_port = htons(port);
									client_addr.sin_addr.s_addr = inet_addr(temp);

									client_socket = socket(AF_INET, SOCK_STREAM, 0); //make client socket
									if (client_socket == -1) {
										printf("socket 생성 실패\n");
										exit(0);
									}
									res = connect(client_socket, (struct sockaddr *)&client_addr, sizeof(client_addr));
									if (res == -1) {
										printf("connect 실패\n");
										exit(0);
									}
									memset(packet->payload, 0, MAX_PAYLOAD_SIZE);
									sprintf(packet->payload, "%s", name);        //SEND name for request to that child node
									packet->header.MSG_type = htonl(0x00000040);
									packet->header.length = htonl(sizeof(Header) + strlen(packet->payload));
									packet->header.node_ID = htonl(ID);
									write(client_socket, packet, ntohl(packet->header.length));   //FILE REQ

									memset(packet->payload, 0, MAX_PAYLOAD_SIZE);
									read(client_socket, packet, sizeof(Header) + 1);       //If this is text file, then payload will be 't'. If not, payload is 'b'
									if (ntohl(packet->header.MSG_type) == 0x00000040 && packet->payload[0] == 't') {  //this is text file
										sprintf(path, "./download/%s", rename);
										fp = fopen(path, "w");               //open file with "w"
										read(client_socket, packet, sizeof(packet_form));
										while (ntohl(packet->header.MSG_type) != 0x00000041) {
											fprintf(fp, "%s", packet->payload);
											memset(packet->payload, 0, MAX_PAYLOAD_SIZE);
											read(client_socket, packet, sizeof(packet_form));
										}
										fclose(fp);
									}
									else if (ntohl(packet->header.MSG_type) == 0x00000040 && packet->payload[0] == 'b') {   //this is binary file
										sprintf(path, "./download/%s", rename);
										fp = fopen(path, "wb");               //open file with "wb"
										read(client_socket, packet, sizeof(packet_form));
										while (ntohl(packet->header.MSG_type) != 0x00000041) {
											fwrite(packet->payload, 1, ntohl(packet->header.length) - sizeof(Header), fp);    //use fwrite function
											memset(packet->payload, 0, MAX_PAYLOAD_SIZE);
											read(client_socket, packet, sizeof(packet_form));
										}
										fclose(fp);
									}
									else printf("ERROR: FILE RES FAIL\n");
									close(client_socket);
								}
							}
						}
						break;

					case 0x00000021:           //FILE INFO RECV SUCCESS
						read(client_socket, packet->payload, ntohl(packet->header.length) - sizeof(Header));   //check What I send
						FD_CLR(super_socket, &(pool.read_set));               //close this super socket because no more need to read this socket in this process.
						pool.client_sock[0] = -1;
						close(super_socket);
						break;
					case 0x00000022:          //FILE INFO RECV ERROR
						printf("ERROR: FILE INFO RECV ERROR\n");
						FD_CLR(super_socket, &(pool.read_set));             //close this super socket because no more need to read this socket in this process.
						pool.client_sock[0] = -1;
						close(super_socket);
						break;

					case 0x00000040:
						read(client_socket, packet->payload, ntohl(packet->header.length) - sizeof(Header));
						sprintf(path, "./data/%s", packet->payload);
						fp = fopen(path, "rb");               //open this filename with rb, and check this is binary file or text file
						if (fp == NULL) {
							packet->header.MSG_type = htonl(0x00000042);
							packet->header.length = htonl(sizeof(Header));
							packet->header.node_ID = htonl(ID);
							write(client_socket, packet, ntohl(packet->header.length));
						}
						else {
							res = fread(packet->payload, 1, MAX_PAYLOAD_SIZE, fp);
							fclose(fp);
							if (is_binary(packet->payload, res)) {
								fp = fopen(path, "rb");            //open file with "rb"
								memset(packet->payload, 0, MAX_PAYLOAD_SIZE);
								sprintf(packet->payload, "b");
								packet->header.MSG_type = htonl(0x00000040);
								packet->header.length = htonl(sizeof(Header) + 1);
								packet->header.node_ID = htonl(ID);
								write(client_socket, packet, sizeof(Header) + 1);       //send packet, payload is "b"
								memset(packet->payload, 0, MAX_PAYLOAD_SIZE);
								while ((res = fread(packet->payload, 1, MAX_PAYLOAD_SIZE, fp)) == MAX_PAYLOAD_SIZE) {
									packet->header.length = htonl(sizeof(Header) + res);
									write(client_socket, packet, sizeof(packet_form));   //use fread for read binary file
									memset(packet->payload, 0, MAX_PAYLOAD_SIZE);
								}
								packet->header.length = htonl(sizeof(Header) + res);
								write(client_socket, packet, sizeof(packet_form));
								packet->header.MSG_type = htonl(0x00000041);
								packet->header.length = htonl(sizeof(Header));
								write(client_socket, packet, ntohl(packet->header.length));
								fclose(fp);
							}
							else {
								fp = fopen(path, "r");               //open file with "r"
								memset(packet->payload, 0, MAX_PAYLOAD_SIZE);
								sprintf(packet->payload, "t");
								packet->header.MSG_type = htonl(0x00000040);
								packet->header.length = htonl(sizeof(Header) + 1);
								packet->header.node_ID = htonl(ID);
								write(client_socket, packet, sizeof(Header) + 1);     //send packet, payload is "t"
								memset(packet->payload, 0, MAX_PAYLOAD_SIZE);
								while (fgets(packet->payload, MAX_PAYLOAD_SIZE, fp) != NULL) {
									write(client_socket, packet, sizeof(packet_form));
									memset(packet->payload, 0, MAX_PAYLOAD_SIZE);
								}
								packet->header.MSG_type = htonl(0x00000041);
								packet->header.length = htonl(sizeof(Header));
								write(client_socket, packet, ntohl(packet->header.length));
								fclose(fp);
							}
						}
						FD_CLR(client_socket, &(pool.read_set));    //close this socket because no need to communicate each other.
						pool.client_sock[i] = -1;
						close(client_socket);
						break;

				}
			}
		}
	}
}

int is_binary(char *data, int len)
{
	return memchr(data, '\0', len) != NULL;          //If there is some NUL, then this is binary file.
}

void sigint_handler(int signo)       //signal handle for Ctrl + C 
{
	int i;
	free(packet);
	for (i = 0; i < FD_SETSIZE; i++) {
		if (pool.client_sock[i] > 0) {
			close(pool.client_sock[i]);
		}                            //close all open socket in pool
	}
	close(server_socket);
	while (waitpid(-1, 0, WNOHANG) > 0)    //wait for exit other process, avoid to become zombie process
		;
	exit(0);
}