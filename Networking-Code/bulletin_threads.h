#include <pthread.h> 
#include <stdio.h> 
#include <netdb.h> 
#include <netinet/in.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <sys/types.h>
#include <unistd.h>

#include "bulletin.hpp"
#include "sync.hpp"

class sync_master;

typedef struct bulletin_threads{
    int status;
    int sock;
    int read_delay;
    int write_delay;
    int* read_num;
    pthread_mutex_t process_lock;
    pthread_cond_t sig_lock;
    bulletin_resources* resources;
    pthread_mutex_t* read;
    pthread_mutex_t* write;
    sync_master* syns;
};

void* bulletin_client(void* y);
