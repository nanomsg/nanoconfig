cmake_minimum_required (VERSION 2.6)

project (nanoconfig C)
find_package (Threads)

add_subdirectory (doc)

add_library (nanoconfig SHARED
    src/nanoconfig.c
    src/worker.c
    src/utils/random.c
    src/utils/alloc.c
    src/utils/thread.c
    src/utils/err.c
    src/utils/clock.c
    src/utils/msgpack.c
    )
target_link_libraries (nanoconfig nanomsg ${CMAKE_THREAD_LIBS_INIT})

add_executable (nccat
    tools/nanocat.c
    tools/options.c)
target_link_libraries (nccat nanoconfig)

add_executable (ncdev
    tools/nanodev.c
    tools/options.c)
target_link_libraries (ncdev nanoconfig)

install (TARGETS nanoconfig nccat ncdev
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)
install (FILES src/config.h
    DESTINATION include/nn)
