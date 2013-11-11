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

#include <nanomsg/nn.h>
#include <nanomsg/pubsub.h>
#include <nanomsg/pipeline.h>
#include <nanomsg/bus.h>
#include <nanomsg/pair.h>
#include <nanomsg/survey.h>
#include <nanomsg/reqrep.h>

#include "../src/nanoconfig.h"
#include "options.h"
#include "../src/utils/sleep.c"
#include "../src/utils/clock.c"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#if !defined NN_HAVE_WINDOWS
#include <unistd.h>
#endif


typedef struct nn_options {
    /* Global options */
    int verbose;

    /* Socket options */
    int protocol;

	/* Topology name */
	char *topology;
} nn_options_t;


enum protocol {
    PROTO_SURVEY,
    PROTO_REQREP,
    PROTO_PUBSUB,
    PROTO_PIPELINE,
    PROTO_BUS,
    PROTO_PAIR
};

/*  Constants to get address of in option declaration  */
static const int proto_survey = PROTO_SURVEY;
static const int proto_reqrep = PROTO_REQREP;
static const int proto_pubsub = PROTO_PUBSUB;
static const int proto_pipeline = PROTO_PIPELINE;
static const int proto_bus = PROTO_BUS;
static const int proto_pair = PROTO_PAIR;


struct nn_enum_item protocols[] = {
    {"survey", PROTO_SURVEY},
    {"reqrep", PROTO_REQREP},
    {"pubsub", PROTO_PUBSUB},
    {"pipeline", PROTO_PIPELINE},
    {"bus", PROTO_BUS},
    {"pair", PROTO_PAIR},
    {NULL, 0},
};

/*  Constants for conflict masks  */
#define NN_MASK_PROTOCOL 1
#define NN_MASK_TOPOLOGY 2
#define NN_NO_PROVIDES 0
#define NN_NO_CONFLICTS 0
#define NN_NO_REQUIRES 0

struct nn_option nn_options[] = {
    /* Generic options */
    {"verbose", 'v', NULL,
     NN_OPT_INCREMENT, offsetof (nn_options_t, verbose), NULL,
     NN_NO_PROVIDES, NN_NO_CONFLICTS, NN_NO_REQUIRES,
     "Generic", NULL, "Increase verbosity of the nanocat"},
    {"silent", 'q', NULL,
     NN_OPT_DECREMENT, offsetof (nn_options_t, verbose), NULL,
     NN_NO_PROVIDES, NN_NO_CONFLICTS, NN_NO_REQUIRES,
     "Generic", NULL, "Decrease verbosity of the nanocat"},
    {"help", 'h', NULL,
     NN_OPT_HELP, 0, NULL,
     NN_NO_PROVIDES, NN_NO_CONFLICTS, NN_NO_REQUIRES,
     "Generic", NULL, "This help text"},

	{"protocol", 0, NULL,
	 NN_OPT_ENUM, offsetof (nn_options_t, protocol), &protocols,
	 NN_MASK_PROTOCOL, NN_MASK_PROTOCOL, NN_NO_REQUIRES,
     "Protocols", "PROTO", "Use protocol PROTO for the device"},
	{"survey", 0, NULL,
	 NN_OPT_SET_ENUM, offsetof (nn_options_t, protocol), &proto_survey,
	 NN_MASK_PROTOCOL, NN_MASK_PROTOCOL, NN_NO_REQUIRES,
     "Protocols", NULL, "Make survey device"},
	{"reqrep", 0, NULL,
	 NN_OPT_SET_ENUM, offsetof (nn_options_t, protocol), &proto_reqrep,
	 NN_MASK_PROTOCOL, NN_MASK_PROTOCOL, NN_NO_REQUIRES,
     "Protocols", NULL, "Make reqrep device"},
	{"pubsub", 0, NULL,
	 NN_OPT_SET_ENUM, offsetof (nn_options_t, protocol), &proto_pubsub,
	 NN_MASK_PROTOCOL, NN_MASK_PROTOCOL, NN_NO_REQUIRES,
     "Protocols", NULL, "Make pubsub device"},
	{"pipeline", 0, NULL,
	 NN_OPT_SET_ENUM, offsetof (nn_options_t, protocol), &proto_pipeline,
	 NN_MASK_PROTOCOL, NN_MASK_PROTOCOL, NN_NO_REQUIRES,
     "Protocols", NULL, "Make pipeline device"},
	{"bus", 0, NULL,
	 NN_OPT_SET_ENUM, offsetof (nn_options_t, protocol), &proto_bus,
	 NN_MASK_PROTOCOL, NN_MASK_PROTOCOL, NN_NO_REQUIRES,
     "Protocols", NULL, "Make bus device"},
	{"pair", 0, NULL,
	 NN_OPT_SET_ENUM, offsetof (nn_options_t, protocol), &proto_pair,
	 NN_MASK_PROTOCOL, NN_MASK_PROTOCOL, NN_NO_REQUIRES,
     "Protocols", NULL, "Make pair device"},

    {"topology", 'T', NULL,
     NN_OPT_STRING, offsetof (nn_options_t, topology), NULL,
     NN_MASK_TOPOLOGY, NN_MASK_TOPOLOGY, NN_NO_REQUIRES,
     "Socket Options", "NAME", "Use nanoconfig://TOPOLOGY address"},

    /* Sentinel */
    {NULL, 0, NULL,
     0, 0, NULL,
     0, 0, 0,
     NULL, NULL, NULL},
    };

static void nn_assert_errno (int flag, char *description)
{
    int err;

    if (!flag) {
        err = errno;
        fprintf (stderr, "%s: %s\n", description, nn_strerror (err));
        exit (3);
    }
}

static int nn_create_socket1 (nn_options_t *options)
{
    int sock;
	int sock_type;

	switch(options->protocol) {
	case PROTO_SURVEY:
        sock_type = NN_SURVEYOR;
		break;
	case PROTO_REQREP:
        sock_type = NN_REQ;
		break;
	case PROTO_PUBSUB:
        sock_type = NN_PUB;
		break;
	case PROTO_PIPELINE:
        sock_type = NN_PUSH;
		break;
	case PROTO_PAIR:
        sock_type = NN_PAIR;
		break;
	case PROTO_BUS:
        sock_type = NN_BUS;
		break;
	}

    sock = nn_socket (AF_SP, sock_type);
    nn_assert_errno (sock >= 0, "Can't create socket");

    return sock;
}

static int nn_create_socket2 (nn_options_t *options)
{
    int sock;
	int sock_type;

	switch(options->protocol) {
	case PROTO_SURVEY:
        sock_type = NN_RESPONDENT;
		break;
	case PROTO_REQREP:
        sock_type = NN_REP;
		break;
	case PROTO_PUBSUB:
        sock_type = NN_SUB;
		break;
	case PROTO_PIPELINE:
        sock_type = NN_PULL;
		break;
	case PROTO_PAIR:
        sock_type = NN_PAIR;
		break;
	case PROTO_BUS:
        return -1;
	}

    sock = nn_socket (AF_SP, sock_type);
    nn_assert_errno (sock >= 0, "Can't create socket");

    return sock;
}

static void nn_connect_socket (nn_options_t *options, int sock)
{
    int rc;
    char *addr;
    int addr_len;

    addr_len = strlen (options->topology);
    addr_len += strlen ("nanoconfig://");
    addr_len += 1;
    addr = malloc (addr_len);
    alloc_assert (addr);
    sprintf (addr, "nanoconfig://%s", options->topology);

    rc = nc_configure (sock, addr);
    nn_assert_errno (rc >= 0, "Can't connect");

    free (addr);
}


struct nn_commandline nn_cli = {
    "A command-line interface to nanomsg",
    "",
    nn_options,
    NN_MASK_PROTOCOL | NN_MASK_TOPOLOGY,
};

int main (int argc, char **argv)
{
    int sock1, sock2;
    int rc;
    nn_options_t options = {
        /* verbose */ 0,
        /* protocol */ 0,
		/* topology */ NULL,
    };

    nn_parse_options (&nn_cli, &options, argc, argv);
    sock1 = nn_create_socket1 (&options);
    nn_connect_socket (&options, sock1);
    sock2 = nn_create_socket2 (&options);
    if(sock2 >= 0)
        nn_connect_socket (&options, sock2);

    rc = nn_device (sock1, sock2);
    nn_assert_errno (rc >= 0, "Failed to start device");

    nc_close (sock2);
    nc_close (sock1);
    return 0;
}
