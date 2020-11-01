#include <netdb.h> 
#include <netinet/in.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <sys/types.h>
#include <unistd.h>
#include <cstdio>
#include <fstream>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "sync.hpp"
#include "bulletin.hpp"

using namespace std;

enum replica_status{
    idle = 0,
    ready = 1,
};

void* sync_receiver(void* y){
    sync_thread* res = (sync_thread*)y;
    char buff[1024];
    char command[1024];
    char msg[1024];
    replica_status status = idle;
    fstream fs;
    FILE* shell;
    
    while(read(res->sock, buff, 1024) > 0){
        memset(msg, 0, sizeof(char) * 1024);
        sscanf(buff, "%s", command);
        
        switch (status){

            case idle:
                if (!strcmp("commit", command)){
                    if (res->debug){
                        printf("sync: recv commit\n");
                    }
                    sprintf(msg, "2.0 abort\n");
                    pthread_mutex_lock(res->write);
                    // lock successfully
                    sprintf(msg, "1.0 OK\n");
                    status = ready;
                    
                }
                break;

            case ready:
                status = idle;
                if (!strcmp("calloff", command)){
                    pthread_mutex_unlock(res->write);
                    if (res->debug){
                        printf("sync: undoes\n");
                    }
                    sprintf(msg, "1.0 OK\n");
                    break;
                }
                fs.open(res->resource->get_filename(), fstream::out | fstream::app);
                fs << buff;
                fs.close();
                
                if (res->debug){
                    printf("sync: recv message\n");
                }
                sprintf(msg, "1.0 OK\n");
                pthread_mutex_unlock(res->write);
                break;
            
            default:
                status = idle;
                break;
        }
        memset(buff, 0, sizeof(char) * 1024);
        write(res->sock, msg, strlen(msg));
    }
    pthread_mutex_unlock(&res->process);
    return NULL;
}

// class sync_master
sync_master::sync_master(){

}

void sync_master::push_peer(char* ip, int port){
    peers.push_back(make_pair(ip, port));
    return;
}

int sync_master::get_peers_num(){
    return peers.size();
}

void sync_master::init(){
    while (sock.size() < peers.size()){
        status.push_back(0);
        sock.push_back(0);
    }
    
}

int sync_master::connectto(int i){
    struct sockaddr_in servaddr;
    sock[i] = socket(AF_INET, SOCK_STREAM, 0);
    servaddr.sin_family = AF_INET; 
    servaddr.sin_addr.s_addr = inet_addr(peers[i].first.c_str());
    servaddr.sin_port = htons(peers[i].second);
    int val;
    if ( ( val = connect(sock[i], (struct sockaddr*)&servaddr, sizeof(servaddr)) ) == 0)
        status[i] = 1;
    return val;
}
int sync_master::commit(int i){
    char msg[1024];
    char buff[1024];
    int ans;
    sprintf(msg, "commit\n");
    write(sock[i], msg, strlen(msg));
    read(sock[i], buff, 1024);
    sscanf(buff, "%d", &ans);
    if (ans == 1)
        return 0;
    return 1;
}
int sync_master::sendmessage(int i, int q, char* message){
    char msg[1024];
    char buff[1024];
    int ans;
    sprintf(msg, "%d/%s\n", q, message);
    write(sock[i], msg, strlen(msg));
    read(sock[i], buff, 1024);
    sscanf(buff, "%d", &ans);
    if (ans == 1)
        return 0;
    return 1;
}
int sync_master::calloff(int i){
    char msg[1024];
    char buff[1024];
    int ans;
    sprintf(msg, "calloff\n");
    write(sock[i], msg, strlen(msg));
    read(sock[i], buff, 1024);
    sscanf(buff, "%d", &ans);
    if (ans == 1)
        return 0;
    return 1;
}
int sync_master::close_connection(int i){
    if ( sock[i] == 0 )
        return 1;
    close(sock[i]);
    status[i] = 0;
    return 0;
}
int sync_master::get_status(int i){
    return status[i];
}

void sync_master::erase_peer(int i){
    peers.erase(peers.begin() + i);
    status.erase(status.begin() + i);
    sock.erase(sock.begin() + i);
    return;
}

void sync_master::setconfig(int opt, int d, char* log){
    debug = opt;
    daemon = d;
    log_file = log;
}