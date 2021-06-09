#include "utilityfunction.h"
#include <signal.h>
#include <ctype.h>
#include <termios.h>

/* ************************************************************************** */

ssize_t writen(int fd, const void *vptr, size_t n)
{
    size_t nleft;
    ssize_t nwritten;
    const char *ptr;

    ptr = vptr;
    nleft = n;
    while (nleft > 0) {
        if ( (nwritten = write(fd, ptr, nleft)) <= 0) {
            if (nwritten < 0 && errno == EINTR)
                nwritten = 0;   /* and call write() again */
            else
                return (-1);    /* error */
        }
        nleft -= nwritten;
        ptr += nwritten;
    }
    return (n);
}

/* ************************************************************************** */

ssize_t readn(int fd, void *vptr, size_t n)
{
    size_t  nleft;
    ssize_t nread;
    char   *ptr;

    ptr = vptr;
    nleft = n;
    while (nleft > 0) {
        if ( (nread = read(fd, ptr, nleft)) < 0) {
            if (errno == EINTR)
                nread = 0;      /* and call read() again */
            else
                return (-1);
        } else if (nread == 0)
            break;              /* EOF */

        nleft -= nread;
        ptr += nread;
    }
    return (n - nleft);         /* return >= 0 */
}

/* ************************************************************************** */

void manageSignal()
{
	signal(SIGINT , SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGHUP , SIG_IGN);
	signal(SIGILL , SIG_IGN);
	signal(SIGABRT, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
}

/* ************************************************************************** */

void clean_stdin()
{
   int stdin_copy = dup(STDIN_FILENO);
   /* remove garbage from stdin */
   tcdrain(stdin_copy);
   tcflush(stdin_copy, TCIFLUSH);
   close(stdin_copy);
}

/* ************************************************************************** */

char * trimwhitespace(char * s) {
    int l = strlen(s);
    while(isspace(s[l - 1])) --l;
    while(* s && isspace(* s)) ++s, --l;
    return strndup(s, l);
}
