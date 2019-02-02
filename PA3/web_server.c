#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#define MAX_PAYLOAD_SIZE 1024  //maximum size of each socket's data

typedef struct
{
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
	unsigned short client_sock_port[FD_SETSIZE];
	struct in_addr client_sock_addr[FD_SETSIZE];
} pool_t;

typedef struct Node
{
	int socket_num;
	unsigned short socket_port;
	struct in_addr socket_addr;
	struct Node *next;
} node_t;

typedef struct 
{
	node_t *front;
	node_t *rear;
	int count;
} queue_t;

pthread_mutex_t mutex;
pthread_cond_t cond;
pthread_cond_t full;
pthread_cond_t wake;
queue_t *queue;
FILE *fp_log[10];
int cond_cnt;

void Initqueue(queue_t *queue);
int IsEmpty(queue_t *queue);
void Enqueue(queue_t *queue, int socket_num, unsigned short socket_port, struct in_addr socket_addr);
node_t* Dequeue(queue_t *queue);
void *thread(void *vargp);
void sigint_handler(int signo);

int main(int argc, char* argv[])
{
	int res, i, port, optval = 1;
	int server_socket;
	struct sockaddr_in server_addr;
	int client_socket, addr_size;
	struct sockaddr_in client_addr;
	pool_t pool;
	pthread_t tid[10];
	signal(SIGINT, sigint_handler);

	if (argc != 1) {                              //invalid argument
		printf("ERROR: invalid arguments, no need argument\n");
		exit(0);
	}
	port = 8080;

	server_socket = socket(AF_INET, SOCK_STREAM, 0);  //make listener socket
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

	res = listen(server_socket, 10);          //listen socket
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

	queue = (queue_t *)calloc(1, sizeof(queue_t));
	Initqueue(queue);
	
	int iarr[10];
	for (i = 0; i < 10; i++) {
		iarr[i] = i + 1;
	}

	pthread_mutex_init(&mutex, NULL);       //init mutex and cond variable
	pthread_cond_init(&cond, NULL);
	pthread_cond_init(&full, NULL);
	pthread_cond_init(&wake, NULL);
	cond_cnt = 10;
	for (i = 0; i < 10; i++) {
		pthread_create(&tid[i], NULL, thread, (void *)&iarr[i]);    //make thread pool
	}

	while(1) {
		pthread_mutex_lock(&mutex);        //if queue is not empty, wait for processing
		if (!Isempty(queue)) pthread_cond_wait(&full, &mutex);
		pthread_mutex_unlock(&mutex);

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

					pool.client_sock_port[i] = client_addr.sin_port;
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

				pthread_mutex_lock(&mutex);
				Enqueue(queue, client_socket, pool.client_sock_port[i], pool.client_sock_addr[i]);
				pthread_cond_signal(&cond);      //signal to pool, so enqueue from queue
				pthread_mutex_unlock(&mutex);
				FD_CLR(client_socket, &(pool.read_set));
				pool.client_sock[i] = -1;
			}
		}
	}
}

void *thread(void *vargp)
{
	int myid = *((int *)vargp);
	node_t *socket;
	packet_form packet_org;
	packet_form *packet;
	packet = &packet_org;
	char name[13];
	FILE *fp_read;
	struct tm *t;
	time_t timer;
	sprintf(name, "thread_%d.log", myid);           //log file name
	fp_log[myid - 1] = fopen(name, "w");

	pthread_detach(pthread_self());
	while(1) {
		pthread_mutex_lock(&mutex);
		pthread_cond_signal(&full);        //signal to main thread, i'm ready for processing
		cond_cnt--;
		if (cond_cnt != 0) pthread_cond_wait(&wake, &mutex);   //wait for all thread in pool
		else {
			cond_cnt = 10;
			pthread_cond_broadcast(&wake);
		}
		while(Isempty(queue)) pthread_cond_wait(&cond, &mutex);   //if signal receive, process 
		socket = Dequeue(queue);
		pthread_mutex_unlock(&mutex);
		memset(packet, 0, sizeof(packet_form));
		read(socket->socket_num, packet, sizeof(packet_form));     //read http request
		
		memset(packet, 0, sizeof(packet_form));
		sprintf(packet->payload, "HTTP/1.1 200 OK\n");
		write(socket->socket_num, packet, sizeof(packet_form));       //send http response
		
		fp_read = fopen("index.html", "r");
		memset(packet, 0, sizeof(packet_form));
		while (fgets(packet->payload, MAX_PAYLOAD_SIZE, fp_read) != NULL) {
			write(socket->socket_num, packet, sizeof(packet_form));
			memset(packet, 0, MAX_PAYLOAD_SIZE);
		}
		fclose(fp_read);

		timer = time(NULL);                     //write log
		t = localtime(&timer);
		fprintf(fp_log[myid - 1], "%02d:%02d:%02d ", t->tm_hour, t->tm_min, t->tm_sec);
		fprintf(fp_log[myid - 1], "%s:%d GET /index.html\n", inet_ntoa(socket->socket_addr), ntohs(socket->socket_port));
		fflush(fp_log[myid - 1]);
		
		memset(packet->payload, '0', 20);
		write(socket->socket_num, packet, strlen(packet->payload) + 1);    //for notice finish
		close(socket->socket_num);
		free(socket);
	}
}

void Initqueue(queue_t *queue)
{
    queue->front = queue->rear = NULL;
    queue->count = 0;
}
 
int Isempty(queue_t *queue)
{
    return queue->count == 0;
}
 
void Enqueue(queue_t *queue, int socket_num, unsigned short socket_port, struct in_addr socket_addr)
{
    node_t *new = (node_t *)malloc(sizeof(node_t));
    new->socket_num = socket_num;
    new->socket_port = socket_port;
    new->socket_addr = socket_addr;
    new->next = NULL;

    if (Isempty(queue)) queue->front = new;
    else queue->rear->next = new;
    queue->rear = new;
    queue->count++;
}
 
node_t* Dequeue(queue_t *queue)
{
    node_t *temp;
    if (Isempty(queue)) return NULL;
    temp = queue->front;
    queue->front = temp->next;
    queue->count--;
    return temp;
}

void sigint_handler(int signo)       //signal handle for Ctrl + C 
{
	node_t *p, *temp;
	int i;
	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&cond);
	pthread_cond_destroy(&wake);
	pthread_cond_destroy(&full);
	for (i = 0; i < 10; i++) {
		fclose(fp_log[i]);
	}
	for (p = queue->front; p != NULL; p = temp) {
		temp = p->next;
		free(p);
	}
	free(queue);
	exit(0);
}