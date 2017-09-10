/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <gorny@santarago.org> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 * ----------------------------------------------------------------------------
 */

/**
A poor-man's asynchronous DNS resolver. By fork()ing and then returning a fd
which can be used to send and receive resolved DNS entries by the calling
parent process. Please note that this DNS resolver will still resolve DNS
requests one by one and sequentially so for a large amount of DNS requests it
will still be very slow. This code is especially usefull if there's no
getaddrinfo_a() available on your platform and you don't want to include an
external asynchronous DNS resolver library in your code.  You can add data
pointers to associate data with a request.

Usage:

void * sessiondata = 0x41414141;
void * sessret;
struct addrinfo * ret;
resolve_fd = resolver_start();

do {
   // blabla event loop
   resolve(sessiondata, "kernel.org", "80");

  // if poll on resolve_fd returns a READ event
  resolve_result(&sessret,&ret);

  // handle the addrinfo struc; see man getaddrinfo()
  // sessret now contains 0x41414141 so you can retrieve the associated data

  freeaddrinfo(ret);
} while(1);

Just cleanup when exiting by calling resolver_stop(). Also remember to catch
SIGCHLD since the child *might* crash if it's OOM.

Also for those of you who still use gethostbyname to resolve DNS names; you
make Ulrich Drepper sad: http://www.akkadia.org/drepper/userapi-ipv6.html. 
  
It's quick and dirty but it might be of some use to someone though.
Written in September 2012.
**/

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

/* API */
int resolver_start();
void resolver_stop();
void resolve(void *, const char *, const char *);
void resolve_result(void **, struct addrinfo **);

#define ALARM_TIMEOUT 1

static int _child;
static int _fd;
static int _fdres;
static int started = 0;

static void
fd_setnonblock(int fd)
{
	int flags;
	flags = fcntl(fd, F_GETFL);
	flags |= O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);
}

static void
fd_setblock(int fd)
{
	int flags;
	flags = fcntl(fd, F_GETFL);
	flags &= ~O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);
}

static void
fatal(const char * msg)
{
	fprintf(stderr, "%s\n", msg);
	fflush(stderr);
	exit(EXIT_FAILURE);
}

static void *
xmalloc(size_t sz)
{
	void * p=malloc(sz);
	if (!p) fatal("malloc");
	memset(p, 0, sz);
	return p;
}

static void
fd_read(int fd, void * buf, size_t size)
{
	void * p;
	size_t todo;
	int ret;

	p = buf;
	ret = 0;
	todo = size;
	do {
		if (ret < 0) ret = 0;
		p += ret;	
		if (p-buf == size) break;
		todo -= ret;
		ret = read(fd, p, todo);
	} while (ret || ((ret < 0) && (errno == EINTR || errno == EAGAIN)));
	if (ret < 0) fatal("fd_read");
	return;
}

static void
fd_write(int fd, const void * buf, size_t size)
{
	const void * p;
	size_t todo;
	int ret;

	p = buf;
	ret = 0;
	todo = size;
	do {
		if (ret < 0) ret = 0;
		p += ret;	
		if (p-buf == size) break;
		todo -= ret;
		ret = write(fd, p, todo);
	} while ((ret) || ((ret < 0) && (errno == EINTR || errno == EAGAIN)));
	if (ret < 0) fatal("fd_write");
	return;
}

static void
sigpipe_handler(int err)
{
	exit(EXIT_FAILURE);
}

static void
sigalrm_handler(int err)
{
	if (getppid() != 1) alarm(ALARM_TIMEOUT);
	else exit(EXIT_FAILURE);
}

static void
sigterm_handler(int err)
{
	exit(EXIT_SUCCESS);
}

inline static void
resolver(int fd_in, int fd_out)
{
	int ret;
	void * ptr;
	size_t host_len, port_len, nrresults;
	char host[4096], port[5];
	struct addrinfo hints;
	struct addrinfo * res, * rp; 

	for (;;) {
		memset(host, 0, sizeof(host));		
		memset(port, 0, sizeof(port));
		fd_read(fd_in, &ptr, sizeof(void *));
		fd_read(fd_in, &host_len, sizeof(size_t));	
		if (host_len > sizeof(host)) fatal("invalid host_len");
		fd_read(fd_in, &host, host_len);
		fd_read(fd_in, &port_len, sizeof(size_t));	
		if (port_len > sizeof(port)) fatal("invalid port_len");
		fd_read(fd_in, &port, port_len);

		memset(&hints, 0, sizeof(struct addrinfo));
		/* XXX: should get hints from request */
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM; /* should get this from request */
		hints.ai_flags = AI_PASSIVE;     /* For wildcard IP address */
		hints.ai_protocol = 0;           /* Any protocol */
		hints.ai_canonname = NULL;
		hints.ai_addr = NULL;
		hints.ai_next = NULL;

		res = NULL;
		ret = getaddrinfo(host, port, &hints, &res);
		/* XXX: improve error handling */

		fd_write(fd_out, &ptr, sizeof(void *));

		nrresults = 0;
		for (rp = res; rp != NULL; rp = rp->ai_next) nrresults++;
		fd_write(fd_out, &nrresults, sizeof(size_t));
		for (rp = res; rp != NULL; rp = rp->ai_next) {
			fd_write(fd_out, rp, sizeof(struct addrinfo));
			fd_write(fd_out, rp->ai_addr, rp->ai_addrlen);
		}

		if (res) freeaddrinfo(res);
	}
}

int
resolver_start()
{
	int fds[2], fds2[2];
	pid_t pid;

	if (started) fatal("resolver already started"); 
	if (pipe(fds) < 0) fatal("pipe");
	if (pipe(fds2) < 0) fatal("pipe");

	pid = fork();
	switch (pid) {
		case -1:
			fatal("fork");
			/* does not return */
			break; 
		case 0:
			started = 1;
			signal(SIGPIPE, sigpipe_handler);
			signal(SIGALRM, sigalrm_handler);
			signal(SIGTERM, sigterm_handler);
			alarm(ALARM_TIMEOUT);
			resolver(fds[0], fds2[1]);
			/* does not return */
			break;
		default:
			started = 1;
			_child = pid;
	}
	_fd = fds[1];
	_fdres = fds2[0];
	return fds2[0];	
}

void
resolver_stop()
{
	kill(_child, SIGTERM);
}

void
resolve(void * p, const char * host, const char * port)
{
	int fd;
	size_t len;

	fd = _fd;
	fd_write(fd, &p, sizeof(void *));
	len = strlen(host);
	fd_write(fd, &len, sizeof(size_t));
	fd_write(fd, host, len);
	len = strlen(port);
	fd_write(fd, &len, sizeof(size_t));
	fd_write(fd, port, len);
	return;
}

void
resolve_result(void ** p, struct addrinfo ** r)
{
	void * ret;
	size_t i, nrresults;
	struct addrinfo * first, * rp, * last;

	fd_setblock(_fdres);
	nrresults = 0;
	fd_read(_fdres, &ret, sizeof(void *));	
	fd_read(_fdres, &nrresults, sizeof(size_t));
	first = last = NULL;
	for (i=0;i<nrresults;i++) {
		rp = xmalloc(sizeof(struct addrinfo));
		if (!i) first = last = rp;
		else {
			last->ai_next = rp;
			last = rp;
		}
		fd_read(_fdres, rp, sizeof(struct addrinfo));
		rp->ai_addr = xmalloc(rp->ai_addrlen);
		fd_read(_fdres, rp->ai_addr, rp->ai_addrlen);
	}
	if (last) last->ai_next = NULL;
	*p = ret;
	*r = first;
	fd_setnonblock(_fdres);
}
