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
#include "state.h"


enum {
    NC_POLL_CMD_RCVFD,
    NC_POLL_REQUEST_RCVFD,
    NC_POLL_UPDATES_RCVFD,
    NC_POLL_NUM
};

struct nc_socket {
    int socket_no;
    uint64_t sent_time;
    uint64_t retry_time;

    struct nc_socket **prev;
    struct nc_socket *next;

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

    /*  TODO(tailhook) add ifdefs for Windows */
    struct pollfd fds[NC_POLL_NUM];

    struct nn_socket_list {
        struct nc_socket *head;
        struct nc_socket **tail;
    } socket_list;
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

    sock = nc_list_find (self, cmd->socket)

    if (sock) {

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
        reqlen = urllen = strlen(cmd->url);
        reqlen += 1;  /*  A space  */
        reqlen += constlen;


        sock = nn_alloc (sizeof (struct nc_socket) + reqlen, "socket_str");
        sock->socket_no = cmd->socket;
        /*  The request looks like "URL NN_SOCK_TYPE" */
        memcpy (sock->request, urllen);
        sock->request [urllen] = ' ';
        memcpy (sock->request [urllen + 1], nnconst, constlen);
        sock->request_len = reqlen;

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
        nn_close(cmd->socket);
        return;
    }

    nc_list_rm (self, sock);
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

        timeo = nc_process_timeouts (self);

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

    self = &worker_struct;
    self->request_socket = state->request_socket;
    self->update_socket = state->update_socket;
    self->socket_list.head = NULL;
    self->socket_list.tail = &self->socketlist.head;
    self->running = 1;

    nn_thread_init (&state->worker, nc_worker_loop, self);

}

void nc_worker_term (struct nc_state *state) {
    nn_thread_term (&state->worker);
}

