/*
    Copyright (c) 2013 Insollo Entertainment, LLC.  All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom
    the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/
#include <poll.h>
#include <stdint.h>
#include <arpa/inet.h>

#include <nanomsg/nn.h>
#include <nanomsg/pipeline.h>
#include <nanomsg/pubsub.h>

#include "utils/thread.h"
#include "utils/err.h"
#include "utils/random.h"
#include "utils/alloc.h"
#include "utils/clock.h"
#include "utils/msgpack.h"
#include "worker.h"

/*  Amount of ms to wait if nn_send failed or reply is invalid  */
#define NC_ERROR_RETRY_TIME 100
/*  Time for reply. 1 sec should be enough nowadays  */
#define NC_REQUEST_WAIT_TIME 1000
/*  Check for NS record updates every 5 minutes  */
#define NC_REQUEST_AGAIN_TIME (5*60*1000)

#define min(a, b) ((a) > (b) ? (b) : (a))

enum {
    NC_POLL_CMD_RCVFD,
    NC_POLL_REQUEST_RCVFD,
    NC_POLL_UPDATES_RCVFD,
    NC_POLL_NUM
};

enum {
    NC_STATE_STARTING,
    NC_STATE_REQUEST_SENT,
    NC_STATE_SLEEPING,
};


struct nc_topic {
    struct nc_topic *next;
    struct nc_topic **prev;

    struct nc_topic_subscr_list {
        struct nc_subscription *head;
        struct nc_subscription **tail;
    } subscr_list;

    int topic_len;
    char topic [];
};


struct nc_subscription {
    struct nn_socket_list {
        struct nc_subscription **prev;
        struct nc_subscription *next;
    } socket_list;
    struct nn_topic_list {
        struct nc_subscription **prev;
        struct nc_subscription *next;
    } topic_list;

    int visited;
    struct nc_topic *topic;
    struct nc_socket *socket;
};


struct nc_socket {
    int socket_no;
    int state;
    uint32_t request_id;
    uint64_t sent_time;
    uint64_t retry_time;

    struct nc_socket **prev;
    struct nc_socket *next;

    struct nc_sock_subscr_list {
        struct nc_subscription *head;
        struct nc_subscription **tail;
    } subscr_list;
    struct nc_endpoint_list {
        struct nc_endpoint *head;
        struct nc_endpoint **tail;
    } endpoint_list;

    /*  A protocol name to use for socket option matching and in request  */
    int protocol_len;
    const char *protocol;
    int protoid;
    /*  A buffer for name request  */
    int request_len;
    char request [];
};

struct nc_endpoint {
    int eid;
    int visited;

    struct nc_endpoint **prev;
    struct nc_endpoint *next;

    int addr_len;
    char addr[];
};

struct nc_worker {
    int cmd_socket;
    int cmd_rcvfd;
    int request_socket;
    int request_rcvfd;
    int updates_socket;
    int updates_rcvfd;
    int running;
    uint32_t next_request_id;
    struct nn_clock clock;

    /*  TODO(tailhook) add ifdefs for Windows */
    struct pollfd fds [NC_POLL_NUM];

    struct nc_socket_list {
        struct nc_socket *head;
        struct nc_socket **tail;
    } socket_list;
    struct nc_topic_list {
        struct nc_topic *head;
        struct nc_topic **tail;
    } topic_list;
};

static struct nc_worker worker_struct;


static void nc_list_add (struct nc_worker *self, struct nc_socket *sock)
{
    sock->prev = self->socket_list.tail;
    *sock->prev = sock;
    self->socket_list.tail = &sock->next;
    sock->next = NULL;
}

static void nc_list_rm (struct nc_socket *sock)
{
    *sock->prev = sock->next;
    if (sock->next)
        sock->next->prev  = sock->prev;
}

static struct nc_socket *nc_list_find (struct nc_worker *self, int num)
{
    struct nc_socket *res;

    for (res = self->socket_list.head; res; res = res->next) {
        if (res->socket_no == num)
            break;
    }

    return res;
}


static void nc_setup_cmd_socket (struct nc_worker *self) {
    int rc;

    self->cmd_socket = nn_socket (AF_SP, NN_PULL);
    if (self->cmd_socket < 0) {
        fprintf (stderr, "nanoconfig: Can't create nanomsg socket: %s",
            nn_strerror(errno));
        nn_err_abort();
    }

    rc = nn_bind (self->cmd_socket, "inproc://nanoconfig-worker");
    if (rc < 0) {
        fprintf (stderr,
            "nanoconfig: Can't connect inproc socket: %s",
            nn_strerror(errno));
        nn_err_abort();
    }
}

static void nc_setup_pollfd (struct nc_worker *self)
{
    size_t optlen;
    int rc;

    optlen = sizeof(int);

    rc = nn_getsockopt (self->cmd_socket, NN_SOL_SOCKET, NN_RCVFD,
        &self->cmd_rcvfd, &optlen);
    errno_assert (rc >= 0);
    nn_assert (self->cmd_rcvfd >= 0);
    self->fds [NC_POLL_CMD_RCVFD].fd = self->cmd_rcvfd;
    self->fds [NC_POLL_CMD_RCVFD].events = POLLIN;

    rc = nn_getsockopt (self->request_socket, NN_SOL_SOCKET, NN_RCVFD,
        &self->request_rcvfd, &optlen);
    errno_assert (rc >= 0);
    nn_assert (self->request_rcvfd >= 0);
    self->fds [NC_POLL_REQUEST_RCVFD].fd = self->request_rcvfd;
    self->fds [NC_POLL_REQUEST_RCVFD].events = POLLIN;

    if (self->updates_socket >= 0) {
        rc = nn_getsockopt (self->updates_socket, NN_SOL_SOCKET, NN_RCVFD,
            &self->updates_rcvfd, &optlen);
        errno_assert (rc >= 0);
        nn_assert (self->updates_rcvfd >= 0);
        self->fds [NC_POLL_UPDATES_RCVFD].fd = self->updates_rcvfd;
        self->fds [NC_POLL_UPDATES_RCVFD].events = POLLIN;
    }
}

static int nc_poll_exec (struct nc_worker *self, int timeout)
{
    int num;
    int rc;

    num = NC_POLL_NUM;
    if(self->updates_socket < 0) {
        num -= 1;
    }

    /*  TODO(tailhook) insert ifdefs for windows' WSAPoll  */
    rc = poll (self->fds, num, timeout);

    if (rc < 0) {
        if (errno == EINTR)
            return 0;
        errno_assert (rc > 0);
    }
    return rc;
}

static void nc_process_configure (struct nc_worker *self,
    struct nc_command_configure *cmd)
{
    struct nc_socket *sock;
    int rc;
    int urllen;
    int reqlen;
    int proto;
    size_t optlen;
    int i;
    int constval;
    int constlen;
    const char *nconst;
    char *target;

    sock = nc_list_find (self, cmd->socket);

    if (sock) {
        sock->state = NC_STATE_STARTING;  /*  Will resend request ASAP  */
    } else {
        optlen = sizeof(proto);

        rc = nn_getsockopt (cmd->socket,
            NN_SOL_SOCKET, NN_PROTOCOL, &proto, &optlen);
        errno_assert (rc >= 0);
        nn_assert (optlen == sizeof(int));

        for (i = 0; ; ++i) {
            nconst = nn_symbol (i, &constval);
            nn_assert (nconst); /*  Must break before the end of the list  */
            if (constval == proto)
                break;
        }

        constlen = strlen(nconst);

        urllen = strlen(cmd->url);
        reqlen = 8;  /*  Request id  */
        reqlen += strlen("RESOLVE ");
        reqlen += urllen;
        reqlen += 1;  /*  A space  */
        reqlen += constlen;

        sock = nn_alloc (sizeof (struct nc_socket) + reqlen, "socket_str");
        sock->state = NC_STATE_STARTING;
        sock->socket_no = cmd->socket;
        /*  The request looks like:
            "\0\0\0\0" + REQUEST_ID(4 bytes) + "RESOLVE "+URL+" "+NN_SOCK_TYPE

            The first 4 bytes are the fake channel id. It's stripped by xrep
            socket and put to the header.

            Since nn_recvmsg doesn't work well with ancillary data yet, we
            don't receive the header, and need to read REQUEST ID from the
            body.  */
        memset (sock->request, 0, 8);
        target = sock->request + 8;  /*  Requestid is filled later  */
        memcpy (target, "RESOLVE ", strlen("RESOLVE "));
        target += strlen("RESOLVE ");
        memcpy (target, cmd->url, urllen);
        target += urllen;
        *target++ = ' ';
        memcpy (target, nconst, constlen);
        sock->request_len = reqlen;
        sock->protocol = nconst;
        sock->protocol_len = constlen;
        sock->protoid = proto;
        sock->subscr_list.head = NULL;
        sock->subscr_list.tail = &sock->subscr_list.head;
        sock->endpoint_list.head = NULL;
        sock->endpoint_list.tail = &sock->endpoint_list.head;

        nc_list_add (self, sock);
    }
}

static void nc_report_error (struct nc_worker *self, char *str, int len)
{
    (void) self;
    /*  TODO(tailhook) put error log somewhere else  */
    if (len < 0) {
        fprintf (stderr, "nanoconfig: %s\n", str);
    } else {
        fprintf (stderr, "nanoconfig: %.*s\n", len, str);
    }
}

static void nc_report_errno (struct nc_worker *self, char *str, int errcode)
{
    (void) self;
    /*  TODO(tailhook) put error log somewhere else  */
    fprintf (stderr, "nanoconfig: %s: %s\n", str, nn_strerror (errcode));
}

static void nc_process_close (struct nc_worker *self,
    struct nc_command_close *cmd)
{
    struct nc_socket *sock;

    sock = nc_list_find (self, cmd->socket);
    if (!sock) {
        /*  Allow not configured sockets to close this is to avoid too much
            checking for which sockets are configured by nanoconfig and which
            aren't by user  */
        nn_close (cmd->socket);
        return;
    }

    nc_list_rm (sock);
    nn_close (cmd->socket);
}


static void nc_process_commands (struct nc_worker *self)
{
    char buf [NC_URL_MAX + 50];
    int rc;
    int tag;

    while (1) {
        rc = nn_recv (self->cmd_socket, buf, sizeof (buf), NN_DONTWAIT);
        if (rc < 0) {
            if (errno == EAGAIN)
                return;
            if (errno == ETERM) {
                self->running = 0;
                return;
            }
            errno_assert (rc >= 0);
        }
        nn_assert (rc >= (int) sizeof (int));
        tag = *(int *)buf;

        switch (tag)
        {
        case NC_CONFIGURE:
            nc_process_configure (self, (struct nc_command_configure *)buf);
            break;
        case NC_CLOSE:
            nc_process_close (self, (struct nc_command_close *)buf);
            break;
        case NC_SHUTDOWN:
            self->running = 0;
            return;
        default:
            nn_assert (0);
        }
    }
}

static int nc_parse_and_set_option (struct nc_worker *worker,
    int sock, int optlev, int optid,
    char **buf, int *buflen)
{
    int intopt;
    char *stropt;
    int stroptlen;
    int optarrlen;
    int rc;
    int j;

    if (nc_mp_parse_int (buf, buflen, &intopt)) {
        rc = nn_setsockopt (sock, optlev, optid, &intopt, sizeof (intopt));
        if (rc < 0)
            nc_report_errno (worker, "Failed to set option", errno);
    } else if (nc_mp_parse_string (buf, buflen,
        &stropt, &stroptlen)) {
        rc = nn_setsockopt (sock, optlev, optid,
            stropt, stroptlen);
        if (rc < 0)
            nc_report_errno (worker, "Failed to set option", errno);
    } else if (nc_mp_parse_array (buf, buflen, &optarrlen)) {
        for (j = 0; j < optarrlen; ++j) {
            if (nc_mp_parse_int (buf, buflen, &intopt)) {
                rc = nn_setsockopt (sock, optlev, optid,
                    &intopt, sizeof(intopt));
                if (rc < 0)
                    nc_report_errno (worker, "Failed to set option", errno);
            } else if (nc_mp_parse_string (buf, buflen,
                                           &stropt, &stroptlen)) {
                rc = nn_setsockopt (sock, optlev, optid,
                    stropt, stroptlen);
                if (rc < 0)
                    nc_report_errno (worker, "Failed to set option", errno);
            }
        }
    } else {
        nc_report_error (worker, "Failed to parse option", -1);
        return 0;
    }
    return 1;
}

static int nc_parse_and_apply (struct nc_worker *worker,
    struct nc_socket *self, char *buf, int buflen)
{
    /*  Reply structure (JSON-like syntax)
     *  Error:
     *    [0,  # Error marker
     *      1234,  # Error code
     *      "Error: no such node"]  # Error message
     *  Success:
     *    [1,  # Success marker
     *      {  # Socket-level options (NN_SOL_SOCKET , NN_pattern)
     *         # Any constant found by nn_symbol may be specified
     *         # Type is derived from msgpack type
     *         # Level is derived from the option name
     *         # (e.g. NN_SUB_SUBSCRIBE has level NN_SUB)
     *          'NN_SOCKET_NAME': "worker",
     *          'NN_LINGER': 1000,
     *         # Multiple options sets may be specified using:
     *          'NN_SUB_SUBSCRIBE': ['abc', 'def'],
     *         # Probably useful only for subscriptions
     *      },
     *      [
     *          # Zero for bind, and one for connect
     *          [0, "tcp://127.0.0.1:1234", {
     *              'NN_TCP_NODELAY': 1,
     *              'NN_SNDPRIO': 10,
     *              #  Any transport- or endpoint-specific option
     *          }],
     *      ],
     *      ["org.example.worker"],  # Subscriptions for updates (optional)
     *    ]
     */

    int ralen;
    int retcode;
    int errcode;
    char *errstr;
    int errstrlen;
    char *optname;
    int optnamelen;
    int i, j, k;
    int rc;
    const char *symname;
    int symval;
    int optpairs;
    int optid;
    int optlev;
    int addressnum;
    int tuplelen;
    int kind;
    char *addr;
    int addrlen;
    char *colonchar;
    int translen;
    int transid;
    const char *transpref;
    int topicnum;
    char *topic;
    int topiclen;
    struct nc_topic *tnode;
    struct nc_subscription *sub;
    struct nc_subscription *nsub;
    struct nc_endpoint *ep;
    struct nc_endpoint *nep;

    if (!nc_mp_parse_array (&buf, &buflen, &ralen)) {
        nc_report_error (worker, "Response is not an array", -1);
        return 0;
    }
    if (!nc_mp_parse_int (&buf, &buflen, &retcode)) {
        nc_report_error (worker, "First element is not an int", -1);
        return 0;
    }
    if (retcode == 0) {
        /*  TODO(tailhook) report error somehow  */
        if (ralen < 3) {
            nc_report_error (worker, "Wrong length of error return array", -1);
        } else if (!nc_mp_parse_int (&buf, &buflen, &errcode)) {
            nc_report_error (worker, "No error code in error message", -1);
        } else if (!nc_mp_parse_string (&buf, &buflen, &errstr, &errstrlen)) {
            nc_report_error (worker, errstr, errstrlen);
        }
        return 0;
    }
    if (retcode != 1)
        return 0;
    if (ralen < 3) {
        nc_report_error (worker, "Success result tuple is too short", -1);
        return 0;
    }
    if (!nc_mp_parse_mapping (&buf, &buflen, &optpairs)) {
        nc_report_error (worker, "Socket options is not a mapping", -1);
        return 0;
    }
    for (i = 0; i < optpairs; ++i) {
        if (!nc_mp_parse_string (&buf, &buflen, &optname, &optnamelen)) {
            nc_report_error (worker, "Socket option key is not a string", -1);
            return 0;
        }
        for (j = 0; ; ++j) {
            symname = nn_symbol (j, &symval);
            if (!symname)
                break;
            if (!strncmp(symname, optname, optnamelen) &&
                symname [optnamelen] == 0)
            {
                optid = symval;
                break;
            }
        }
        if (!symname) {
            nc_report_error (worker, "Socket option not found", -1);
            if (!nc_mp_skip_value (&buf, &buflen)) {
                nc_report_error (worker,
                    "Failed to skip unknown socket option", -1);
                return 0;
            }
            continue;
        }
        /*  If prefixed by protocol name then it's protocol level, else it's
         *  NN_SOL_SOCKET level option  */
        if (self->protocol_len < optnamelen &&
            !memcmp (optname, self->protocol, self->protocol_len) &&
            optname [self->protocol_len] == '_') {
            optlev = self->protoid;
        } else {
            optlev = NN_SOL_SOCKET;
        }
        if (!nc_parse_and_set_option (worker, self->socket_no, optlev, optid,
            &buf, &buflen))
            return 0;
    }

    if (!nc_mp_parse_array (&buf, &buflen, &addressnum)) {
        nc_report_error (worker, "Address list is not an array", -1);
        return 0;
    }

    for (ep = self->endpoint_list.head; ep; ep=ep->next) {
        ep->visited = 0;
    }

    for (i = 0; i < addressnum; ++i) {
        if (!nc_mp_parse_array (&buf, &buflen, &tuplelen)) {
            nc_report_error (worker, "Address not a tuple (array)", -1);
            return 0;
        }
        if (tuplelen < 2 || tuplelen > 3) {
            nc_report_error (worker, "Address tuple has wrong length", -1);
            return 0;
        }
        if (!nc_mp_parse_int (&buf, &buflen, &kind) || (
            kind != 0 && kind != 1))
        {
            nc_report_error (worker, "Wrong address kind", -1);
            return 0;
        }
        if (!nc_mp_parse_string (&buf, &buflen, &addr, &addrlen)) {
            nc_report_error (worker, "Address is not a string", -1);
            return 0;
        }
        colonchar = strchr (addr, ':');
        if (!colonchar) {
            nc_report_error (worker, "Address has no transport name", -1);
            return 0;
        }
        translen = colonchar - addr;

        for (j = 0; ; ++j) {
            symname = nn_symbol (j, &symval);
            if (!symname)
                break;
            if (!strncmp (symname, "NN_", 3) &&
                !strncasecmp (symname+3, addr, translen) &&
                symname [translen+3] == 0)
            {
                transpref = symname;
                transid = symval;
                translen += 3;
                break;
            }
        }
        if (!symname) {
            nc_report_error (worker, "Transport not found", -1);
            for (j = 2; i < tuplelen; ++j) {
                if (!nc_mp_skip_value (&buf, &buflen)) {
                    nc_report_error (worker,
                        "Failed to skip unknown transport", -1);
                    return 0;
                }
            }
            continue;
        }

        for (ep = self->endpoint_list.head; ep; ep = ep->next) {
            if (ep->addr_len == addrlen
                && !(memcmp (ep->addr, addr, addrlen)))
                break;
        }

        if (tuplelen > 2) {
            if (!nc_mp_parse_mapping (&buf, &buflen, &optpairs)) {
                nc_report_error (worker, "Options are not a mapping", -1);
                return 0;
            }
            for (j = 0; j < optpairs; ++j) {
                if (!nc_mp_parse_string (&buf, &buflen,
                                         &optname, &optnamelen))
                {
                    nc_report_error (worker,
                        "Socket option key is not a string", -1);
                    return 0;
                }
                for (k = 0; ; ++k) {
                    symname = nn_symbol (k, &symval);
                    if (!symname)
                        break;
                    if (!strncmp (symname, optname, optnamelen) &&
                        symname [optnamelen] == 0)
                    {
                        optid = symval;
                        break;
                    }
                }
                if (!symname) {
                    nc_report_error (worker, "Socket option not found", -1);
                    if (!nc_mp_skip_value (&buf, &buflen)) {
                        nc_report_error (worker,
                            "Failed to skip unknown option", -1);
                        return 0;
                    }
                }
                /*  If prefixed by transport name then it's transport level,
                 *  else it's NN_SOL_SOCKET level option  */
                if (self->protocol_len < optnamelen &&
                    !memcmp (optname, transpref, translen) &&
                    optname [translen] == '_') {
                    optlev = transid;
                } else {
                    optlev = NN_SOL_SOCKET;
                }
                if (!nc_parse_and_set_option (worker, self->socket_no,
                    optlev, optid, &buf, &buflen))
                    return 0;
            }
        }
        if (ep) {
            ep->visited = 1;
        } else {
            ep = nn_alloc (sizeof(struct nc_endpoint) + addrlen + 1,
                "nanoconfig_endpoint");
            ep->visited = 1;
            memcpy(ep->addr, addr, addrlen);
            ep->addr[addrlen] = 0;
            ep->addr_len = addrlen;

            if (kind == 0) {
                rc = nn_bind (self->socket_no, ep->addr);
                printf("NN_BIND ``%s'' %d\n", ep->addr, rc);
            } else if(kind == 1) {
                rc = nn_connect (self->socket_no, ep->addr);
                printf("NN_CONNECT ``%s'' %d\n", ep->addr, rc);
            }
            if (rc < 0)
                nc_report_errno (worker, "Can't set address", errno);

            ep->eid = rc;
            ep->next = NULL;
            ep->prev = self->endpoint_list.tail;
            *self->endpoint_list.tail = ep;
            self->endpoint_list.tail = &ep->next;
        }
    }

    for (ep = self->endpoint_list.head; ep; ep = nep) {
        nep = ep->next;
        if (ep->visited)
            continue;
        nn_assert (ep->eid > 0);

        rc = nn_shutdown (self->socket_no, ep->eid);
        printf("NN_SHUTDOWN %d\n", ep->eid);
        if (rc < 0)
            nc_report_errno (worker, "Error shutting down endpoing", errno);

        *ep->prev = ep->next;
        if (ep->next)
            ep->next->prev = ep->prev;
        nn_free (ep);
    }

    for (sub = self->subscr_list.head; sub; sub = sub->socket_list.next) {
        sub->visited = 0;
    }

    if (ralen > 3) {
        if (!nc_mp_parse_array (&buf, &buflen, &topicnum)) {
            nc_report_error (worker, "Subscriptions must be array", -1);
            return 0;
        }
        for (i = 0; i < topicnum; ++i) {
            if (!nc_mp_parse_string (&buf, &buflen, &topic, &topiclen)) {
                nc_report_error (worker, "Subscription must be string", -1);
                return 0;
            }

            for (sub = self->subscr_list.head; sub;
                 sub = sub->socket_list.next) {
                if (sub->topic->topic_len == topiclen &&
                    !memcmp (sub->topic->topic, topic, topiclen)) {
                    sub->visited = 1;
                }
            }

            if (!sub) {
                sub = nn_alloc (sizeof (struct nc_subscription),
                                "subscription");
                sub->visited = 1;
                sub->socket = self;

                for (tnode = worker->topic_list.head; tnode;
                     tnode = tnode->next)
                {
                    if (tnode->topic_len == topiclen &&
                        !memcmp (tnode->topic, topic, topiclen))
                        break;
                }
                if (!tnode) {
                    tnode = nn_alloc (sizeof (struct nc_topic) + topiclen,
                        "topic");
                    memcpy (tnode->topic, topic, topiclen);
                    tnode->topic_len = topiclen;
                    tnode->subscr_list.head = NULL;
                    tnode->subscr_list.tail = &tnode->subscr_list.head;
                    *worker->topic_list.tail = tnode;
                    tnode->next = NULL;
                    worker->topic_list.tail = &tnode->next;
                    if (worker->updates_socket) {
                        rc = nn_setsockopt (worker->updates_socket,
                            NN_SUB, NN_SUB_SUBSCRIBE,
                            topic, topiclen);
                        if (rc < 0) {
                            nc_report_errno (worker, "Can't subscribe "
                                "for updates", errno);
                        }
                    }
                }
                sub->socket_list.next = NULL;
                sub->socket_list.prev = self->subscr_list.tail;
                *self->subscr_list.tail = sub;
                self->subscr_list.tail = &sub->socket_list.next;
                sub->topic_list.next = NULL;
                sub->topic_list.prev = tnode->subscr_list.tail;
                *tnode->subscr_list.tail = sub;
                tnode->subscr_list.tail = &sub->topic_list.next;
                sub->topic = tnode;
            }
        }
    }
    for (sub = self->subscr_list.head; sub; sub = nsub) {
        nsub = sub->socket_list.next;
        if (sub->visited)
            continue;

        tnode = sub->topic;
        *sub->topic_list.prev = sub->topic_list.next;
        if (sub->topic_list.next)
            sub->topic_list.next->topic_list.prev = sub->topic_list.prev;
        *sub->socket_list.prev = sub->socket_list.next;
        if (sub->socket_list.next)
            sub->socket_list.next->socket_list.prev = sub->socket_list.prev;
        nn_free (sub);

        if (tnode->subscr_list.head == NULL)  {
            *tnode->prev = tnode->next;
            if (tnode->next)
                tnode->next->prev = tnode->prev;

            if (worker->updates_socket) {
                rc = nn_setsockopt (worker->updates_socket,
                    NN_SUB, NN_SUB_UNSUBSCRIBE,
                    tnode->topic, tnode->topic_len);
                if (rc < 0) {
                    nc_report_errno (worker, "Can't subscribe "
                        "for updates", errno);
                }
            }

            nn_free (tnode);
        }
    }

    /*  Anything is allowed at the end of the list for forward compatibility */

    return 1;
}

static void nc_process_responses (struct nc_worker *self) {
    char *buf;
    int buflen;
    struct nc_socket *item;
    uint32_t request_id;

    buf = NULL;
    while (1) {
        if (buf)
            nn_freemsg(buf);
        buflen = nn_recv (self->request_socket, &buf, NN_MSG, NN_DONTWAIT);

        if (buflen < 0) {
            errno_assert (errno == EAGAIN);
            return;
        }

        if (buflen < 5)
            continue;  /*  Non-valid reply  */
        request_id = ntohl(*(uint32_t *)buf);
        if (!(request_id & 0x80000000u))
            continue;  /*  We're not endpoint for this message  */
        request_id &= ~0x80000000u;
        for (item = self->socket_list.head; item; item = item->next) {
            if (item->request_id == request_id) {
                if (item->state != NC_STATE_REQUEST_SENT)
                    break;  /*  Already has a reply or request reset  */
                item->state = NC_STATE_SLEEPING;
                if (nc_parse_and_apply (self, item, buf+4, buflen-4)) {
                    item->retry_time = nn_clock_now (&self->clock) +
                        NC_REQUEST_AGAIN_TIME;
                } else {
                    item->retry_time = nn_clock_now (&self->clock) +
                        NC_ERROR_RETRY_TIME;
                }
                break;
            }
        }
    }
}

static void nc_process_updates (struct nc_worker *self) {
    char *buf;
    int buflen;
    struct nc_subscription *sub;
    struct nc_topic *topic;

    while(1) {
        buflen = nn_recv (self->updates_socket, &buf, NN_MSG, NN_DONTWAIT);

        if (buflen < 0) {
            if (errno == EAGAIN)
                return;
            if (errno == EINTR || errno == ETERM)
                /*  Exit immediately if we shutting down,
                    but return shortly if not  */
                return;
            errno_assert (0);
        }

        for (topic = self->topic_list.head; topic; topic = topic->next) {
            if (buflen < topic->topic_len)
                continue;
            if (!memcmp (topic->topic, buf, topic->topic_len)) {
                for (sub = topic->subscr_list.head; sub;
                     sub = sub->topic_list.next)
                {
                    sub->socket->state = NC_STATE_STARTING;
                }
            }
        }
    }
}

static uint64_t nc_process_timers (struct nc_worker *self) {
    int rc;
    uint64_t now;
    uint64_t next_timeout;
    struct nc_socket *item;

    now = nn_clock_now (&self->clock);
    next_timeout = now + 60000;

    for (item = self->socket_list.head; item; item = item->next) {
        if (item->state != NC_STATE_STARTING && item->retry_time <= now) {
            item->state = NC_STATE_STARTING;
        } else {
            next_timeout = min (next_timeout, item->retry_time);
        }
        if (item->state == NC_STATE_STARTING) {
            item->request_id = self->next_request_id;
            self->next_request_id += 1;
            if (self->next_request_id == 0x80000000u)
                self->next_request_id = 0;
            *(uint32_t *)(item->request+4) =
                htonl(item->request_id | 0x80000000u);
            rc = nn_send (self->request_socket,
                item->request, item->request_len,
                NN_DONTWAIT);
            if (rc < 0) {
                if (errno == EINTR || errno == ETERM)
                    /*  Exit immediately if we shutting down,
                        but return shortly if not  */
                    return 0;
                if (errno == EAGAIN) {
                    item->retry_time = now + NC_ERROR_RETRY_TIME;
                    item->state = NC_STATE_SLEEPING;
                    next_timeout = min (next_timeout, item->retry_time);
                    continue;
                } else {
                    errno_assert (0);
                }
            }
            item->state = NC_STATE_REQUEST_SENT;
            item->retry_time = now + NC_REQUEST_WAIT_TIME;
            next_timeout = min (next_timeout, item->retry_time);
        }
    }
    now = nn_clock_now (&self->clock);
    if (next_timeout > now)
        return next_timeout - now;
    return 0;
}

static void nc_worker_loop (void *data)
{
    struct nc_worker *self;
    int timeo;
    int rev;

    self = data;

    nc_setup_cmd_socket (self);
    nc_setup_pollfd (self);

    while (1) {

        timeo = nc_process_timers (self);

        if (nc_poll_exec (self, timeo) > 0) {

            rev = self->fds [NC_POLL_CMD_RCVFD].revents;
            if (rev) {
                nn_assert (rev == POLLIN);
                nc_process_commands (self);
                if (!self->running)
                    return;
            }

            rev = self->fds [NC_POLL_REQUEST_RCVFD].revents;
            if (rev) {
                nn_assert (rev == POLLIN);
                nc_process_responses (self);
            }

            if (self->updates_socket >= 0) {
                rev = self->fds [NC_POLL_UPDATES_RCVFD].revents;
                if (rev) {
                    nn_assert (rev == POLLIN);
                    nc_process_updates (self);
                }
            }
        }

    }
};

void nc_worker_start (struct nc_state *state) {
    struct nc_worker *self;
    nn_random_seed();

    self = &worker_struct;
    self->request_socket = state->request_socket;
    self->updates_socket = state->updates_socket;
    self->socket_list.head = NULL;
    self->socket_list.tail = &self->socket_list.head;
    self->topic_list.head = NULL;
    self->topic_list.tail = &self->topic_list.head;
    self->running = 1;
    nn_clock_init (&self->clock);
    nn_random_generate (&self->next_request_id,
        sizeof (self->next_request_id));
    self->next_request_id &= ~0x80000000u;

    nn_thread_init (&state->worker, nc_worker_loop, self);

}

void nc_worker_term (struct nc_state *state) {
    nn_thread_term (&state->worker);
    nn_clock_term (&worker_struct.clock);
}

