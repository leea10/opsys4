#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

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

	char command[BUFFER_SIZE];
	while(1) {
    	printf("Enter a command: ");
    	strcpy(command, " ");
    	scanf("%[^\n]", command);
    	getchar();

    	int n = write(sockfd, command, strlen(command));
    	fflush(NULL);
    	if(n < 0) {
    		perror("write() failed!\n");
   			exit(EXIT_FAILURE);
    	}

    	char buffer[BUFFER_SIZE];
    	n = read(sockfd, buffer, BUFFER_SIZE); // blocking
    	if(n < 0) {
    		perror("read() failed!\n");
    		exit(EXIT_FAILURE);
    	} else {
    		buffer[n] = '\0';
    		printf("Received message from server: %s\n", buffer);
    	}
	}

	close(sockfd); 
	return EXIT_SUCCESS;
}