#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define N_FRAMES 32
#define FRAME_SIZE 1024
#define FRAMES_PER_FILE 4

// GLOBAL VARIABLES... BEWARE.
char server_memory[N_FRAMES * FRAME_SIZE];

// arguments for handle_client() thread function
typedef struct client_info {
	int sockfd;
} client_t;

// new thread for new client
void* handle_client(void* args) {
	client_t* client = (client_t*) args; // function arguments
	int pth = (int)pthread_self(); 		 // thread ID
	int n = 0; 						   	 // will hold umber of bytes received
	
	// receieve and handle data sent from this client
	do {
		char buffer[BUFFER_SIZE]; // data received from client
		n = read(client->sockfd, buffer, BUFFER_SIZE);
		if(n < 0) {
			perror("read() failed!\n");
		} else if(n == 0) {
			printf("[thread %u] Client closed its socket....terminating\n", pth);
		} else {
			printf("[thread %u] Rcvd: %s\n", pth, buffer);
			
			// TEST - send back acknowledgement to the client 
			n = write(client->sockfd, "ACK", 3);
			fflush(NULL);
			if(n != 3) {
				perror("send() failed!\n");
			}
		}
	} while(n > 0); // n will be 0 when the client closes its connection
	
	close(client->sockfd);
	free(client); // free memory allocated for this thread's arguments
	return NULL;  // terminate thread
}

int main() {
	/* Create listener as TCP (SOCK_STREAM) socket */
	int sockfd = socket(PF_INET, SOCK_STREAM, 0);

	if(sockfd < 0) {
		perror("socket() failed!\n");
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
		perror("bind() failed!\n");
		exit(EXIT_FAILURE);
	}

	listen(sockfd, 5); // 5 is the maximum number of waiting clients
	printf("Listening on port %d\n", port);

	struct sockaddr_in client_address;
	int fromlen = sizeof(client_address);

	// make the hidden storage directory, if it doesn't already exist
	if(mkdir(".storage", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0 && errno != EEXIST) {
		perror("mkdir() failed!\n");
		exit(EXIT_FAILURE);
	}

	while(1) {
		client_t* new_client = (client_t*)malloc(sizeof(client_t)); // thread function arguments
		new_client->sockfd =  accept(sockfd, (struct sockaddr*)&client_address, (socklen_t*)&fromlen);
		printf("Received incoming connection from %s\n", "<client-hostname");

		/* handle socket in new thread */
		pthread_t thread;
		if(pthread_create(&thread, NULL, handle_client, (void*)new_client) != 0) {
			perror("pthread_create() failed!\n");
			free(new_client);
		}
	}

	close(sockfd);
	return EXIT_SUCCESS;
}