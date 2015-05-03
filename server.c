#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>


#define BUFFER_SIZE 1024

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
	printf("PARENT: Listener bound to port %d\n", port);

	return EXIT_SUCCESS;
}