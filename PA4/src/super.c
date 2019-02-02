#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <signal.h>
#define MAX_PAYLOAD_SIZE 1024  //maximum size of each socket's data

typedef struct
{
	uint32_t length;
	uint32_t node_ID;
	uint32_t MSG_type;         //Header
} Header;

typedef struct
{
	Header header;
	char payload[MAX_PAYLOAD_SIZE];
} packet_form;

typedef struct           //pool for select
{
	int maxfd;
	fd_set read_set;
	fd_set write_set;
	fd_set ready_set;
	int readynum;
	int maxi;
	int client_sock[FD_SETSIZE];
	struct in_addr client_sock_addr[FD_SETSIZE];
	int client_sock_port[FD_SETSIZE];
	int client_sock_ID[FD_SETSIZE];
} pool_t;

typedef struct Node      //file information will save in this form 
{
	int node_ID;
	struct in_addr node_addr;
	int node_port;
	char *file_name; 
	int file_size;
	struct Node *next;
} node_t;

typedef struct          //hash table's each element is list
{
	node_t *front;
	node_t *rear;
	int count;
} list_t;

void Initlist();
void Add_list(list_t *list, int ID, int port, struct in_addr client_sock_addr, char *name, int size);
int hash(char *str);
void sigint_handler(int signo);
packet_form *packet;
list_t *flist[10];              //flist save file information as hash table
pool_t pool;
int server_socket;

int main(int argc, char* argv[])
{
	int res, i, port, s_port, ID = 1, optval = 1;
	int temp_id, temp_port;
	struct in_addr temp_addr;
	int super_socket = 0;
	struct sockaddr_in server_addr, super_addr;
	int client_socket, addr_size;
	char *name, *size, *str;
	struct sockaddr_in client_addr;
	char *s_ip = NULL;
	char port_op[3] = "-p";
	char s_port_op[9] = "--s_port";
	char s_ip_op[7] = "--s_ip";
	char divi[2] = " ";
	char dot[2] = ".";
	char dotdot[3] = "..";
	node_t *p;
	signal(SIGINT, sigint_handler);              //for free allocated memory when we put Ctrl+C
	packet = (packet_form *)calloc(1, sizeof(packet_form));

	if (argc < 3) {                              //invalid argument
		printf("ERROR: invalid arguments, need more argument\n");
		exit(0);
	}
	for (i = 1; i < argc; i = i + 2) {            //set information of options
		if (!strcmp(argv[i], port_op)) port = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], s_port_op)) s_port = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], s_ip_op)) s_ip = argv[i + 1];
		else printf("ERROR: invalid option\n");
	}

	if (s_ip != NULL) {                   //If this is second super node, we have to send HELLO
		ID = 2;
		memset(&super_addr, 0, sizeof(super_addr));   //set socket
		super_addr.sin_family = AF_INET;
		super_addr.sin_port = htons(s_port);
		super_addr.sin_addr.s_addr = inet_addr(s_ip);

		super_socket = socket(AF_INET, SOCK_STREAM, 0); //make super socket, this connect with super node
		if (super_socket == -1) {
			printf("socket 생성 실패\n");
			exit(0);
		}
		res = connect(super_socket, (struct sockaddr *)&super_addr, sizeof(super_addr));
		if (res == -1) {
			printf("connect 실패\n");
			exit(0);
		}

		packet->header.MSG_type = htonl(0x00000012);
		packet->header.length = htonl(sizeof(Header));
		packet->header.node_ID = htonl(0);
		write(super_socket, packet, ntohl(packet->header.length));    //send HELLO FROM SUPER TO SUPER
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

	memset(&server_addr, 0, sizeof(server_addr));     //set server socket
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
	}
	pool.maxfd = server_socket;
	FD_ZERO(&(pool.read_set));
	FD_SET(server_socket, &(pool.read_set));   //set pool informaion
	if (s_ip != NULL) {                //if this is second super node, we have to set pool this socket too for select()
		pool.client_sock[0] = super_socket;
		FD_SET(super_socket, &(pool.read_set));
		if (super_socket > pool.maxfd) pool.maxfd = super_socket;
		if (i > pool.maxi) pool.maxi = i;
	}

	Initlist();         //make flist for save file information

	while(1) { 
		pool.ready_set = pool.read_set;
		pool.readynum = select(pool.maxfd + 1, &(pool.ready_set), &(pool.write_set), NULL, NULL);     //select()

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
					pool.client_sock_addr[i] = client_addr.sin_addr;
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
					case 0x00000012:          //HELLO from SUPER to SUPER, we use this socket because this is connect with super node.
						super_socket = client_socket;   //so save this socket.
						break;

					case 0x00000010:            //HELLO from CHILD
						read(client_socket, packet->payload, ntohl(packet->header.length) - sizeof(Header));
						port = atoi(packet->payload);
						pool.client_sock_ID[i] = ID;                   //super node give ID for each child. 
						pool.client_sock_port[i] = port;

						packet->header.MSG_type = htonl(0x00000011);
						packet->header.length = htonl(sizeof(Header));
						packet->header.node_ID = htonl(ID);
						write(client_socket, packet, ntohl(packet->header.length));
						ID = ID + 2;                            //next child's ID is ID + 2 because avoid conflict with other super node's child's ID.
						break;

					case 0x00000020:         //FILE INFO
						read(client_socket, packet->payload, ntohl(packet->header.length) - sizeof(Header));
						res = 0;
						str = (char *)calloc(1, sizeof(packet->payload));
						strcpy(str, packet->payload);
						name = strtok(packet->payload, divi);           //name and size divide by space " "
						while (name != NULL) {
							while (name != NULL && (!strcmp(dot, name) || !strcmp(dotdot, name))) {     //there is '.' or '..' in directory so avoid this
								size = strtok(NULL, divi);
								name = strtok(NULL, divi);
								if (name == NULL) res++;
							}
							size = strtok(NULL, divi);
							if (size == NULL) {
								res--;
								break;
							}

							Add_list(flist[hash(name)], pool.client_sock_ID[i], pool.client_sock_port[i], pool.client_sock_addr[i], name, atoi(size));  //Add file info in list using hash
							name = strtok(NULL, divi);
						}

						if (res == 0) {
							sprintf(packet->payload, "%s", str);        //SEND RECV file information to child
							packet->header.MSG_type = htonl(0x00000021);  //FILE INFO RECV SUCCESS
							packet->header.length = htonl(sizeof(Header) + strlen(packet->payload));
							packet->header.node_ID = htonl(0);
							write(client_socket, packet, ntohl(packet->header.length));

							sprintf(packet->payload, "%d %d %s %s", pool.client_sock_ID[i], pool.client_sock_port[i], inet_ntoa(pool.client_sock_addr[i]), str); //share information with id, port, address
							packet->header.MSG_type = htonl(0x00000050);   //FILE INFO SHARE
							packet->header.length = htonl(sizeof(Header) + strlen(packet->payload));
							packet->header.node_ID = htonl(0);
							write(super_socket, packet, ntohl(packet->header.length));
						}
						else {
							packet->header.MSG_type = htonl(0x00000022);  //FILE INFO RECV FAIL
							packet->header.length = htonl(sizeof(Header));
							packet->header.node_ID = htonl(0);
							write(client_socket, packet, ntohl(packet->header.length));
						}
						free(str);
						break;

					case 0x00000050:      //FILE INFO SHARE
						read(client_socket, packet->payload, ntohl(packet->header.length) - sizeof(Header));
						str = (char *)calloc(1, sizeof(packet->payload));
						strcpy(str, packet->payload);
						res = 0;
						temp_id = atoi(strtok(packet->payload, divi));
						temp_port = atoi(strtok(NULL, divi));
						inet_aton(strtok(NULL, divi), &temp_addr);        //save ID, port, address information first.
						if (temp_id == 0 || temp_port == 0) res = -1; 
						name = strtok(NULL, divi);                       //Then save information about that child's file same as above
						while (name != NULL && (!strcmp(dot, name) || !strcmp(dotdot, name))) {
							size = strtok(NULL, divi);
							name = strtok(NULL, divi);
						}
						while (name != NULL && res == 0) {
							while (name != NULL && (!strcmp(dot, name) || !strcmp(dotdot, name))) {
								size = strtok(NULL, divi);
								name = strtok(NULL, divi);
								if (name == NULL) break;
							}
							size = strtok(NULL, divi);
							if (size == NULL) {
								res = -1;
								break;
							}

							Add_list(flist[hash(name)], temp_id, temp_port, temp_addr, name, atoi(size));
							name = strtok(NULL, divi);
						}

						if (res == 0) {
							sprintf(packet->payload, "%s", str);	         //SEND FILE INFO RECV to other super node with 0x00000051
							packet->header.MSG_type = htonl(0x00000051);  //FILE INFO RECV SUCCESS
							packet->header.length = htonl(sizeof(Header) + strlen(packet->payload));
							packet->header.node_ID = htonl(0);
							write(client_socket, packet, ntohl(packet->header.length));
						}
						else {
							packet->header.MSG_type = htonl(0x00000052);  //FILE INFO RECV FAIL
							packet->header.length = htonl(sizeof(Header));
							packet->header.node_ID = htonl(0);
							write(client_socket, packet, ntohl(packet->header.length));
						}
						free(str);
						break;

					case 0x00000051:      //FILE INFO SHARE SUCCESS 
						read(client_socket, packet->payload, ntohl(packet->header.length) - sizeof(Header));    //Check What I send.
						break;
					case 0x00000052:     //FILE INFO SHARE ERROR
						break;

					case 0x00000030:      //SEARCH QUERY
						read(client_socket, packet->payload, ntohl(packet->header.length) - sizeof(Header));   //read file name from payload
						for (p = flist[hash(packet->payload)]->front; p != NULL; p = p->next) {
							if (!strcmp(packet->payload, p->file_name)) {                            //find file using file name in hash table
								sprintf(packet->payload, "%s %d", inet_ntoa(p->node_addr), p->node_port);
								packet->header.MSG_type = htonl(0x00000031);
								packet->header.length = htonl(sizeof(Header) + strlen(packet->payload));
								packet->header.node_ID = htonl(0);
								write(client_socket, packet, ntohl(packet->header.length));  //SEARCH ANS SUCCESS
								break;
							}
						}
						if (p == NULL) {
							packet->header.MSG_type = htonl(0x00000032);
							packet->header.length = htonl(sizeof(Header));
							packet->header.node_ID = htonl(0);
							write(client_socket, packet, ntohl(packet->header.length));   //SEARCH ANS FAIL
						}
						break;
				}
			}
		}
	}
}

void Initlist()         //Initialize flist
{
	int i;
	for (i = 0; i < 10; i++) {
		flist[i] = (list_t *)calloc(1, sizeof(list_t));
		flist[i]->front = NULL;
		flist[i]->rear = NULL;
		flist[i]->count = 0;
	}
}

void Add_list(list_t *list, int ID, int port, struct in_addr client_sock_addr, char *name, int size)    //add information node to hash table's list
{
	node_t *new = (node_t *)calloc(1, sizeof(node_t));
    new->node_ID = ID;
    new->node_addr = client_sock_addr;
    new->node_port = port;
    if (name != NULL) {
    	new->file_name = (char *)calloc(strlen(name) + 1, sizeof(char));
    	strcpy(new->file_name, name);
    }
    new->file_size = size;
    new->next = NULL;
    if (list->count == 0) list->front = new;
    else list->rear->next = new;
    list->rear = new;
    list->count++;
}

int hash(char *str)         //calculate hash value using 2 char.
{
	if (strlen(str) == 1) return str[0] % 10;
	else return (str[0] * 10 + str[1]) % 10;
}

void sigint_handler(int signo)       //signal handle for Ctrl + C 
{
	int i;
	node_t *p, *temp;
	for (i = 0; i < 10; i++) {
		for (p = flist[i]->front; p != NULL; p = temp) {
			temp = p->next;
			free(p->file_name);
			free(p);
		}
		free(flist[i]);
	}                         //free all allocated memory
	free(packet);
	for (i = 0; i < FD_SETSIZE; i++) {
		if (pool.client_sock[i] > 0) {
			close(pool.client_sock[i]);
		}                    //close open socket in pool
	}
	close(server_socket);
	exit(0);
}