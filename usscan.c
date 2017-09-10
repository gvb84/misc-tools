/*
	UNIX socket access checker

	This simple scanner takes a username as the first and only input
	argument and drop privileges to that user. Then the scanner will
	enumerate through the available listening UNIX sockets on the
	system and it will try to read and write 1 byte to it.

	The tool will output a table which for each UNIX socket contains
	an entry which denotes the process id which owns the socket, the
	username of this process, the actual UNIX socket name and the
	results of the tests specified above. Please note that one must
	run this tool as root so it can retrieve the list of listening
	UNIX sockets properly via netstat.
*/
/*
- username: root
pid     user    conn    #read   #write  socket
------------------------------------------------------------------------------
1346    root    true    1       0               /var/run/fail2ban/fail2ban.sock
1047    root    true    1       0               /var/run/acpid.socket
13845   root    true    1       0               @tmp/ptud.sock
28113   postgres        true    1       0       /var/run/postgresql/.s.PGSQL.5432


*/

#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#define __USE_GNU
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define UNIX_PATH_MAX	108

inline static void
pfatal(const char * s)
{
	perror(s);
	fflush(stderr);
	exit(EXIT_FAILURE);
}

inline static void
fatal(const char * s)
{
	fprintf(stderr, "%s\n", s);
	fflush(stderr);
	exit(EXIT_FAILURE);
}

inline static int
timeout_read(int fd, void * buf, size_t buflen, int timeout)
{
	struct timeval tv;
	int ret;
	fd_set rfds, xfds;

	if (fd < 0 || !buf || !buflen || timeout < 0)
		fatal("argument failure in timeout_read");

	FD_ZERO(&rfds);
	FD_ZERO(&xfds);
	FD_SET(fd, &rfds);
	FD_SET(fd, &xfds);
	tv.tv_sec = timeout;
	tv.tv_usec = 0;
	ret = select(fd+1, &rfds, NULL, &xfds, &tv);

	if (FD_ISSET(fd, &xfds)) {
		fatal("ex\n");
	}
	if (FD_ISSET(fd, &rfds)) {
		return read(fd, buf, buflen);
	}
	return 0;
}

inline static int
timeout_write(int fd, void * buf, size_t buflen, int timeout)
{
	struct timeval tv;
	int ret;
	fd_set wfds, xfds;

	if (fd < 0 || !buf || !buflen || timeout < 0)
		fatal("argument failure in timeout_read");

	FD_ZERO(&wfds);
	FD_ZERO(&xfds);
	FD_SET(fd, &wfds);
	FD_SET(fd, &xfds);
	tv.tv_sec = timeout;
	tv.tv_usec = 0;
	ret = select(fd+1, NULL, &wfds, &xfds, &tv);

	if (FD_ISSET(fd, &xfds)) {
		fatal("ex\n");
	}
	if (FD_ISSET(fd, &wfds)) {
		return write(fd, buf, buflen);
	}
	return 0;
}

inline static int
unix_connect(char * sockname)
{
	struct sockaddr_un addr;
	int fd, ret;

	if (!sockname || strlen(sockname) > UNIX_PATH_MAX)
		fatal("argument failure in unix_connect");

	memset(&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;
	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) pfatal("socket");
	snprintf(addr.sun_path, UNIX_PATH_MAX, sockname);
	if (addr.sun_path[0]=='@') addr.sun_path[0]=0;
	ret = connect(fd, (struct sockaddr *)&addr,
		sizeof(struct sockaddr_un));
	return (ret < 0 ? ret : fd);
}

inline static void
get_ids(const char * username, uid_t * uid, gid_t * gid)
{
	char buffer[1024];
	struct passwd * pwd;

	pwd = malloc(sizeof(struct passwd));
	if(pwd == NULL) fatal("cannot allocate passwd struct");
	memset(buffer, 0, sizeof(buffer));
	getpwnam_r(username, pwd, buffer, sizeof(buffer), &pwd);
	if(pwd == NULL) fatal("invalid user specified");
	*uid = pwd->pw_uid;
	*gid = pwd->pw_gid;
	free(pwd);
}

inline static char *
get_user(uid_t uid)
{
	char * ret;
	char buffer[1024];
	struct passwd * pwd;

	pwd = malloc(sizeof(struct passwd));
	if(pwd == NULL) fatal("cannot allocate passwd struct");
	memset(buffer, 0, sizeof(buffer));

	getpwuid_r(uid, pwd, buffer, sizeof(buffer), &pwd);
	if (pwd == NULL) fatal("invalid uid specified");
	ret = strdup(pwd->pw_name);
	if (!ret) pfatal("strdup");
	free(pwd);
	return ret;
}

static void
check_socket(size_t pid, char * sockname)
{
	struct ucred credentials;
	char buf[1], * user;
	int fd, ret;
	socklen_t ucredsz;

	printf("%i\t", (int)pid);
	fd = unix_connect(sockname);
	if (fd < 0) {
		printf("n/a\tfalse\tn/a\tn/a");
		goto out;
	}

	ucredsz= sizeof(struct ucred);
 	memset(&credentials, 0, ucredsz);
	if((ret = getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials,
		&ucredsz)) < 0) {
		printf("n/a\t");
 	}
	else {
		user = get_user(credentials.uid);
		printf("%s\t", user);
		free(user);
	}
	

	printf("true\t");
	ret = timeout_write(fd, buf, 1, 1);
	printf("%i\t", ret);
	ret = timeout_read(fd, buf, 1, 1);	
	printf("%i", ret);
out:
	printf("\t");
	close(fd);
}
int
main(int argc, char ** argv)
{
	FILE * fp;
	char buf[1024], sockname[1024], * ret, * p;
	int size, iret, read;
	uid_t uid;
	gid_t gid;
	size_t pid;

	signal(SIGPIPE, SIG_IGN);

	if (geteuid() != 0 && getuid() != 0) fatal("run this tool as root");

	if (argc < 2) fatal("supply username to run test with");
	get_ids(argv[1], &uid, &gid);

	fp = popen("netstat -lnxp", "r");

	printf("UNIX socket access checker\n\n");
	printf("- username: %s\n", argv[1]);
	printf("pid\tuser\tconn\t#read\t#write\tsocket\n");
	for (iret=0;iret<78;iret++) printf("-");
	printf("\n");
	iret = setgid(gid);
	if (iret < 0) pfatal("setgid");
	iret = setuid(uid);
	if (iret < 0) pfatal("setuid");
	do {
		memset(buf, 0, sizeof(buf));
		size = sizeof(buf)-1;
		ret = fgets(buf, size, fp);
		if (!ret) break;
		p = strstr(buf, "LISTENING");	
		if (!p) continue; /* take next line */
		p += strlen("LISTENING");
		/* ignore first number */
		iret = sscanf(p, "%lu%n", &pid, &read);
		if (iret != 1) fatal("cannot find number in netstat output");
		p = p + read;
		iret = sscanf(p, "%lu%n", &pid, &read);
		if (iret != 1) fatal("cannot find number in netstat output");
		p = p + read;
		p = strstr(p, " ");
		if (!p) fatal("cannot find whitespace");
		memset(sockname, 0, sizeof(sockname));
		iret = sscanf(p, "%s", (char *)&sockname);	
		if (iret != 1) fatal("cannot find socket name");

		check_socket(pid, sockname);

		printf("\t%s\n", sockname);
		fflush(stdout);
	} while (!feof(fp));
	fclose(fp);

	printf("\ndone\n");
	exit(EXIT_SUCCESS);
}

/* EOF */
