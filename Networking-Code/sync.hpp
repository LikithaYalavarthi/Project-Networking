#include <iostream>
#include <vector>
#include <utility>
#include <string>
#include <pthread.h>

using namespace std;

class bulletin_resources;

struct sync_thread{
    int sock;
    int debug;
    int daemon;
    char* log_file;
    bulletin_resources* resource;
    pthread_mutex_t* read;
    pthread_mutex_t* write;
    pthread_mutex_t process;
};

void* sync_receiver(void* y);

class sync_master{
protected:
    vector <pair<string, int>> peers;
    vector <int> sock;
    vector <int> status;
    
public:
    int debug;
    int daemon;
    char* log_file;
    sync_master();
    void setconfig(int opt, int d, char* log);
    void push_peer(char* ip, int port);
    void erase_peer(int i);
    int get_peers_num();
    void init();
    int connectto(int i);
    int commit(int i);
    int sendmessage(int i, int q, char* message);
    int calloff(int i);
    int close_connection(int i);
    int get_status(int i);
};