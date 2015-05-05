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
	int bytes_transferred = 0; 						   	 // will hold umber of bytes received
	
	// receieve and handle data sent from this client
	do {
		char command[BUFFER_SIZE]; // data received from client
		bytes_transferred = read(client->sockfd, command, BUFFER_SIZE);
		command[bytes_transferred] = '\0';

		if(bytes_transferred < 0) { // something went wrong, loop will terminate
			perror("read() failed!\n");
		} else if(bytes_transferred == 0) { // client closed socket, loop will terminate
			printf("[thread %u] Client closed its socket....terminating\n", pth);
		} else { // successful read
			printf("[thread %u] Rcvd: %s", pth, command);
			int cmd_len = bytes_transferred; // for the sake of making the code more readable

			// checking for valid command
			unsigned short is_valid_cmd = 0;
			if(cmd_len > 7 && command[6] == ' ') {
				char* filename = command + 7;
				command[6] = '\0';
				if(strcmp(command, "DELETE") == 0) {
					// find the end of the filename argument
					char* p = filename;
					while(!(*p == '\n' || *p == '\0')) {
						p++;
					}
					if(*p == '\n') {  // reached end of filename, according to protocol
						*p = '\0';	  // filename is now stored correctly
						is_valid_cmd = 1; // officially a valid command!
					}			
				}
			} else if(cmd_len > 6 && command[5] == ' ') {
				char* command_args = command + 6;
				command[5] = '\0';
				if(strcmp(command, "STORE") == 0) {
					is_valid_cmd = 1;
				}
			} else if(cmd_len > 5 && command[4] == ' ') {
				char* command_args = command + 5;
				command[4] = '\0';
				if(strcmp(command, "READ") == 0) {
					is_valid_cmd = 1;
				}
			} else if(cmd_len >= 4 && command[3] == '\n') {
				command[3] = '\0';
				if(strcmp(command, "DIR") == 0) {
					is_valid_cmd = 1;
					// LIST THE DIR STUFF
				}
			} 

			// send proper message back to client
			char* msg = NULL;
			msg = is_valid_cmd ? "ACK\n" : "ERROR: invalid command\n";
			int msg_len = strlen(msg);
			bytes_transferred = write(client->sockfd, msg, msg_len);
			if(bytes_transferred < msg_len) { // sending failed, loop will terminate
				perror("write() failed!\n");
			}

			// else if strlen >= 6 and strcmp first 6 characters to 'STORE '
				// start from the character after the space
				// iterate until...
					// reach another space
						// store the string in between as the filename
						// check if the filname exists already
							// if something goes wrong, send error, close this connection
							// if file does exist, send back an error
							// if not, store the file name in a variable
					// reach EOS or \n
						// return invalid command error to client
				// iterate until...
					// if reach a non-numeric character
						// return invalid command error to client
					// else if reach '\n'
						// convert string into integer and store as num_bytes
				// AT THIS POINT, THE COMMAND IS VALID
				// if the rest of the string's length is less than the stored num bytes,
					// return error: not enough bytes in content
				// create a new file in .storage with the stored file name
				// open file
				// lock file
				// copy the specified number of bytes into the file
				// close the file
				// unlock file
				// send ACK
		}
	} while(bytes_transferred > 0); // n will be 0 when the client closes its connection
	
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