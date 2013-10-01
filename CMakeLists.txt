cmake_minimum_required (VERSION 2.6)

project (nanoconfig)

add_library (nanoconfig
    src/nanoconfig.c
    src/worker.c
    src/utils/random.c
    src/utils/alloc.c
    src/utils/thread.c
    src/utils/err.c
    src/utils/clock.c
    src/utils/msgpack.c
    )
target_link_libraries (nanoconfig nanomsg)
