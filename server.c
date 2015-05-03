#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>

#define BUFFER_SIZE 1024

typedef struct client_info {
	int sockfd;
} client_t;

void* handle_client(void* args) {
	client_t* client = (client_t*) args;	// function arguments
	int pth = (int)pthread_self(); 			// thread ID
	char buffer[BUFFER_SIZE];	   			// bytes received from client
	int n = 0; 						   		// number of bytes received?
	do {
		printf("[thread %d] Blocked on recv()\n", pth);
		//n = recv(client->sockfd, buffer, BUFFER_SIZE, 0);
	} while(n > 0); // n will be 0 when the client closes its connection
	free(client);
	return NULL;
}

int main() {
	/* Create listener as TCP (SOCK_STREAM) socket */
	int sockfd = socket(PF_INET, SOCK_STREAM, 0);

	if(sockfd < 0) {
		perror("socket() failed!");
		exit(EXIT_FAILURE);
	}

	/* socket address information */
	unsigned short port = 8765; // port to bind to

	struct sockaddr_in server_address;
	server_address.sin_family = PF_INET;
	server_address.sin_addr.s_addr = INADDR_ANY;
	server_address.sin_port = htons(port);
	/* htons() is host-to-network-short for marshalling */
	/* Internet is a big-endian but intel is a little-endian */

	int addr_length = sizeof(server_address);

	if(bind(sockfd, (struct sockaddr *)&server_address, addr_length) < 0) {
		perror("bind() failed!");
		exit(EXIT_FAILURE);
	}

	listen(sockfd, 5); // 5 is the maximum number of waiting clients
	printf("Listening on port %d\n", port);

	struct sockaddr_in client_address;
	int fromlen = sizeof(client_address);

	while(1) {
		client_t* new_client = (client_t*)malloc(sizeof(client_t));
		new_client->sockfd =  accept(sockfd, (struct sockaddr*)&client_address, (socklen_t*)&fromlen);
		printf("Received incoming connection from %s\n", "<client-hostname");

		/* handle socket in new thread */
		pthread_t thread;
		if(pthread_create(&thread, NULL, handle_client, (void*)new_client) != 0) {
			perror("pthread_create() failed!");
			free(new_client);
		}
	}
	return EXIT_SUCCESS;
}