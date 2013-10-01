#include <stdlib.h>

#include <nanomsg/nn.h>
#include <nanomsg/reqrep.h>
#include <nanomsg/pubsub.h>
#include <nanomsg/pipeline.h>

#include "worker.h"
#include "utils/err.h"

static struct nc_state self;

static void nc_setup_request_socket ()
{
    char *addr;
    int rc;

    addr = getenv ("NN_CONFIG_SERVICE");
    if (!addr) {
        fprintf (stderr, "nanoconfig: NN_CONFIG_SERVICE must be set");
        nn_err_abort();
    }

    self.request_socket = nn_socket (AF_SP_RAW, NN_REQ);
    if (self.request_socket < 0) {
        fprintf (stderr, "nanoconfig: Can't create nanomsg socket: %s",
            nn_strerror(errno));
        nn_err_abort();
    }

    rc = nn_connect (self.request_socket, addr);
    if (rc < 0) {
        fprintf (stderr,
            "nanoconfig: Can't connect to configuration service: %s",
            nn_strerror(errno));
        nn_err_abort();
    }
}

static void nc_setup_updates_socket ()
{
    char *addr;
    int rc;

    addr = getenv ("NN_CONFIG_UPDATES");
    if (addr) {

        self.updates_socket = nn_socket (AF_SP, NN_SUB);
        if (self.updates_socket < 0) {
            fprintf (stderr, "nanoconfig: Can't create nanomsg socket: %s",
                nn_strerror(errno));
            abort();
        }

        rc = nn_connect (self.updates_socket, addr);
        if (rc < 0) {
            fprintf (stderr,
                "nanoconfig: Can't connect to configuration service: %s",
                nn_strerror(errno));
            nn_err_abort();
        }


    } else {
        self.updates_socket = -1;
    }

}


static void nc_setup_worker_socket ()
{
    int rc;

    self.worker_socket = nn_socket (AF_SP, NN_PUSH);
    if (self.worker_socket < 0) {
        fprintf (stderr, "nanoconfig: Can't create nanomsg socket: %s",
            nn_strerror(errno));
        nn_err_abort();
    }

    rc = nn_connect (self.worker_socket, "inproc://nanoconfig-worker");
    if (rc < 0) {
        fprintf (stderr,
            "nanoconfig: Can't connect inproc socket: %s",
            nn_strerror(errno));
        nn_err_abort();
    }
}


static void nc_start() {
    char addr;
    int rc;

    if (self.initialized)
        return;

    nc_setup_request_socket();
    nc_setup_updates_socket();
    nc_setup_worker_socket();


    nc_worker_start(&self);

    self.initialized = 1;
}

int nc_validate_url (char *url) {
    if (strlen (url) > NC_URL_MAX)
        return 0;
    if (strncmp (url, "nanoconfig://", 13))
        return 0;
    /*  Barely checking that characters are printable and not space.
        May implement more comprehensive URL checks here */
    for (;*url; ++url)
        if (!isprint (*url) || isblank (*url))
            return 0;
    return 1;
}

int nc_configure (int sock, char *url) {
    int rc;
    struct nc_command_configure *cmd;
    int cmdlen;
    int err;

    if (!nc_validate_url (url)) {
        errno = EINVAL;
        return -1;
    }

    nc_start();

    cmdlen = sizeof (*cmd) + strlen (url) + 1;
    cmd = nn_allocmsg (cmdlen, 0);
    if (!cmd)
        return -1;

    cmd->tag = NC_CONFIGURE;
    cmd->socket = sock;
    strcpy (cmd->url, url);


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
