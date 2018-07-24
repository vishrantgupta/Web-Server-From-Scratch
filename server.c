/*****************************************************************************
*
* A simple web server to handle get and head request
*
* Usage:  a3 -r <document root> [-d]
*
*  -d   Enables server debugging
*
*  -r	Document root directory name
*
*****************************************************************************/

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <regex.h>
#include <time.h>
#include <sys/resource.h>
#include <sys/wait.h>

static int handleTransaction(int sock);
static int reqHead(FILE *file, char *url);
static int reqGet(FILE *file, char *url);
static int reqBogus(FILE *file, char *url);
static void debug(char *format, ...);
static int sendHeader(FILE *fwsock, char *url);
static int sendStatus(FILE *fwsock, int isGet);
static int isEvilUrl(char *url);
static int sendBadRequestResponse(FILE *fwsock);
static int sendBody(FILE *fwsock, char *url);
static void sigHandler(int sig);

static int pipeCount = 0;
static int timeoutCount = 0;
static int headCount = 0;
static int getCount = 0;
static int errorCount = 0;
static int debugFlag = 0;
static char *documentRoot = NULL;
static time_t startTime;

static jmp_buf jmpEnv;

#define MY_SERVER_PORT		0	/* Set this for a static port number */
#define ALARM_TIME			20	/* Seconds to wait before aborting transaction */

/*****************************************************************************
*
* SIMPLE WEB SERVER
*
*****************************************************************************/

int main(int argc, char **argv)
{
	int					sock;
	int					length;
	struct sockaddr_in	server;
	int 				msgsock;
	int					c;

	/* The following are for getopt(3C) */
	extern char			*optarg;
	extern int			optind;
	extern int			opterr;
	extern int			optopt;

	while ((c = getopt(argc, argv, "dr:")) != EOF)
	{
		switch (c) 
		{
		case 'd':
			debugFlag = 1;
			break;
		case 'r':
			documentRoot = optarg;
			break;
		}
	}

	/* Make sure that we have a document root directory, or gripe & croak */
	if (documentRoot == NULL)
	{
		fprintf(stderr, "Usage: %s -r <document root> [-d]\n", argv[0]);
		exit(2);
	}

	time(&startTime);

	debug("Debugging enabled\n");

	/* Create socket to listen on */
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) 
	{
		perror("opening stream socket");
		exit(1);
	}
	/* Name socket */
	server.sin_family = AF_INET;			/* Use Internet TCP/IP protocols */
	server.sin_addr.s_addr = INADDR_ANY;	/* Use any attached addresses. */
	server.sin_port = htons(MY_SERVER_PORT);

	/* bind() tells the O/S to map the above server values to the socket */
	if (bind(sock, (struct sockaddr *)&server, sizeof(server))) 
	{
		perror("binding stream socket");
		exit(1);
	}

	/* Find out assigned port number and print it out */
	length = sizeof(server);
	if (getsockname(sock, (struct sockaddr *)&server, &length)) 
	{
		perror("getting socket name");
		exit(1);
	}
	fprintf(stderr, "Socket has port #%d\n", ntohs(server.sin_port));

	/* Tell the O/S that we are willing to accept connections */
	listen(sock, 5);

	while(1)
	{

		/* Wait for, and accept any connections from clients */
		msgsock = accept(sock, 0, 0);
		if (msgsock == -1)
		{
			/* Something bad happened in accept() */
			perror("accept");
		}
		else
		{	/* We got a connection, process the request */

			int		status;
			int		pid;

			debug("\nTransaction starting on FD %d\n", msgsock);
			pid = fork();
			
			if (pid < 0)
			{
				debug("ERROR: fork() failed\n");
				status = -1;
			}
			else if (pid == 0)
			{
				signal(SIGPIPE, sigHandler);
				signal(SIGALRM, sigHandler);

				alarm(ALARM_TIME);
				status = handleTransaction(msgsock);
				alarm(0);
				exit(status);
			}
			else
			{
				int		stat;
				int		waitpid;
				waitpid = wait(&stat);
				status = WEXITSTATUS(stat);
				switch(status)
				{
				case 1:
					pipeCount++;
					break;
				case 2:
					timeoutCount++;
					break;
				case 100:
					headCount++;
					break;
				case 110:
					getCount++;
					break;
				default:
					errorCount++;
					break;
				}
			}
			debug("Transaction completed, pid = %d, status = %d\n", pid, status);
			close(msgsock); /* Closes the FD in the parent process */
		}
	}
	/*
	 * Since this program has an infinite loop, the socket "sock" is
	 * never explicitly closed.  However, all sockets will be closed
	 * automatically when a process is killed or terminates normally. 
	 */
}
/*****************************************************************************
*
*****************************************************************************/
static void sigHandler(int sig)
{
	if (sig == SIGPIPE)
	{
		debug("SIGPIPE... aborting transaction\n");
		exit(1);
	}
	else if (sig == SIGALRM)
	{
		debug("SIGALRM... aborting transaction\n");
		exit(2);
	}
	exit(3);
}


/*****************************************************************************
*
* Handle one single socket connection request.
*
* RETURNS:
*	0			Bad request;
*	100
*	101
*	110
*	111
*	120
*	121
*
*****************************************************************************/
static int handleTransaction(int sock)
{
	FILE	*fsock;

	char	buf[2048];
	char	cmd[2048];
	int		status = 1;
	char	*method;
	char	*url;
	char	*lasts;
	char	*ret;

	/* Create a stream for reading AND writing */
    if ((fsock = fdopen(sock, "r+b")) == NULL)
    {
        perror("fdopen");
        return(0);
    }

	/* Read the request phase request line */
	ret = fgets(cmd, sizeof(cmd), fsock);
	if (ret != NULL)
	{	/* Got the command line of the request... grab the good stuff */

		method = strtok(cmd, " ");
		url = strtok(NULL, " ");

		if (method==NULL || url==NULL)
		{
			debug("handleTransaction() got bogus request line\n");
			fclose(fsock);
			return(0);
		}
		debug("method: '%s', url: '%s'\n", method, url);
	}

	/* Read rest of the request phase header lines and toss them */
	ret = fgets(buf, sizeof(buf), fsock);
	while (ret!=NULL && buf[0]!='\r' && buf[0]!='\n')
	{
		/*debug("Got input data line: '%s'\n", buf);*/
		ret = fgets(buf, sizeof(buf), fsock);
	}

	/* This is strange at best... is Solaris hosed? */
	fseek(fsock, 0L, SEEK_END);	

	if (!strcmp(method, "HEAD"))
	{
		if ((status = reqHead(fsock, url)) == 0)
			status = 100;
		else
			status = 101;
	}
	else if (!strcmp(method, "GET"))
	{
		if ((status = reqGet(fsock, url)) == 0)
			status = 110;
		else
			status = 111;
	}
	else
	{
		if ((status = reqBogus(fsock, url)) == 0)
			status = 120;
		else
			status = 121;
	}

	fflush(fsock);	/* force the stream to write any buffered data */
	fclose(fsock);	/* closes the stream AND the file descriptor(socket) */
	return(status);
}

/*****************************************************************************
*
* A custom version of printf() that only prints stuff if debugFlag is 
* not zero.
*
*****************************************************************************/
static void debug(char *format, ...)
{
	va_list ap;
	if (debugFlag)
	{
		va_start(ap, format);
		(void) vfprintf(stderr, format, ap);
		va_end(ap);
	}
	return;
}

/*****************************************************************************
*
* Handle the processing of a HEAD request type.
*
* file:		client socket stream 
* url:		unverified url requested in the GET request
*
*****************************************************************************/
static int reqHead(FILE *file, char *url)
{
	++headCount;

	/* Special processing for status requests */
	if (!strcmp(url, "/status"))
		return(sendStatus(file, 0));

	return(sendHeader(file, url));
}

/*****************************************************************************
*
* Handle the processing of a GET request type.
*
* file:		client socket stream 
* url:		unverified url requested in the GET request
*
*****************************************************************************/
static int reqGet(FILE *file, char *url)
{
	int	stat;
	++getCount;

	/* Special processing for status requests */
	if (!strcmp(url, "/status"))
		return(sendStatus(file, 1));

	if (isEvilUrl(url))
	{
		debug("URL is EVIL... rejecting\n");
		return(sendBadRequestResponse(file));
	}
	if ((stat = sendHeader(file, url)) != 0)
	{
		return(stat);
	}
	return(sendBody(file, url));
}

/*****************************************************************************
*
* Handle the processing of a bad request type.
*
* file:		client socket stream 
* url:		unverified url requested in the GET request
*
*****************************************************************************/
static int reqBogus(FILE *file, char *url)
{
	++errorCount;
	debug("bogus request received\n");
	return(0);
}

/*****************************************************************************
*
* Special function to send a response for a hit to URL /status
*
* fwsock:	Client socket stream 
* isGet:	Set to non-zero value if the requet type was a GET, 
*			otherwise we assume is a HEAD.
*
*****************************************************************************/
static int sendStatus(FILE *fwsock, int isGet)
{
	fprintf(fwsock, "HTTP/1.0 200 OK\r\n");
	fprintf(fwsock, "Content-Type: text/plain\r\n");
	fprintf(fwsock, "Connection: close\r\n");
	fprintf(fwsock, "\r\n");
	if (isGet)
	{
		time_t	t;
		time_t	delta;
		time_t  hours;
		time_t  mins;
		time_t  secs;
		char sname[64];
		int len;

		time(&t);

		delta = t-startTime;
		hours = delta/60/60;
		mins = (delta-(hours*60*60))/60;
		secs = delta-(hours*60*60)-(mins*60);

		strcpy(sname, ctime(&t));
		len = strlen(sname);
		sname[len-1] = 0;
		fprintf(fwsock, "Current time: %s\r\n", sname);
		strcpy(sname, ctime(&startTime));
		len = strlen(sname);
		sname[len-1] = 0;
		fprintf(fwsock, "Last restart: %s\r\n", sname);
		fprintf(fwsock, "      Uptime: %d hours, %d minuites, %d seconds\r\n\r\n", hours, mins, secs);

		// get the socket's peer name and display it
		struct sockaddr_in sa;
		socklen_t sl = sizeof(sa);

		fprintf(fwsock, "Socket Peer details:\r\n");
		int rc = getpeername(fileno(fwsock), (struct sockaddr*)&sa, &sl);
		inet_ntop(sa.sin_family, &sa.sin_addr, sname, sizeof(sname));
		fprintf(fwsock, "  Address: %s\r\n", sname);
		fprintf(fwsock, "     Port: %d\r\n", sa.sin_port);
		fprintf(fwsock, "\r\n");

#if 0
		struct rusage usage;
		getrusage(RUSAGE_SELF, &usage);
		fprintf(fwsock, "CPU Utilization:\n");
		fprintf(fwsock, "  User Mode......:%d.%06d Seconds\n", usage.ru_utime.tv_sec, usage.ru_utime.tv_usec);
		fprintf(fwsock, "  System Mode....:%d.%06d Seconds\n", usage.ru_stime.tv_sec, usage.ru_stime.tv_usec);
		fprintf(fwsock, "  Preempts.......:%d\n\n", usage.ru_nivcsw);
#endif


		fprintf(fwsock, "Program Status\r\n");
		fprintf(fwsock, "  Total Hits.....:%d\r\n", headCount+getCount+errorCount);
		fprintf(fwsock, "  GET requests...:%d\r\n", getCount);
		fprintf(fwsock, "  HEAD requests..:%d\r\n", headCount);
		fprintf(fwsock, "  Bad requests...:%d\r\n", errorCount);
		fprintf(fwsock, "  Timeouts ......:%d\r\n", timeoutCount);
		fprintf(fwsock, "  Broken pipes...:%d\r\n", pipeCount);
	}

#if 0
	/* This delay will allow us with an easy way to force a SIGPIPE */
	fflush(fwsock);
	sleep(2);
	fprintf(fwsock, "-\r\n");
#endif

	return(0);
}

/*****************************************************************************
*
* Send the appropriate header data for the requested URL.
*
* fwsock:	client socket stream 
* url:		unverified url requested in the GET request
*
*****************************************************************************/
static int sendHeader(FILE *fwsock, char *url)
{
	size_t	len;
	char	*ct;

	len = strlen(url);
	if (!strcmp(&url[len-4], ".gif"))
		ct = "image/gif";
	else if (!strcmp(&url[len-5], ".html"))
		ct = "text/html";
	else
		ct = "text/plain";

	debug("Content-Type: %s\n", ct);

	fprintf(fwsock, "HTTP/1.0 200 OK\r\n");
	fprintf(fwsock, "Content-Type: %s\r\n", ct);
	fprintf(fwsock, "Connection: close\r\n");
	fprintf(fwsock, "\r\n");
	return(0);
}

/*****************************************************************************
*
* Return zero if the URL is OK to process, and non-zero if it looks risky.
*
* non-evil URLs are ONLY made up of the following characters:
*
*	[a-z][A-z][0-9]/.?&%-_
*
* non-evil URLs must NOT contain the following sequence:
*
*	/../
*
*****************************************************************************/
static int isEvilUrl(char *url)
{
	char	*pattern="^/[a-zA-Z0-9/?._%&-]*$";
	int  	status;
	regex_t	re;

	if (regcomp(&re, pattern, REG_NOSUB) != 0) 
	{
		debug("regcomp() failed in isEvilUrl\n");
		return(1);      /* report error */
	}
	status = regexec(&re, url, (size_t) 0, NULL, 0);
	regfree(&re);
	if (status != 0)
	{
		debug("regcomp() does not like the specified URL\n");
		return(1);      /* report error */
	}

	return(strstr(url, "/../")!=NULL);
}

/*****************************************************************************
*
* Send a simple hard-coded general purpose error response to the client... 
* including headers.  This is used as a bail-out reply when things go bad.
*
* fwsock:	client socket stream 
*
*****************************************************************************/
static int sendBadRequestResponse(FILE *fwsock)
{
	fprintf(fwsock, "HTTP/1.0 404 Not Found\r\n");
	fprintf(fwsock, "Content-Type: text/plain\r\n");
	fprintf(fwsock, "Connection: close\r\n");
	fprintf(fwsock, "\r\n");
	fprintf(fwsock, "Error, bad request\r\n");
	return(0);
}

/*****************************************************************************
*
* Send the body of the specified URL to the client.
* Assume that the request-phase headers have alrealy been verified as 
* valid and that the response phase headers have already been sent.
*
* fwsock:	Client socket stream 
* url:		Clean, but unverified url requested in the GET request
*
*****************************************************************************/
static int sendBody(FILE *fwsock, char *url)
{
	int		fd;
	char	buf[1000];
	ssize_t	len;
	size_t	totalLen = 0;
	size_t	fwstat = 0;

	debug("sendBody url: '%s'\n", url);

	/* Make sure noone is sending us an URL that is too big */
	len = strlen(url) + strlen(documentRoot);
	if (len > sizeof(buf))
	{
		debug("sendBody: url and root strings are too long %d\n", len);
		fprintf(fwsock, "Error, no such file\r\n");
		return(-1);
	}
	sprintf(buf, "%s%s", documentRoot, url);

	if ((fd = open(buf, O_RDONLY)) < 0)
	{
		debug("sendBody: could not open file\n", url);
		fprintf(fwsock, "Error, no such file\r\n");
		return(-1);
	}

	debug("sendBody: FD %d opened '%s'\n", fd, buf);
	while ((len = read(fd, buf, sizeof(buf))) > 0)
	{
		totalLen += len;
		fwrite(buf, 1, len, fwsock);
	}
	if (len < 0)
	{
		debug("read() failed in sendBody(): %s\n", strerror(errno));
	}
	if (fwstat = 0)
	{
		debug("fwrite() failed in sendBody()\n");
	}
	
	debug("sendBody: file sent %d bytes\n", totalLen);
	close(fd);
	return(0);
}
