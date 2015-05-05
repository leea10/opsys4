#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
	FILE* client_sock = fdopen(client->sockfd, "r");

	// receieve and handle data sent from this client
	char full_cmd[BUFFER_SIZE]; // data receieved from client
	while(fgets(full_cmd, BUFFER_SIZE, client_sock)) {
		full_cmd[strlen(full_cmd)-1] = '\0'; // get rid of newline character
		printf("[thread %u] Rcvd: %s\n", pth, full_cmd);

		char* cmd = strtok(full_cmd, " ");
		if(strcmp(cmd, "DIR") == 0) {
			write(client->sockfd, "DIR COMMAND FOUND\n", 18);
		} else if(strcmp(cmd, "STORE") == 0) {
			write(client->sockfd, "STORE COMMAND FOUND\n", 20);
		} else if(strcmp(cmd, "READ") == 0) {
			write(client->sockfd, "READ COMMAND FOUND\n", 19);
		} else if(strcmp(cmd, "DELETE") == 0) {
			write(client->sockfd, "DELETE COMMAND FOUND\n", 21);
		} else {
			char unknown_cmd_err[strlen(cmd)+26];
			sprintf(unknown_cmd_err, "ERROR: Unknown command '%s'\n", cmd);
			write(client->sockfd, unknown_cmd_err, strlen(unknown_cmd_err));
		}
	}

	printf("[thread %u] Client closed its socket....terminating\n", pth);
	fclose(client_sock);
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