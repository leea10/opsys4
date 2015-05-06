#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

int main() {
	/* create TCP client socket */
	int sockfd = socket(PF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		perror("socket() failed!\n");
		exit(EXIT_FAILURE);
	}

	struct hostent* hp = gethostbyname("ariel-Inspiron-3521");
	if(hp == NULL) {
		perror("gethostbyname() failed!\n");
		exit(EXIT_FAILURE);
	}

	struct sockaddr_in server;
	server.sin_family = PF_INET;
	memcpy((void*)&server.sin_addr, (void*)hp->h_addr, hp->h_length);
	unsigned short port = 8765;
	server.sin_port = htons(port);

	if(connect(sockfd, (struct sockaddr*)&server, sizeof(server)) < 0) {
		perror("connect() failed!\n");
		exit(EXIT_FAILURE);
	}

	FILE * socket_stream = fdopen(sockfd, "r");
	char command[BUFFER_SIZE];
	while(1) {
    	printf("Enter a command: ");
    	strcpy(command, " ");
    	scanf("%[^*]", command);
    	getchar();
    	getchar();

    	fflush(NULL);
    	int n = write(sockfd, command, strlen(command));
    	printf("you sent: %s", command);
    	if(n < 0) {
    		perror("write() failed!\n");
   			exit(EXIT_FAILURE);
    	}

    	// count how many commands were in that sent packet
    	int num_commands = 0;
    	char* cmd = strtok(command, "\n");
    	while(cmd) {
    		num_commands++;
    		cmd = strtok(NULL, "\n");
    	}

    	int msgs_rcvd = 0;
    	char buffer[BUFFER_SIZE];
    	while(msgs_rcvd < num_commands && fgets(buffer, BUFFER_SIZE, socket_stream)) {
    		printf("%s", buffer);
    		msgs_rcvd++;
    	}
	}

	close(sockfd); 
	return EXIT_SUCCESS;
}