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

#define NUM_THREADS 1
#define NUM_CONNECTIONS 1
#define MAX_CONNECTIONS_PER_ADDRESS 1
#define SHARED_CONNECTIONS 0

#define IGNORE_BROKEN_PIPE 1

#define LINE_OFF_DEFAULT 1024
#define LINE_OFF_BLOCK 256

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

pthread_t threads[NUM_THREADS];

struct threadargs_t threadargs[NUM_THREADS];

bool doexit = false;

void* send_thread(void* data)
{
	int err;
	char str[20];
	struct threadargs_t* args = data;
	printf("Starting thread %d, offset %lu, length %lu\n", args->tid, args->offset, args->datalen);
	printf("Socket FD: %d File FD: %d\n", args->socket, fileno(args->file));
	str[sizeof(str) - 1] = 0;
	fseek(args->file, args->offset, SEEK_SET);
	fread(str, sizeof(str) - 1, 1, args->file);
	printf("Thread: %d Data start: %s\n", args->tid, str);
	fseek(args->file, args->offset + args->datalen - sizeof(str), SEEK_SET);
	printf("Reading %d bytes\n", sizeof(str) - 1);
	fread(str, sizeof(str) - 1, 1, args->file);
	printf("Thread: %d Data end: %s", args->tid, str);
	while(!doexit)
	{
		fseek(args->file, args->offset, SEEK_SET);
		if((err = sendfile(args->socket, fileno(args->file), 0, args->datalen)) < 0)
		{
			if(errno == EPIPE && IGNORE_BROKEN_PIPE)
				continue;
			fprintf(stderr, "Write failed: %d => %s\n", errno, strerror(errno));
			doexit = true;
			break;
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
	unsigned char* buffer;
	off_t *lineoffsets, *lineoffsetstmp;
	int socket_cnt, thread_cnt, file_cnt, i, sockcons = 0, sockindex = 0, err = 0;
	struct sockaddr_in inaddr;
	struct sockaddr_in inmyaddrs[NUM_MY_ADDRS];
	FILE *files[NUM_THREADS], *file;
	long fsize, lineoffset = 0, lineoffset_alloc, fpos = 0, lines_per_thread;
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
	for(file_cnt = 0; file_cnt < NUM_THREADS; file_cnt++)
	{
		if(!(files[file_cnt] = fopen(filename, "r")))
		{
			fprintf(stderr, "Failed to open file: %d\n", errno);
			err = -errno;
			goto files_cleanup;
		}
	}
	file = files[0];
	fseek(file, 0, SEEK_END);
	fsize = ftell(file);
	fseek(file, 0, SEEK_SET);
	buffer = malloc(fsize + 1);
	if(!buffer)
	{
		goto files_cleanup;
	}
	fread(buffer, fsize, 1, file);
	printf("Read %ld bytes of data to memory, counting instructions\n", fsize);
	lineoffsets = malloc(LINE_OFF_DEFAULT * sizeof(off_t));
	lineoffset_alloc = LINE_OFF_DEFAULT;
	lineoffsets[lineoffset++] = 0;
	while(fpos < fsize)
	{
		if(buffer[fpos++] == '\n')
		{
			lineoffsets[lineoffset] = fpos;
			lineoffset++;
			if(lineoffset == lineoffset_alloc)
			{
				lineoffset_alloc += LINE_OFF_BLOCK;
				lineoffsetstmp = realloc(lineoffsets, lineoffset_alloc * sizeof(off_t));
				if(!lineoffsetstmp)
				{
					fprintf(stderr, "Allocation of %d offsets failed, offset %d\n", LINE_OFF_BLOCK, lineoffset_alloc - LINE_OFF_BLOCK);
					err = -ENOMEM;
					goto offsets_cleanup;
				}
				lineoffsets = lineoffsetstmp;
			}
		}
	}
	lineoffsets[lineoffset] = fpos;

	printf("Final offset: %ld@%d\n", fpos, lineoffset);

	lines_per_thread = lineoffset / NUM_THREADS;
	printf("Using %ld lines per thread\n", lines_per_thread);

	inet_pton(AF_INET, host, &(inaddr.sin_addr.s_addr));
	inaddr.sin_port = htons(port);
	inaddr.sin_family = AF_INET;
	if(NUM_MY_ADDRS)
	{
		for(i = 0; i < NUM_MY_ADDRS; i++)
		{
			inmyaddrs[i].sin_family = AF_INET;
			inmyaddrs[i].sin_port = 0;
			inet_pton(AF_INET, myaddrs[i], &(inmyaddrs[i].sin_addr.s_addr));
		}
	}
	for(socket_cnt = 0; socket_cnt < NUM_CONNECTIONS; socket_cnt++)
	{
		sockets[socket_cnt] = socket(AF_INET, SOCK_STREAM, 0);
		if((err = sockets[socket_cnt]) < 0)
		{
			err = -errno;
			fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
			goto socket_cleanup;
		}
		if(NUM_MY_ADDRS)
		{
			sockcons++;
			if(sockcons >= MAX_CONNECTIONS_PER_ADDRESS)
			{
				sockindex++;
				sockcons = 0;
			}
			if(bind(sockets[socket_cnt], (struct sockaddr *)&inmyaddrs[sockindex], sizeof(inmyaddrs[sockindex])))
			{
				err = -errno;
				fprintf(stderr, "Failed to bind socket: %s\n", strerror(errno));
				goto socket_cleanup;
			}
		}
		printf("Connecting socket %d\n", socket_cnt);
		if((err = connect(sockets[socket_cnt], (struct sockaddr *)&inaddr, sizeof(inaddr))))
		{
			err = -errno;
			fprintf(stderr, "Failed to connect socket: %s\n", strerror(errno));
			goto socket_cleanup;
		}
		printf("Connected socket %d\n", socket_cnt);
	}
	for(thread_cnt = 0; thread_cnt < NUM_THREADS; thread_cnt++)
	{
		threadargs[thread_cnt].socket = sockets[thread_cnt % socket_cnt];
		threadargs[thread_cnt].tid = thread_cnt;
		threadargs[thread_cnt].file = files[thread_cnt];
		threadargs[thread_cnt].offset = lineoffsets[thread_cnt * lines_per_thread];
		threadargs[thread_cnt].datalen = lineoffsets[(thread_cnt + 1) * lines_per_thread] - lineoffsets[thread_cnt * lines_per_thread] + 1;
		pthread_create(&threads[thread_cnt], NULL, send_thread, &threadargs[thread_cnt]);
	}

	printf("Waiting for threads to finish\n");
	for(i = 0; i < NUM_THREADS; i++)
	{
		printf("Joining thread %d\n", i);
		pthread_join(threads[i], NULL);
		printf("Thread %d finished\n", i);
	}
	err = 0;

socket_cleanup:
	while(socket_cnt-- >= 0)
	{
		printf("Closing %d\n", sockets[socket_cnt]);
		close(sockets[socket_cnt]);
		shutdown(sockets[socket_cnt], SHUT_RDWR);
		int one = 1;
		setsockopt(sockets[socket_cnt], SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
	}
offsets_cleanup:
	free(lineoffsets);
memory_cleanup:
	free(buffer);
files_cleanup:
	printf("Got %d file descriptors\n", file_cnt);
	while(file_cnt-- > 0)
	{
		printf("Closing %d\n", fileno(files[file_cnt]));
		fclose(files[file_cnt]);
	}
	return err;
}
