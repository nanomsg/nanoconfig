#
#  This code is released into a public domain
#
from __future__ import print_function
import yaml
import json
import msgpack
from pynanomsg import Domain, AF_SP, REP


with open('config.yaml', 'rt') as f:
    data = yaml.load(f)

dom = Domain(AF_SP)
sock = dom.socket(REP)
sock.bind('ipc:///tmp/ns.sock')
print('Name service is listening at "ipc:///tmp/ns.sock"')
print('Run nanomsg/nanoconfig apps with:')
print('    NN_CONFIG_SERVICE=ipc:///tmp/ns.sock app ...')

while True:
    req = sock.recv(4096)
    print("REQUEST", req)
    if req in data:
        resp = data[req]
    else:
        resp = [0, 404, "Not found"]
    print("RESPONSE", json.dumps(resp))
    sock.send(msgpack.dumps(resp))


