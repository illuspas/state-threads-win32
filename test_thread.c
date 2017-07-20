#include <stdio.h>

/*
build and execute
    gcc -I../obj -g huge_threads.c ../obj/libst.a  -o huge_threads;
    ./huge_threads 10000
10K report:
    10000 threads, running on 1 CPU 512M machine,
    CPU 6%, MEM 8.2% (~42M = 42991K = 4.3K/thread)
30K report:
    30000 threads, running on 1CPU 512M machine,
    CPU 3%, MEM 24.3% (4.3K/thread)
*/

#include "st.h"

void* do_calc(void* arg){
    int sleep_ms = (int)(long int)(char*)arg * 10;
    
    for(;;){
        printf("in sthread #%dms\n", sleep_ms);
        st_usleep(sleep_ms * 1000);
    }
    
    return NULL;
}

int main(int argc, char** argv){
//    if(argc <= 1){
//        printf("Test the concurrence of state-threads!\n"
//            "Usage: %s <sthread_count>\n"
//            "eg. %s 10000\n", argv[0], argv[0]);
//        return -1;
//    }
    
    if(st_init() < 0){
        printf("error!");
        return -1;
    }
    
    int i;
    int count = 1000;//atoi(argv[1]);
    for(i = 1; i <= count; i++){
        if(st_thread_create(do_calc, (void*)i, 0, 0) == NULL){
            printf("error!");
            return -1;
        }
    }
    
    st_thread_exit(NULL);
    printf("hello\n");
    return 0;
}