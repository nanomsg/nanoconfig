#include <nanomsg/nn.h>

#include "worker.h"
#include "utils/err.h"

static struct nc_state self;

enum nc_command_tag {
    NC_CONFIGURE = 1,
    NC_CLOSE = 2,
    NC_SHUTDOWN = 99
};

struct nc_command_close {
    int tag;
    int socket;
};

struct nc_command_shutdown {
    int tag;
};

struct nc_command_configure {
    int tag;
    int socket;
    char name[];
};


static void nc_start() {
    char addr;
    int rc;

    if (self.initialized)
        return;

    addr = getenv ("NN_CONFIG_SERVICE");
    if (!addr) {
        fprintf (stderr, "nanoconfig: NN_CONFIG_SERVICE must be set");
        abort();
    }

    self.request_socket = nn_socket (AF_SP, NN_REQ);
    if (self.request_socket < 0) {
        fprintf (stderr, "nanoconfig: Can't create nanomsg socket: %s",
            nn_strerror(errno));
        abort();
    }

    rc = nn_connect (self.request_socket, addr);
    if (rc < 0) {
        fprintf (stderr,
            "nanoconfig: Can't connect to configuration service: %s",
            nn_strerror(errno));
        abort();
    }

    self.worker_socket = nn_socket (AF_SP, PUSH);
    if (self.request_socket < 0) {
        fprintf (stderr, "nanoconfig: Can't create nanomsg socket: %s",
            nn_strerror(errno));
        abort();
    }

    rc = nn_connect (self.worker_socket, "inproc://nanoconfig-worker");
    if (rc < 0) {
        fprintf (stderr,
            "nanoconfig: Can't connect inproc sokcet: %s",
            nn_strerror(errno));
        abort();
    }

    nc_worker_start(&self);

    self.initialized = 1;
}

int nc_configure (int sock, char *url) {
    int rc;
    struct nc_command_configure *cmd;
    int cmdlen;
    int err;

    nc_start();

    cmdlen = sizeof (*cmd) + strlen (url) + 1;
    cmd = nn_allocmsg (cmdlen, 0);
    if (!cmd)
        return -1;

    cmd->tag = NC_CONFIGURE;
    cmd->socket = sock;
    strcpy (cmd->name, url);

    if (!nc_validate_url (url)) {
        errno = EINVAL;
        return -1;
    }

    rc = nn_send (self.worker_socket, &cmd, sizeof(cmd), 0);
    if (rc < 0) {
        err = errno;
        nn_freemsg (cmd);
        errno = err;
        return -1;
    }
    return 0;
}

void nc_close (int sock) {
    int rc;
    struct nc_command_close cmd = { NC_CLOSE, sock };

    if (!self.initialized) {
        nn_close(sock);
        return;
    }

    rc = nn_send (self.worker_socket, &cmd, sizeof(cmd), 0);
    if (rc < 0) {
        if (errno == ETERM)
            return;
        errno_assert (rc < 0);
    }
}

void nc_term () {
    int rc;
    struct nc_command_shutdown cmd = { NC_SHUTDOWN };

    if (!self.initialized) {
        return;
    }

    rc = nn_send (self.worker_socket, &cmd, sizeof(cmd), 0);
    if (rc < 0) {
        if (errno != ETERM) {
            errno_assert (rc < 0);
        }
    }
    nn_worker_stop (&self);
}
