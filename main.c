#include <stdio.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/sendfile.h>

#include "main.h"

#define NUM_THREADS 10
#define NUM_CONNECTIONS 10
#define MAX_CONNECTIONS_PER_ADDRESS 100
#define SHARED_CONNECTIONS 0

#define IGNORE_BROKEN_PIPE 1

#define LINE_CNT_DEFAULT 1024
#define LINE_CNT_BLOCK 256
#define LINE_LENGTH_DEFAULT 16
#define LINE_LENGTH_BLOCK 8

#define NUM_MY_ADDRS 0

#if NUM_MY_ADDRS > 0
	#if NUM_CONNECTIONS > MAX_CONNECTIONS_PER_ADDRESS * NUM_MY_ADDRS
		#error Too many connections for the given number of source addresses
	#endif
#endif

const char* host = "94.45.231.39";
const char* myaddrs[NUM_MY_ADDRS] = {};
const unsigned short port = 1234;


char* filename = "data.txt";

int sockets[NUM_CONNECTIONS];
FILE* files[NUM_THREADS];

pthread_t threads[NUM_THREADS];

struct threadargs_t threadargs[NUM_THREADS];

bool doexit = false;

void* send_thread(void* data)
{
	long i = 0;
	int err;
	struct threadargs_t* args = data;
	printf("Starting thread %d, lines @%p, lengths @%p\n", args->tid, args->lines, args->linelengths);
	while(!doexit)
	{
		for(i = 0; i < args->numlines; i++)
		{
			if((err = write(args->socket, args->lines[i], args->linelengths[i])) < 0)
			{
				if(errno == EPIPE && IGNORE_BROKEN_PIPE)
					continue;
				fprintf(stderr, "Write failed after %d lines: %d => %s\n", i, errno, strerror(errno));
				doexit = true;
				break;
			}
		}
	}

	return NULL;
}

void doshutdown(int signal)
{
	doexit = true;
}

int main(int argc, char** argv)
{
	unsigned char* buffer, *line, *linetmp;
	unsigned char** lines, **linestmp;
	int socket_cnt, thread_cnt, i, sockcons = 0, sockindex = 0, err = 0;
	long* linelengths, *linelengthstmp;
	struct sockaddr_in inaddr;
	struct sockaddr addr;
	struct sockaddr_in inmyaddrs[NUM_MY_ADDRS];
	FILE* file;
	long fsize, linenum = 0, linenum_alloc, linepos = 0, linepos_alloc, fpos = 0, lines_per_thread;
	if(argc < 2)
	{
		fprintf(stderr, "Please specify a file to send\n");
		return -EINVAL;
	}
	filename = argv[1];
	if(SHARED_CONNECTIONS || NUM_THREADS != NUM_CONNECTIONS)
		return -EINVAL;
	if(signal(SIGINT, doshutdown))
	{
		fprintf(stderr, "Failed to bind signal\n");
		return -EINVAL;
	}
	if(signal(SIGPIPE, SIG_IGN))
	{
		fprintf(stderr, "Failed to bind signal\n");
		return -EINVAL;
	}
	if(!(file = fopen(filename, "r")))
	{
		fprintf(stderr, "Failed to open file: %d\n", errno);
		return -errno;
	}
	fseek(file, 0, SEEK_END);
	fsize = ftell(file);
	fseek(file, 0, SEEK_SET);
	printf("File size: %dl bytes\n", fsize);
	inet_pton(AF_INET, host, &(inaddr.sin_addr.s_addr));
	inaddr.sin_port = htons(port);
	inaddr.sin_family = AF_INET;
	for(i = 0; i < NUM_CONNECTIONS; i++)
	{
		
	}
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if(err = connect(sock, (struct sockaddr *)&inaddr, sizeof(inaddr)))
	{
		printf("Failed to connect to target\n");
		goto file_cleanup;
	}
	while(!doexit)
	{
		sendfile(sock, fileno(file), 0, fsize);
		fseek(file, 0, SEEK_SET);
	}
	
	close(sock);
	shutdown(sock, SHUT_RDWR);
	int one = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));

file_cleanup:
	fclose(file);
	return err;
}
