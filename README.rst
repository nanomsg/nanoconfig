Welcome to nanoconfig
=====================

nanoconfig is a small wrapper on top of nanomsg that lets admins configure
nanomsg sockets without programmers intervention.

The goal for nanoconfig is to be enventually included into nanomsg itself.


API
---

Any nanomsg socket can be configured with nanoconfig. To do this, instead of
calling ``nn_bind()`` and ``nn_connect()`` and setting various socket options
do single ``nc_configure()`` call. For example::

    #include <nanomsg/nn.h>
    #include <nanomsg/reqrep.h>
    #include <nanomsg/nanoconfig.h>

    s = nn_socket (AF_SP, AF_SP_RAW)
    assert (s >= 0)
    rc = nc_configure(s, "nanoconfig://topology1")
    assert (rc >= 0)
    ...
    nc_close (s);

Note: instead of raw ``nn_close`` from nanomsg you must call ``nc_close`` from
nanoconfig, so that nanoconfig can free it's own resources.


Environment
-----------

You should set the address of configuration service using environment variable.
For example, if you have ``your_app`` binary application::

    NN_CONFIG_SERVICE=ipc:///var/run/name_service.sock your_app

To have system-wide configuration service you might want to
add ``/etc/profile.d/nanoconfig.sh`` with the following contents::

    NN_CONFIG_SERVICE=ipc:///var/run/name_service.sock
    export NN_CONFIG_SERVICE


Command-Line
------------

There are two command-line programs, that are useful for debugging
and experimentation with nanoconfig:

* ``nccat`` is just same utility the ``nanocat`` is but instead of
  ``--bind`` and ``--connect`` options it has ``--topology`` option
* ``ncdev`` is a thin wrapper around ``nn_device`` that should transparently
  join the topology


Configuration Services
----------------------

There is a very dumb configuration service in ``examples`` directory. You can
run it with::

    cd examples
    python2 ns.py

See ``examples/README.rst`` for list of dependencies.

More configuration service implementation will be listed later.


See Also
--------

nanomsg website: http://nanomsg.org


