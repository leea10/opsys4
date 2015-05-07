#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define N_FRAMES 32
#define FRAME_SIZE 1024
#define FRAMES_PER_FILE 4

// struct for page table entries
typedef struct page_table_entry {
	char* filename;
	int page_number;
	time_t last_used;
} pte_t;

// GLOBAL VARIABLES... BEWARE.
char server_memory[N_FRAMES * FRAME_SIZE];
pte_t page_table[N_FRAMES];

// function to handle DIR command
// parameter: socket descriptor to write errors and results to
// return: number of files in the storage directory, -1 if failed
int list_dir(int sockfd) {
	int num_files = 0;                // count of the number of files in the directory
	int file_list_size = BUFFER_SIZE; // initial size of the file list in bytes
	char* file_list = (char*)malloc(file_list_size*sizeof(char)); // file list
	int i = 0; // position that we are writing to in the file list	

	// read directory
	DIR* directory = opendir(".storage");
	struct dirent* file;
	while((file = readdir(directory)) != NULL) {
		// do not count the . and .. directories
		if(strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) {
			continue;
		}
		num_files++;
		
		int filename_len = strlen(file->d_name);
		if(i + filename_len + 1 >= file_list_size) {
			// need to increase size of buffer
			file_list_size *= 2;
			char* tmp = realloc(file_list, file_list_size);
			if(tmp) {
				file_list = tmp;
			} else {
				perror("out of memory!\n");
				return -1;
			}
		} 

		// write this name of this file to the buffer
		sprintf(file_list+i, "%s\n", file->d_name); 
		i += filename_len + 1; // move our current position forward
	}
	
	// send result to client
	char msg[strlen(file_list)+8];
	sprintf(msg, "%d\n%s", num_files, file_list);
	write(sockfd, msg, strlen(msg));
	
	// free memory
	closedir(directory);
	free(file_list);
	return num_files;
}

// function to handle STORE command
// parameter: socket descriptor to write errors and results to
// parameter: name of file to store
// parameter: number of bytes being stored
// parameter: content to store
// return: number of bytes stored
int store_file(int sockfd, char* filename, int n_bytes, char* content) {
	int pth = (int)pthread_self(); 		 // thread ID
	if(filename && n_bytes > 0 && content) {
		// form the new file's relative path
		char new_filepath[strlen(".storage/")+strlen(filename)+1];
		sprintf(new_filepath, ".storage/%s", filename);
		
		// check if the file already
		struct stat buf;
		int rc = stat(new_filepath, &buf);	
		if(rc >= 0) {
			char* msg = "ERROR: FILE EXISTS\n";
			write(sockfd, msg, strlen(msg));
			return 0;
		}

		// create file and open for writing
		int fd_write = open(new_filepath, 
			O_WRONLY | O_TRUNC | O_CREAT, 
			S_IWUSR | S_IWGRP | S_IWOTH | S_IRUSR | S_IRGRP | S_IROTH);
		if(fd_write < 0) {
			printf("open() failed for write file %s!", new_filepath);
			return 0;
		}

		// write contents to file
		int bytes_written = write(fd_write, content, n_bytes);
		printf("[thread %u] Transferred file (%d bytes)\n", pth, bytes_written);

		// send acknowledgement
		write(sockfd, "ACK\n", 4);
		printf("[thread %u] Sent: ACK\n", pth);
		close(fd_write);
		return 1;
	} else {
		char* msg = "ERROR: invalid arguments for STORE command\n";
		write(sockfd, msg, strlen(msg));
		return 0;
	}
}

// function to handle READ command
// parameter: socket descriptor to write errors and results to
// parameter: name of file to read from
// return: number of bytes read
int read_file(int sockfd, char* filename, int byte_offset, int length) {
	if(filename && byte_offset >= 0 && length > 0) {
		char msg[BUFFER_SIZE]; // plus one is for the null terminator
		sprintf(msg, "READ file: '%s' from byte %d (%d bytes)\n", 
			filename, byte_offset, length);
		write(sockfd, msg, strlen(msg));
		return 1;
	} else {
		char* msg = "ERROR: missing arguments for READ command\n";
		write(sockfd, msg, strlen(msg));
		return 0;
	}
}

// function to handle DELETE command
// parameter: socket descriptor to write errors and results to
// parameter: name of file to delete
// return: number of files deleted (i.e. 1 or 0)
int delete_file(int sockfd, char* filename) {
	// make sure filename was passed in
	if(!filename) {
		char* msg = "ERROR: missing arguments for DELETE command\n";
		write(sockfd, msg, strlen(msg));
		return 0;		
	}

	int pth = (int)pthread_self(); // thread ID

	// relative path of target file to delete
	char target[strlen(".storage/") + strlen(filename)];
	sprintf(target, ".storage/%s", filename);

	int rc = remove(target); // attempt to remove
	
	// check if file exists
	if(errno == ENOENT) {
		char* msg = "ERROR: NO SUCH FILE\n";
		int msg_len = strlen(msg);
		if(write(sockfd, msg, msg_len) == msg_len) {
			printf("[thread %u] Sent: %s\n", pth, msg);
		}
		return 0;
	} 

	// send acknowledgement if the delete was successful
	if(rc == 0) {
		printf("[thread %u] Deleted %s file\n", pth, filename);
		if(write(sockfd, "ACK\n", 4) == 4) {
			printf("[thread %u] Sent: ACK\n", pth);
		}
		return 1;
	}
	return 0; // something else went wrong
}

// arguments for handle_client() thread function
typedef struct client_info {
	int sockfd;
} client_t;

// new thread for new client
void* handle_client(void* args) {
	client_t* client = (client_t*) args; // function arguments
	int pth = (int)pthread_self(); 		 // thread ID
	FILE* client_sock = fdopen(client->sockfd, "r");

	// receieve and parse data sent from this client
	char full_cmd[BUFFER_SIZE]; // data receieved from client
	while(fgets(full_cmd, BUFFER_SIZE, client_sock)) {
		int n = strlen(full_cmd)-1;
		if(full_cmd[n] == '\n') {
			full_cmd[n] = '\0'; // get rid of newline character			
		}	
		printf("[thread %u] Rcvd: %s\n", pth, full_cmd);

		// check for valid command
		char* cmd = strtok(full_cmd, " ");
		if(!cmd) {
			char* msg = "ERROR: Empty command\n";
			write(client->sockfd, msg, strlen(msg));
		} else if(strcmp(cmd, "DIR") == 0) {
			list_dir(client->sockfd);
		} else if(strcmp(cmd, "STORE") == 0) {
			char* filename = strtok(NULL, " "); // name of file to store
			char* arg2 = strtok(NULL, " ");     // number of bytes to store
			int n_bytes = -1;
			char* content = NULL;
			if(arg2) {                          // convert to integer
				char* endptr;
				n_bytes = strtol(arg2, &endptr, 10);
				content = (char*)malloc(n_bytes*sizeof(char));
				fgets(content, n_bytes, client_sock);
			}
			store_file(client->sockfd, filename, n_bytes, content);
			free(content);
		} else if(strcmp(cmd, "READ") == 0) {
			char* filename = strtok(NULL, " "); // name of the file to read from
			char* arg2 = strtok(NULL, " ");     // byte offset to start reading at
			char* arg3 = strtok(NULL, " ");     // number of bytes to read
			
			int byte_offset = -1;
			int length = -1;
			if(arg2 && arg3) {
				char* endptr;
				byte_offset = strtol(arg2, &endptr, 10);
				length = strtol(arg3, &endptr, 10);
			}
			read_file(client->sockfd, filename, byte_offset, length);
		} else if(strcmp(cmd, "DELETE") == 0) {
			char* filename = strtok(NULL, " ");
			delete_file(client->sockfd, filename);
		} else {
			char unknown_cmd_err[strlen(cmd)+27];
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