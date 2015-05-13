#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define N_FRAMES 8
#define FRAME_SIZE 10
#define FRAMES_PER_FILE 4

// struct for page table entries
typedef struct page_table_entry {
	char* filename;
	int page_number;
	struct timeval* last_used;
	pthread_rwlock_t* page_lock;
} pte_t;

// GLOBAL VARIABLES... BEWARE.
char* server_memory[N_FRAMES];
pte_t* page_table[N_FRAMES];
pthread_rwlock_t pte_locks[N_FRAMES];

// function to handle DIR command
// parameter: socket descriptor to write errors and results to
// return: number of files in the storage directory, -1 if failed
int list_dir(int sockfd) {
	int num_files = 0;                // count of the number of files in the directory
	int file_list_size = BUFFER_SIZE; // initial size of the file list in bytes
	char* file_list = (char*)malloc(file_list_size*sizeof(char)); // file list
	file_list[0] = '\0'; // null terminate just in case file_list will be empty
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
	write(sockfd, msg, strlen(msg)+1);
	
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
	int pth = (int)pthread_self(); // thread ID
	
	// validate arguments
	if(!filename || n_bytes <= 0 || !content) {
		char* msg = "ERROR: invalid arguments for STORE command\n";
		write(sockfd, msg, strlen(msg)+1);
		return 0;		
	}

	// form the new file's relative path
	char new_filepath[strlen(".storage/")+strlen(filename)+1];
	sprintf(new_filepath, ".storage/%s", filename);
	
	// check if the file already exists
	struct stat buf;
	int rc = stat(new_filepath, &buf);	
	if(rc >= 0) {
		char* msg = "ERROR: FILE EXISTS\n";
		write(sockfd, msg, strlen(msg)+1);
		return 0;
	}

	// set up file lock for this thread
	struct flock write_lock;
	write_lock.l_type = F_WRLCK; // write lock

	// create file and open for writing
	int fd_write = open(new_filepath, 
		O_WRONLY | O_TRUNC | O_CREAT, 
		S_IWUSR | S_IWGRP | S_IWOTH | S_IRUSR | S_IRGRP | S_IROTH);
	if(fd_write < 0) {
		printf("open() failed for write file %s!", new_filepath);
		return 0;
	}

	// set the lock
	if(fcntl(fd_write, F_SETLKW, &write_lock) < 0) {
		perror("Write lock failed!\n");
		return 0;
	}
	int bytes_written = write(fd_write, content, n_bytes); 	// write contents to file

	// unlock the file
	write_lock.l_type = F_UNLCK;
	if(fcntl(fd_write, F_SETLK, &write_lock) < 0) {
		perror("Unlock write lock failed!\n");
		return 0;
	}

	printf("[thread %u] Transferred file (%d bytes)\n", pth, bytes_written);

	// send acknowledgement
	write(sockfd, "ACK\n", 4);
	printf("[thread %u] Sent: ACK\n", pth);
	close(fd_write);
	return bytes_written;
}

// store and rdlock all pages of this file that are in the page table
// return the number of pages found
int get_all_pages(char* filename, int* all_pages) {
	int num_pages = 0;
	
	int i;
	for(i = 0; i < N_FRAMES; i++) {
		if(page_table[i]) {
			if(strcmp(page_table[i]->filename, filename) == 0) {
				*(all_pages + num_pages) = i;
				num_pages++;
			}
		}
	}
	return num_pages;
}

// function to find an empty slot in memory or the least recently used
// also lock the page table entry chosen
int find_slot() {
	int result = 0;
	int i;
	for(i = 0; i < N_FRAMES; i++) {
		if(!page_table[i]) {
			return i;
		}
		if(page_table[i]->last_used->tv_sec < page_table[result]->last_used->tv_sec ||
			(page_table[i]->last_used->tv_sec == page_table[result]->last_used->tv_sec
			&& page_table[i]->last_used->tv_usec < page_table[result]->last_used->tv_usec)) {
			result = i;
		}
	}
	return result;	
}

// function to handle READ command
// parameter: socket descriptor to write errors and results to
// parameter: name of file to read from
// return: number of bytes read
int read_file(int sockfd, char* filename, int byte_offset, int length) {
	int pth = (int)pthread_self();

	// validate arguments
	if(!filename || byte_offset < 0 || length < 0) {
		char* msg = "ERROR: missing arguments for READ command\n";
		int msg_len = strlen(msg);
		if(write(sockfd, msg, msg_len+1) == msg_len+1) {
			printf("[thread %u] Sent: %s", pth, msg);
		}
		return 0;
	}

	// relative path of target file to delete
	char target[strlen(".storage/") + strlen(filename)];
	sprintf(target, ".storage/%s", filename);

	// check if file exists
	struct stat buf;
	if(stat(target, &buf) < 0) {
		char* msg = "ERROR: NO SUCH FILE\n";
		int msg_len = strlen(msg);
		if(write(sockfd, msg, msg_len+1) == msg_len+1) {
			printf("[thread %u] Sent: %s", pth, msg);
		}
		return 0;
	}

	// make sure bytes are in range
	if(byte_offset + length > buf.st_size) {
		char* msg = "ERROR: INVALID BYTE RANGE\n";
		int msg_len = strlen(msg);
		if(write(sockfd, msg, msg_len+1) == msg_len+1) {
			printf("[thread %u] Sent: %s", pth, msg);
		}
		return 0;
	}

	// get first and last page number of range requested
	int first_page = byte_offset / FRAME_SIZE;
	int last_page = (byte_offset + length - 1) / FRAME_SIZE;

	// number of pages belonging to this file currently in memory
	int* all_pages = (int*)malloc(FRAMES_PER_FILE*sizeof(int));
	int num_pages = get_all_pages(filename, all_pages);
	
	int page;
	for(page = first_page; page <= last_page; page++) {
		// find in page table
		int i = 0;
		int index = -1;
		while(i < num_pages) {
			if(page_table[all_pages[i]]->page_number == page) {
				index = all_pages[i];
				//printf("found page %d in memory at slot %d\n", page, index);
			}
			i++;
		}

		// not found
		if(index < 0) {
			//printf("page %d not found in memory... loading...\n", page);
			// prepare table entry
			pte_t* new_pte = (pte_t*)malloc(sizeof(pte_t));
			new_pte->filename = (char*)malloc((strlen(filename)+1)*sizeof(char));
			sprintf(new_pte->filename, "%s", filename);
			new_pte->page_number = page;
			new_pte->last_used = NULL;
			new_pte->page_lock = (pthread_rwlock_t*)malloc(sizeof(pthread_rwlock_t));
			pthread_rwlock_init(new_pte->page_lock, NULL);

			// get the bytes from the file
			int fd = open(target, 'r');
			lseek(fd, page * FRAME_SIZE, SEEK_SET);

			// find where to put this memory block
			int replaced_page = -1;
			if(num_pages < FRAMES_PER_FILE) {
				//printf("Let's find a slot\n");
				// this file still has room in the memory for more frames
				index = find_slot();
				*(all_pages + num_pages) = index;
				num_pages++;
			} else {
				//printf("Max slots for this file, finding replace\n");
				// need to replace one of the current file's frames
				index = all_pages[0];
				for(i = 1; i < num_pages; i++) {
					// compare seconds
					if(page_table[all_pages[i]]->last_used->tv_sec < page_table[index]->last_used->tv_sec ||
						(page_table[all_pages[i]]->last_used->tv_sec == page_table[index]->last_used->tv_sec
						&& page_table[all_pages[i]]->last_used->tv_usec < page_table[index]->last_used->tv_usec)) {
						index = all_pages[i];
					}
				}
				replaced_page = page_table[index]->page_number;
			}

			// replace the page
			if(page_table[index]) {
				free(page_table[index]);					
			}	
			page_table[index] = new_pte;
			printf("[thread %u] Allocated page %d to frame %d",
				pth, page, index);
			if(replaced_page >= 0) {
				printf(" (replaced page %d)", replaced_page);
			}
			printf("\n");

			// place the memory into the server
			if(server_memory[index]) {
				free(server_memory[index]);				
			}	
			server_memory[index] = (char*)malloc(FRAME_SIZE*sizeof(char));
			read(fd, server_memory[index], FRAME_SIZE);
			close(fd);
		}

		// determine file bytes to send over
		char packet[BUFFER_SIZE];
		int from = 0;
		if(page == first_page && page == last_page) {
			// all content contained on one page
			strncpy(packet, server_memory[index] + byte_offset % FRAME_SIZE, length);
			packet[length] = '\0';
			from = byte_offset;
		} else if(page == first_page) {
			// this is the first page to be copied (of multiple)
				// could start in the middle of a frame, but will end on the end of the frame
			int len = FRAME_SIZE - byte_offset % FRAME_SIZE;
			strncpy(packet, server_memory[index] + byte_offset % FRAME_SIZE, len);
			packet[len] = '\0';
			from = byte_offset;
		} else if(page == last_page) {
			// this is the last page to be copied (of multiple)
				// could end in the middle of a frame, but will start on the beginning of one
			int len = (byte_offset + length -1) % FRAME_SIZE + 1; 
			strncpy(packet, server_memory[index], len);
			packet[len] = '\0';
			from = page * FRAME_SIZE;
		} else {
			// a page in the middle of multiple pages
				// the whole page will need to be copied start to end
			strncpy(packet, server_memory[index], FRAME_SIZE);
			from = page * FRAME_SIZE;
			packet[FRAME_SIZE] = '\0';
		}

		// update last time used
		struct timeval* now_time = (struct timeval*)malloc(sizeof(struct timeval));
		gettimeofday(now_time, NULL);
		page_table[index]->last_used = now_time;

		// send over the file bytes along with "ACK"
		int packet_len = strlen(packet);
		char ack[BUFFER_SIZE];
		sprintf(ack, "ACK %d\n", packet_len);
		int ack_len = strlen(ack);
		if(write(sockfd, ack, ack_len+1) == ack_len+1) {
			printf("[thread %u] Sent: %s", pth, ack);	
			if(write(sockfd, packet, packet_len+1) == packet_len+1) {
				printf("[thread %u] Transferred %d bytes from offset %d\n",
					pth, packet_len, from);
			}		
		}
	}
	return 1;
}

// function to handle DELETE command
// parameter: socket descriptor to write errors and results to
// parameter: name of file to delete
// return: number of files deleted (i.e. 1 or 0)
int delete_file(int sockfd, char* filename) {
	int pth = (int)pthread_self(); // thread ID

	// validate arguments
	if(!filename) {
		char* msg = "ERROR: missing arguments for DELETE command\n";
		int msg_len = strlen(msg);
		if(write(sockfd, msg, msg_len+1) == msg_len+1) {
			printf("[thread %u] Sent: %s", pth, msg);
		}
		return 0;		
	}

	// relative path of target file to delete
	char target[strlen(".storage/") + strlen(filename)+1];
	sprintf(target, ".storage/%s", filename);

	struct stat buf;	
	if(stat(target, &buf) < 0) {
		char* msg;	
		if(errno == ENOENT) {	
			msg = "ERROR: NO SUCH FILE\n";
		}
		int msg_len = strlen(msg);
		if(write(sockfd, msg, msg_len+1) == msg_len+1) {
			printf("[thread %u] Sent: %s", pth, msg);
		}
		return 0;		
	}

	// deallocate any frames and page table entries associated with this file
	int i;
	for(i = 0; i < N_FRAMES; i++) {
		pthread_rwlock_wrlock(pte_locks+i);
		if(page_table[i]) {
		 	if(strcmp(page_table[i]->filename, filename) == 0) {	
				pthread_rwlock_wrlock(page_table[i]->page_lock);
				free(page_table[i]);
				page_table[i] = NULL;			
				// no need to unlock page because the lock no longer exists...
				printf("[thread %u] Deallocated frame %d\n", pth, i);
			}
		}
		pthread_rwlock_unlock(pte_locks+i);
	}

	// send acknowledgement
	if(remove(target) == 0) {
		printf("[thread %u] Deleted %s file\n", pth, filename);
		if(write(sockfd, "ACK\n", 4) == 4) {
			printf("[thread %u] Sent: ACK\n", pth);
		}
		return 1;
	}
	return 0;
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
			write(client->sockfd, msg, strlen(msg)+1);
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
				content = (char*)malloc((n_bytes+1)*sizeof(char));
				
				// read contents until specified number of bytes are read
				int total_bytes_read = 0;
				while(total_bytes_read < n_bytes) {	
					total_bytes_read += read(client->sockfd, 
						content+total_bytes_read, n_bytes-total_bytes_read);
				}
				content[n_bytes] = '\0';
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
			char unknown_cmd_err[BUFFER_SIZE];
			sprintf(unknown_cmd_err, "ERROR: Unknown command '%s'\n", cmd);
			write(client->sockfd, unknown_cmd_err, strlen(unknown_cmd_err)+1);
		}
	}

	printf("[thread %u] Client closed its socket....terminating\n", pth);
	fclose(client_sock);
	close(client->sockfd);
	free(client); // free memory allocated for this thread's arguments
	pthread_exit(NULL);
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

	// initialize global lvariables to null
	int i;
	for(i = 0; i < N_FRAMES; i++) {
		page_table[i] = NULL;
		pthread_rwlock_init(pte_locks+i, NULL);
		server_memory[i] = NULL;
	}

	while(1) {
		client_t* new_client = (client_t*)malloc(sizeof(client_t)); // thread function arguments
		new_client->sockfd =  accept(sockfd, (struct sockaddr*)&client_address, (socklen_t*)&fromlen);
		printf("Received incoming connection from %d.%d.%d.%d\n",
	  		(int)(client_address.sin_addr.s_addr&0xFF),
  			(int)((client_address.sin_addr.s_addr&0xFF00)>>8),
  			(int)((client_address.sin_addr.s_addr&0xFF0000)>>16),
  			(int)((client_address.sin_addr.s_addr&0xFF000000)>>24));

		/* handle socket in new thread */
		pthread_t thread;
		if(pthread_create(&thread, NULL, handle_client, (void*)new_client) != 0) {
			perror("pthread_create() failed!\n");
			free(new_client);
		}
	}

	// free memory
	for(i = 0; i < N_FRAMES; i++) {
		if(server_memory[i]) {
			free(server_memory[i]);
		}
		if(page_table[i]) {
			free(page_table[i]->last_used);
			free(page_table[i]);
		}
	}

	close(sockfd);
	return EXIT_SUCCESS;
}