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

#include <nanomsg/nn.h>

#include "utils/thread.h"
#include "utils/err.h"
#include "utils/random.h"
#include "state.h"

/*  Amount of ms to wait if nn_send failed or reply is invalid  */
#define NC_ERROR_RETRY_TIME 100
/*  Time for reply. 1 sec should be enough nowadays  */
#define NC_REQUEST_WAIT_TIME 1000
/*  Check for NS record updates every 5 minutes  */
#define NC_REQUEST_AGAIN_TIME (5*60*1000)

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

    struct nc_subscr_list {
        struct nc_subscription *head;
        struct nc_subscription **tail;
    } subscr_list;

    int topic_len;
    char topic[];
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

    struct nc_subscr_list {
        struct nc_subscription *head;
        struct nc_subscription **tail;
    } subscr_list;

    /*  A buffer for name request  */
    int request_len;
    char request[];
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

    /*  TODO(tailhook) add ifdefs for Windows */
    struct pollfd fds[NC_POLL_NUM];

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
    self->socket_list.tail = &sock->next;
    sock->next = NULL;
}

static void nc_list_rm (struct nc_worker *self, struct nc_socket *sock)
{
    sock->prev = &sock->next;
}

static struct nc_socket *nc_list_find (struct nc_worker *self, int num)
{
    struct nc_socket *res;

    for (res = self->head; res; res = res->next) {
        if (res->socket_no == num)
            break;
    }

    return res;
}


static void nc_setup_cmd_socket (struct nc_worker *self) {
    int rc;

    self->cmd_socket = nn_socket (AF_SP, PUSH);
    if (self->cmd_socket < 0) {
        fprintf (stderr, "nanoconfig: Can't create nanomsg socket: %s",
            nn_strerror(errno));
        abort();
    }

    rc = nn_bind (self->cmd_socket, "inproc://nanoconfig-worker");
    if (rc < 0) {
        fprintf (stderr,
            "nanoconfig: Can't connect inproc socket: %s",
            nn_strerror(errno));
        abort();
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
    nn_assert (self->cmd_rcvfd > = 0);
    self->fds [NC_POLL_CMD_RCVFD].fd = self->cmd_rcvfd;
    self->fds [NC_POLL_CMD_RCVFD].events = POLLIN;

    rc = nn_getsockopt (self->request_socket, NN_SOL_SOCKET, NN_RCVFD,
        &self->request_rcvfd, &optlen);
    errno_assert (rc >= 0);
    nn_assert (self->request_rcvfd > = 0);
    self->fds [NC_POLL_REQUEST_RCVFD].fd = self->cmd_rcvfd;
    self->fds [NC_POLL_REQUEST_RCVFD].events = POLLIN;

    if (self->updates_socket >= 0) {
        rc = nn_getsockopt (self->updates_socket, NN_SOL_SOCKET, NN_RCVFD,
            &self->updates_rcvfd, &optlen);
        errno_assert (rc >= 0);
        nn_assert (self->updates_rcvfd > = 0);
        self->fds [NC_POLL_UPDATES_RCVFD].fd = self->updates_rcvfd;
        self->fds [NC_POLL_UPDATES_RCVFD].events = POLLIN;
    }
}

static int nc_poll_exec (struct nc_worker *self, int timeout)
{
    int num;

    num = NN_POLL_NUM;
    if(self->updates_socket < 0) {
        num -= 1;
    }
    timeout = 60000;

    /*  TODO(tailhook) insert ifdefs for windows' WSAPoll  */
    rc = poll (self->pollfds, num, timeo);

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
    int proto;
    int optlen;
    int i;
    int constval;
    int constlen;
    char *nnconst;
    char *target;

    sock = nc_list_find (self, cmd->socket)

    if (sock) {
        sock->state = NC_STATE_STARTING;  /*  Will resend request ASAP  */
    } else {
        optlen = sizeof(proto);

        rc = nn_getsockopt (rc, NN_SOL_SOCKET, NN_PROTOCOL, &proto, &optlen);
        errno_assert (rc >= 0);
        nn_assert (optlen == sizeof(int));

        for (i = 0; ; ++i) {
            nnconst = nn_symbol (i, &constval);
            assert (nnconst); /*  Must break before the end of the list  */
            if (constval == proto)
                break;
        }

        constlen = strlen(nconst);

        urllen = strlen(cmd->url);
        reqlen = 4;  /*  Request id  */
        reqlen += strlen("RESOLVE ");
        reqlen += urllen;
        reqlen += 1;  /*  A space  */
        reqlen += constlen;

        sock = nn_alloc (sizeof (struct nc_socket) + reqlen, "socket_str");
        sock->state = NC_STATE_STARTING;
        sock->socket_no = cmd->socket;
        /*  The request looks like:
            REQUEST_ID(4 bytes) + "RESOLVE " + URL + " " + NN_SOCK_TYPE
            */
        target = sock->request + 4;  /*  Requestid is filled later  */
        memcpy (target, "RESOLVE ", strlen("RESOLVE "));
        target += strlen("RESOLVE ");
        memcpy (target, cmd->url, urllen);
        target += urllen;
        *target++ = ' ';
        memcpy (target, nnconst, constlen);
        sock->request_len = reqlen;
        sock->subscr_list.head = NULL;
        sock->subscr_list.tail = &sock->subscr_list.head;

        nc_list_add (self, sock);
    }
}

static void nc_process_close (struct nc_worker *self,
    struct nc_command_close *cmd)
{
    struct nc_socket *sock;

    sock = nc_list_find (self, cmd->socket)
    if (!sock) {
        /*  Allow not configured sockets to close this is to avoid too much
            checking for which sockets are configured by nanoconfig and which
            aren't by user  */
        nn_close (cmd->socket);
        return;
    }

    nc_list_rm (self, sock);
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
        assert (rc > sizeof (int));
        tag = *(int *)buf;

        switch (tag)
        {
        case NC_CONFIGURE:
            nc_process_configure (self, *(struct nc_command_configure *)buf);
            break;
        case NC_CLOSE:
            nc_process_close (self, *(struct nc_command_close *)buf);
            break;
        case NC_SHUTDOWN:
            self->running = 0;
            return;
        default:
            nn_assert (0);
        }
    }
}

static void nc_parse_and_apply (struct nc_worker *worker,
    struct nc_socket *self, char *buf, int buflen) {
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
     *          ["tcp://127.0.0.1:1234", {
     *              'NN_TCP_NODELAY': 1,
     *              'NN_SNDPRIO': 10,
     *              #  Any transport- or endpoint-specific option
     *          }],
     *      ],
     *      ["org.example.worker"],  # Subscriptions for updates (optional)
     *    ]
     */

    assert (0);
}

static void nc_process_responses (struct nc_worker *self) {
    char *buf;
    int buflen;
    int rc
    struct nn_socket *item;

    while(1) {
        buflen = nn_recv (self->request_socket, &buf, NN_MSG, NN_DONTWAIT);

        if (buflen < 0) {
            errno_assert (errno == EAGAIN);
            return;
        }

        if (buflen < 5)
            continue;  /*  Non-valid reply  */
        request_id = *(uint32_t *)buf;
        if (!(request_id & 0x80000000u))
            continue;  /*  We're not endpoint for this message  */
        request_id &= ~0x80000000u;
        for (item = self->socket_list.head; item; item = item->next) {
            if (item->request_id == request_id) {
                if (item->state != NC_STATE_REQUEST_SENT)
                    break;  /*  Already has a reply or request reset  */
                item->state = NC_STATE_SLEEPING;
                if (nc_parse_and_apply (self, item, buf, buflen)) {
                    item->retry_time = nc_clock_now (worker->clock) +
                        NC_REQUEST_AGAIN_TIME;
                } else {
                    item->retry_time = nc_clock_now (worker->clock) +
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
    int rc;
    struct nn_socket *item;
    struct nn_subscription *sub;
    struct nn_topic *topic;

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
                for (sub = topic->subscr_list.head; sub; sub = item->next) {
                    sub->socket->state = NC_STATE_STARTING;
                }
            }
        }
    }
}

static uint64_t nc_process_timers (struct nn_worker *self) {
    int rc;
    uint64_t now;
    uint64_t next_timeout;

    next_timeout = now + 60000;
    now = nn_clock_now (self);

    for (item = self->socket_list.head; item; item = item->next) {
        if (item->state != NC_STATE_STARTING && item->retry_time < now) {
            item->state = NC_STATE_STARTING;
        } else {
            next_timeout = min (next_timeout, item->retry_time);
        }
        if (item->state == NC_STATE_STARTING) {
            item->request_id = self->next_request_id;
            self->next_request_id += 1;
            if (self->next_request_id == 0x80000000u) {
                self->next_request_id = 0;
            *(uint32_t *)item->request =
                htonl(self->next_request_id) | 0x80000000u;
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
                } else {
                    errno_assert (0);
                }
            }
            item->state = NC_REQUEST_SENT;
            item->retry_type = NC_REQUEST_WAIT_TIME
        }
    }
    now = nn_clock_now (self);
    if (next_timeout > now)
        return now - next_timeout;
    return 0;
}

static void nc_worker_loop (void *data)
{
    struct nc_worker *self;
    int rc;
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

void nc_worker_start(struct nc_state *state) {
    struct nc_worker *self;
    nn_random_seed();

    self = &worker_struct;
    self->request_socket = state->request_socket;
    self->update_socket = state->update_socket;
    self->socket_list.head = NULL;
    self->socket_list.tail = &self->socketlist.head;
    self->topic_list.head = NULL;
    self->topic_list.tail = &self->socketlist.head;
    self->running = 1;
    nn_random_generate (&self->next_request_id, sizeof(self->next_request_id));

    nn_thread_init (&state->worker, nc_worker_loop, self);

}

void nc_worker_term (struct nc_state *state) {
    nn_thread_term (&state->worker);
}

