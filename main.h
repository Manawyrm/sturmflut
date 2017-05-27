#ifndef __MAIN_H_
#define __MAIN_H_

typedef struct threadargs_t {
	int tid;
	int socket;
	FILE *file;
	off_t offset;
	size_t datalen;
} threadargs_t;
#endif
