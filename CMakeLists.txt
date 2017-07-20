
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS " -D_WIN32_WINNT=0x0600 -DFD_SETSIZE=4096 -DUSE_POLL -DMALLOC_STACK")
set(ST_FILES  event.c io.c key.c  sched.c sync.c stk.c)

add_library(st STATIC ${ST_FILES})

add_executable(test_thread ${ST_FILES} test_thread.c)
target_link_libraries(test_thread -lws2_32 -lwsock32 -lntdll -lpowrprof)

add_executable(test_socket  ${ST_FILES}  test_socket.c)
target_link_libraries(test_socket -lws2_32 -lwsock32 -lntdll -lpowrprof)