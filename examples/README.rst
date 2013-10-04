Configuration Service Example
=============================

This is just a dumb configuration service, to show that format of request and
replies. It is not meant to be something production-ready.


Dependencies
------------

* python2 (will work on python3 when other dependencies work)
* PyYaml
* msgpack-python
* pynanomsg


Running
-------

Service itself::

    python2 ns.py

Client services::

    NN_CONFIG_SERVICE=ipc:///tmp/ns.sock nccat ...


Configuring
-----------

There is just a ``config.yaml`` which contains a mapping between exact
request strings to exact replies. Replies are serialized by msgpack
(the format that nanomsg needs) but otherwise it's the same, including
successful result code and so on.

If you're not familiar with YAML good enough (the example config has ugly
syntax on the first sight because protocol uses nested lists), you can convert
it with to a more-known and more strict JSON:

    python2 ./yaml2json.py < config.yaml > config.json

(the reason why configuration file is not JSON is that JSON doesn't allow
comments, but YAML does)


On the Fly Updates
------------------

Even this simple dumb service has on-the-fly configuration updates. Just
run clients with the socket for updates configured::

    NN_CONFIG_SERVICE=ipc:///tmp/ns.sock NN_CONFIG_UPDATES=ipc:///tmp/nsup.sock nccat ...

And try to change ``config.yaml``. Everything should be reconnected for just
about a second (for simplicity ns.py polls for updates once a second).
