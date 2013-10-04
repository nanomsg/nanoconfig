#
#  This code is released into a public domain
#
from __future__ import print_function
import os.path
import yaml
import json
import msgpack
from pynanomsg import Domain, AF_SP, REP, PUB, SOL_SOCKET, RCVTIMEO


CONFIG = 'config.yaml'

mtime = os.path.getmtime(CONFIG)
with open(CONFIG, 'rt') as f:
    data = yaml.load(f)

dom = Domain(AF_SP)
rsock = dom.socket(REP)
rsock.bind('ipc:///tmp/ns.sock')
rsock.setsockopt(SOL_SOCKET, RCVTIMEO, 1000)

usock = dom.socket(PUB)
usock.bind('ipc:///tmp/nsup.sock')
usock.setsockopt(SOL_SOCKET, RCVTIMEO, 1000)
print('Name service is listening at "ipc:///tmp/ns.sock"')
print('Run nanomsg/nanoconfig apps with:')
print('    NN_CONFIG_SERVICE=ipc:///tmp/ns.sock '
          'NN_CONFIG_UPDATES=ipc:///tmp/nsup.sock app ...')

while True:
    try:
        req = rsock.recv(4096)
    except AssertionError:  # crappy pynanomsg
        # We're timed out, probably need to recheck config for updates
        nmtime = os.path.getmtime(CONFIG)
        if nmtime != mtime:

            with open(CONFIG, 'rt') as f:
                ndata = yaml.load(f)

            topics = set()
            for req, resp in ndata.items():
                if(req in data  # there was address
                    and data[req] != resp  # it was different
                    and len(data[req]) >= 3):  # and there is a subscription
                    #  Add subscriptions to set of updates
                    topics.update(data[req][3])
            # Send updates to everybody
            # Some topics might target more sockets than needed
            # but we don't care little inefficiency here
            for i in topics:
                print("UPDATE TO", i)
                usock.send(i.encode('ascii'))

            data = ndata
            mtime = nmtime

    else:
        print("REQUEST", req)
        if req in data:
            resp = data[req]
        else:
            resp = [0, 404, "Not found"]
        print("RESPONSE", json.dumps(resp))
        rsock.send(msgpack.dumps(resp))


