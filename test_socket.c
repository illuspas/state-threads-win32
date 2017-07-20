#include <stdio.h>
#include <ws2tcpip.h>

#include "st.h"

#define MAX_BIND_ADDRS 16
#define SERV_PORT_DEFAULT 8000
#define LISTENQ_SIZE_DEFAULT 256
#define MAXLINE 4096  /* max line length */

#define SEC2USEC(s) ((s)*1000000LL)
#define WAIT_THREADS(i)  (srv_socket[i].wait_threads)
#define BUSY_THREADS(i)  (srv_socket[i].busy_threads)
#define TOTAL_THREADS(i) (WAIT_THREADS(i) + BUSY_THREADS(i))
#define RQST_COUNT(i)    (srv_socket[i].rqst_count)
#define REQUEST_TIMEOUT 30

static int sk_count = 1;        /* Number of listening sockets          */
static int listenq_size = LISTENQ_SIZE_DEFAULT;
static int errfd        = STDERR_FILENO;

static int max_threads = 8;       /* Max number of threads         */
static int max_wait_threads = 4;  /* Max number of "spare" threads */
static int min_wait_threads = 2;  /* Min number of "spare" threads */

struct socket_info {
    st_netfd_t nfd;               /* Listening socket                     */
    char *addr;                   /* Bind address                         */
    unsigned int port;            /* Port                                 */
    int wait_threads;             /* Number of threads waiting to accept  */
    int busy_threads;             /* Number of threads processing request */
    int rqst_count;               /* Total number of processed requests   */
} srv_socket[MAX_BIND_ADDRS];   /* Array of listening sockets           */


char *err_tstamp(void)
{
    static char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    static char str[32];
    static time_t lastt = 0;
    struct tm *tmp;
    time_t currt = st_time();

    if (currt == lastt)
        return str;

    tmp = localtime(&currt);
    sprintf(str, "[%02d/%s/%d:%02d:%02d:%02d] ", tmp->tm_mday,
            months[tmp->tm_mon], 1900 + tmp->tm_year, tmp->tm_hour,
            tmp->tm_min, tmp->tm_sec);
    lastt = currt;

    return str;
}

static void err_doit(int fd, int errnoflag, const char *fmt, va_list ap)
{
    int errno_save;
    char buf[MAXLINE];

    errno_save = errno;         /* value caller might want printed   */
    strcpy(buf, err_tstamp());  /* prepend a message with time stamp */
    vsprintf(buf + strlen(buf), fmt, ap);
    if (errnoflag)
        sprintf(buf + strlen(buf), ": %s\n", strerror(errno_save));
    else
        strcat(buf, "\n");
    write(fd, buf, strlen(buf));
    errno = errno_save;
}

void err_sys_quit(int fd, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    err_doit(fd, 1, fmt, ap);
    va_end(ap);
    exit(1);
}

void err_sys_report(int fd, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    err_doit(fd, 1, fmt, ap);
    va_end(ap);
}

static void create_listeners(void)
{
    int i, n, sock;
    char *c;
    struct sockaddr_in serv_addr;
    struct hostent *hp;
    unsigned short port;

    for (i = 0; i < sk_count; i++) {
        port = 0;
        if ((c = strchr(srv_socket[i].addr, ':')) != NULL) {
            *c++ = '\0';
            port = (unsigned short) atoi(c);
        }
        if (srv_socket[i].addr[0] == '\0')
            srv_socket[i].addr = "0.0.0.0";
        if (port == 0)
            port = SERV_PORT_DEFAULT;

        /* Create server socket */
        if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0)
            err_sys_quit(errfd, "ERROR: can't create socket: socket %d",sock);
        n = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&n, sizeof(n)) < 0)
            err_sys_quit(errfd,"ERROR: can't set SO_REUSEADDR: setsockopt %d",sock);
        memset(&serv_addr, 0, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        serv_addr.sin_addr.s_addr = inet_addr(srv_socket[i].addr);
        if (serv_addr.sin_addr.s_addr == INADDR_NONE) {
            /* not dotted-decimal */
            if ((hp = gethostbyname(srv_socket[i].addr)) == NULL)
                err_sys_quit( errfd,"ERROR: can't resolve address: %s",srv_socket[i].addr);
            memcpy(&serv_addr.sin_addr, hp->h_addr, hp->h_length);
        }
        srv_socket[i].port = port;

        /* Do bind and listen */
        if (bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
            err_sys_quit(errfd, "ERROR: can't bind to address %s, port %hu",
                         srv_socket[i].addr, port);
        if (listen(sock, listenq_size) < 0)
            err_sys_quit(errfd, "ERROR: listen");

        /* Create file descriptor object from OS socket */
        if ((srv_socket[i].nfd = st_netfd_open_socket(sock)) == NULL)
            err_sys_quit(errfd, "ERROR: st_netfd_open_socket");
        /*
         * On some platforms (e.g. IRIX, Linux) accept() serialization is never
         * needed for any OS version.  In that case st_netfd_serialize_accept()
         * is just a no-op. Also see the comment above.
         */
        if (st_netfd_serialize_accept(srv_socket[i].nfd) < 0)
            err_sys_quit(errfd, "ERROR: st_netfd_serialize_accept");
    }
}


void handle_session(long srv_socket_index, st_netfd_t cli_nfd)
{
    static char resp[] = "HTTP/1.0 200 OK\r\nContent-type: text/html\r\n"
            "Connection: close\r\n\r\n<H2>It worked!</H2>\n";
    char buf[512];
    int n = sizeof(resp) - 1;
    struct in_addr *from = (struct in_addr*) st_netfd_getspecific(cli_nfd);

    if (st_read(cli_nfd, buf, sizeof(buf), SEC2USEC(REQUEST_TIMEOUT)) < 0) {
        err_sys_report(errfd, "WARN: can't read request from %s: st_read",
                       inet_ntoa(*from));
        return;
    }
    if (st_write(cli_nfd, resp, n, ST_UTIME_NO_TIMEOUT) != n) {
        err_sys_report(errfd, "WARN: can't write response to %s: st_write",
                       inet_ntoa(*from));
        return;
    }

    RQST_COUNT(srv_socket_index)++;
}

static void *handle_connections(void *arg)
{
    st_netfd_t srv_nfd, cli_nfd;
    struct sockaddr_in from;
    int fromlen;
    long i = (long) arg;

    srv_nfd = srv_socket[i].nfd;
    fromlen = sizeof(from);

    while (WAIT_THREADS(i) <= max_wait_threads) {
        cli_nfd = st_accept(srv_nfd, (struct sockaddr *)&from, &fromlen,
                            ST_UTIME_NO_TIMEOUT);
        if (cli_nfd == NULL) {
            err_sys_report(errfd, "ERROR: can't accept connection: st_accept");
            continue;
        }
        /* Save peer address, so we can retrieve it later */
        st_netfd_setspecific(cli_nfd, &from.sin_addr, NULL);

        WAIT_THREADS(i)--;
        BUSY_THREADS(i)++;
        if (WAIT_THREADS(i) < min_wait_threads && TOTAL_THREADS(i) < max_threads) {
            /* Create another spare thread */
            if (st_thread_create(handle_connections, (void *)i, 0, 0) != NULL)
                WAIT_THREADS(i)++;
            else
                err_sys_report(errfd, "ERROR: process %d (pid %d): can't create"
                        " thread", 0, 0);
        }

        handle_session(i, cli_nfd);

        st_netfd_close(cli_nfd);
        WAIT_THREADS(i)++;
        BUSY_THREADS(i)--;
    }

    WAIT_THREADS(i)--;
    return NULL;
}

static void start_threads(void)
{
    long i, n;


    /* Create connections handling threads */
    for (i = 0; i < sk_count; i++) {

        WAIT_THREADS(i) = 0;
        BUSY_THREADS(i) = 0;
        RQST_COUNT(i) = 0;
        for (n = 0; n < max_wait_threads; n++) {
            if (st_thread_create(handle_connections, (void *)i, 0, 0) != NULL)
                WAIT_THREADS(i)++;
            else
                err_sys_report(errfd, "ERROR: process %d (pid %d): can't create"
                        " thread", 0,0);
        }
        if (WAIT_THREADS(i) == 0)
            exit(1);
    }
}

int main(int argc, char** argv){
    if (st_init() < 0) {
        perror("ERROR: initialization failed: st_init");
        exit(1);
    }
    sk_count = 1;
    srv_socket[0].addr = "0.0.0.0";

    create_listeners();
    start_threads();
    st_thread_exit(NULL);
    return 0;
}