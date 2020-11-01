
#include "shfd.h"
#include "bulletin_threads.h"

#include <stdio.h>      
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h> 
#include <string.h> 
#include <arpa/inet.h>
#include <vector>
#include <string>

/*
 * Log file
 */
char logfile[20] = "bbserv.log";
char pidfile[20] = "bbserv.pid";
char config[100] = "bbserv.conf";
char bbfile[100] = "bbfile";
long int shsock, fsock, rssock, ssock, bsock;              // master sockets

bool detach;
sync_thread* sync_thread_resource;
pthread_t* receiver;
pthread_mutex_t read_lock;
pthread_mutex_t write_lock;
int read_num;
int pid = 0;
/*
 * true iff the file server is alive (and kicking).
 */
bool alive;

pthread_mutex_t logger_mutex;

bulletin_resources bulletin_res;
sync_master syns;
bulletin_threads* clients;
pthread_t* bulletin_clients;
client_t* shell_clnt;
threadpool_t *t_pool;
int t_incr = 0;
int t_max = 0;
struct replica_server* r_servers;
int r_servers_count = 0;
char progname[100];
extern char **environ;

int debug_opt;

/*
 * What to debug (nothing by default):
 */
bool debugs[3] = {false, false, false};

void logger(const char * msg) {
    pthread_mutex_lock(&logger_mutex);
    time_t tt = time(0);
    char* ts = ctime(&tt);
    ts[strlen(ts) - 1] = '\0';
    printf("%s: %s", ts, msg);
    fflush(stdout);
    pthread_mutex_unlock(&logger_mutex);
}


void sigquit_handler(int s){
    char command_end[1024];
    sprintf(command_end, "kill -9 %d", pid);
    if (pid > 0)
        popen(command_end, "r");
    sprintf(command_end, "kill -9 %d", getpid());
    popen(command_end, "r");
}

void sigup_handler(int s){
    // read config from config file
    FILE* pfile;
    char buffer[100];
    char option[100];
    
    for (int i = 0; i < syns.get_peers_num(); i++)
        syns.close_connection(i);
    
    while(syns.get_peers_num())
        syns.erase_peer(0);
    
    pfile = fopen(config, "rw+");
    while(fscanf(pfile, "%[^=]%*[=]", option) != EOF){
        int result;
        // printf("Option: %s, var: %s\n", option, var);
        if ( (strcmp(option, "THMAX") == 0) ){
            fscanf(pfile, "%d", &result);
            // (debug)
            // cout << t_max << endl;
        }else if ( (strcmp(option, "BBPORT") == 0) ){
            fscanf(pfile, "%d", &result);
            // (debug)
            // cout << bport << endl;
        }else if ( (strcmp(option, "BBFILE") == 0) ){
            result = fscanf(pfile, "%[^\n]", bbfile);
            // (debug)
            // cout << config << endl;
        }else if ( (strcmp(option, "SYNCPORT") == 0) ){
            fscanf(pfile, "%d", &result);
            // (debug)
            // cout << sport << endl;
        }else if ( (strcmp(option, "DAEMON") == 0) ){
            char var[10];
            result = fscanf(pfile, "%[^\n]", var);
            if ( (!strcmp(var, "true")) || (!strcmp(var, "1")) ){
                // detach = true;
            }
            if ( (!strcmp(var, "false")) || (!strcmp(var, "0")) ){
                // detach = false;
            }
            // (debug)
            // cout << "detach: " << detach << endl;
        }else if ( (strcmp(option, "DEBUG") == 0) ){
            char var[10];
            result = fscanf(pfile, "%[^\n]", var);
            
            if ( (!strcmp(var, "true")) || (!strcmp(var, "1")) ){
                debug_opt = 1;
            }
            if ( (!strcmp(var, "false")) || (!strcmp(var, "0")) ){
                debug_opt = 0;
            }
            // (debug)
            // cout << "debug: " << debug_opt << endl;
        }else if ( (strcmp(option, "PEERS") == 0)){
            char var[20];
            int port;
            while( result = fscanf(pfile, "%[^:\n]%*[:]%d%*[ ]", var, &port) ){
                // (debug) check ip and port
                // cout << var << ", " << port << endl;
                if (!strcmp(var, "localhost"))
                    sprintf(var, "127.0.0.1");
                syns.push_peer(var, port);
            }
        }
        fscanf(pfile, "%*[\n]");

    }
    
    // finish reading config
    fclose(pfile);
    // ***********************************
    syns.init();
    for (int i = 0; (i < syns.get_peers_num()) ;i++){
        syns.connectto(i);
    }

}


/*
 * Simple conversion of IP addresses from unsigned int to dotted
 * notation.
 */
void ip_to_dotted(unsigned int ip, char* buffer) {
    char* ipc = (char*)(&ip);
    sprintf(buffer, "%d.%d.%d.%d", ipc[0], ipc[1], ipc[2], ipc[3]);
}

int next_arg(const char* line, char delim) {
    int arg_index = 0;
    char msg[MAX_LEN];  // logger string

    // look for delimiter (or for the end of line, whichever happens first):
    while ( line[arg_index] != '\0' && line[arg_index] != delim)
        arg_index++;
    // if at the end of line, return -1 (no argument):
    if (line[arg_index] == '\0') {
        if (debugs[DEBUG_COMM]) {
            snprintf(msg, MAX_LEN, "%s: next_arg(%s, %c): no argument\n", __FILE__, line ,delim);
            logger(msg);
        } /* DEBUG_COMM */
        return -1;
    }
    // we have the index of the delimiter, we need the index of the next
    // character:
    arg_index++;
    // empty argument = no argument...
    if (line[arg_index] == '\0') {
        if (debugs[DEBUG_COMM]) {
            snprintf(msg, MAX_LEN, "%s: next_arg(%s, %c): no argument\n", __FILE__, line ,delim);
            logger(msg);
        } /* DEBUG_COMM */    
        return -1;
    }
    if (debugs[DEBUG_COMM]) {
        snprintf(msg, MAX_LEN, "%s: next_arg(%s, %c): split at %d\n", __FILE__, line ,delim, arg_index);
        logger(msg);
    } /* DEBUG_COMM */
    return arg_index;
}


void* bulletin_server (int msock) {
    int ssock;                      // slave sockets
    struct sockaddr_in client_addr; // the address of the client...
    socklen_t client_addr_len = sizeof(client_addr); // ... and its length
    // Setting up the thread creation:

    // int error = 0;
    // socklen_t len = sizeof(error);

    pthread_t tt;
    pthread_attr_t ta;
    pthread_attr_init(&ta);
    pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);

    clients = (bulletin_threads*) malloc(sizeof(bulletin_threads) * t_max);
    bulletin_clients = (pthread_t*) malloc (sizeof(pthread_t) * t_max);
    
    memset(clients, 0, t_max * sizeof(bulletin_threads));
    memset(bulletin_clients, 0, sizeof(pthread_t) * t_max );
    for (int i = 0; i < t_max; i++){
        clients[i].resources = &bulletin_res;
        clients[i].syns = &syns;
        clients[i].read = &read_lock;
        clients[i].write = &write_lock;
        clients[i].read_num = &read_num;
        if (debug_opt){
            clients[i].read_delay = 3;
            clients[i].write_delay = 6;
        }else{
            clients[i].read_delay = 0;
            clients[i].write_delay = 0;
        }
        
    }
    bulletin_res.set_filename(bbfile);

    char msg[MAX_LEN];

    for (int i = 0; i < t_max; i++)
        pthread_create(&bulletin_clients[i], NULL, bulletin_client, &clients[i]);

    while (1) {
        // Accept connection:
        ssock = accept(msock, (struct sockaddr*)&client_addr, &client_addr_len);
        if (ssock < 0) {
            if (errno == EINTR)
                continue;
            snprintf(msg, MAX_LEN, "%s: bulletin server accept: %s\n", __FILE__, strerror(errno));
            logger(msg);
            return 0;
        }

        bool check = false;
        for (int i = 0;(i < t_max) && !check;i++){
            if (pthread_mutex_trylock(&clients[i].process_lock) == 0){
                clients[i].sock = ssock;
                check = 1;
                pthread_cond_signal(&clients[i].sig_lock);
                pthread_mutex_unlock(&clients[i].process_lock);
            }
        }
        
        if (!check){
            // server is busy
            snprintf(msg, MAX_LEN,"FAIL 2 Bulletin server is busy, try again later\n");
            send(ssock, msg, strlen(msg),0);
            close(ssock);
        }

    }
    for (int i = 0; i < syns.get_peers_num(); i++)
        syns.close_connection(i);

    return NULL;
}

void* replica_server_thread(int msock){
    // add sync logic here

    int ssock;
    struct sockaddr_in client_addr; // the address of the client...
    socklen_t client_addr_len = sizeof(client_addr); // ... and its length
    syns.init();
    for (int i = 0; i < t_max; i++){
        sync_thread_resource = (sync_thread*) malloc (20 * sizeof(sync_thread));
        receiver = (pthread_t*) malloc (20 * sizeof(pthread_t));
    }
    
    while (1) {
        
        // Accept connection:
        ssock = accept(msock, (struct sockaddr*)&client_addr, &client_addr_len);
        if (ssock < 0) {
            if (errno == EINTR)
                continue;
            printf("%s: bulletin server accept: %s\n", __FILE__, strerror(errno));
            return 0;
        }

        int i = 0;
        int check = 0;
        for (;(i < t_max) && !check;i++){
            if (pthread_mutex_trylock(&sync_thread_resource[i].process) == 0){
                sync_thread_resource[i].sock = ssock;
                sync_thread_resource[i].resource = &bulletin_res;
                sync_thread_resource[i].read = &read_lock;
                sync_thread_resource[i].write = &write_lock;
                sync_thread_resource[i].debug = debug_opt;
                sync_thread_resource[i].daemon = detach;
                sync_thread_resource[i].log_file = logfile;
                check = 1;
                // create thread receiver 
                pthread_create(&receiver[i], NULL, sync_receiver, &sync_thread_resource[i]);
            }
            
        }
        
    }

    return NULL;
}

/*
 * Initializes the access control structures, fires up a thread that
 * handles the file server, and then does the standard job of the main
 * function in a multithreaded shell server.
 */
int main (int argc, char** argv, char** envp) {
    // int shport = 9001;              // ports to listen to
    // int fport = 9002;
    int bport = 9000;
    int sport = 10000;
    const int qlen = 32;            // queue length for incoming connections
    sprintf(progname, "%s", argv[0]);
    debug_opt = 0;
    // int rsport = 9003;  // informational use only.

    t_max = 20;

    char msg[MAX_LEN];  // logger string

    // char *replica_servers[4];
    pthread_mutex_init(&logger_mutex, 0);

    // parse command line
    extern char *optarg;
    int opt;
    detach = true;  // Detach by default
    bool b, c, T, p, s, f, d, peers_check;
    b = c = T = p = s = f = d = peers_check = false;

    while ( (opt = getopt(argc, argv, "b:c:T:p:s:fd") ) != -1) {
        switch ((char)opt) {

            case 'b':
                b = 1;
                sprintf(bbfile, "%s", optarg);
                break;

            case 'T':
                T = 1;
                t_max = atoi(optarg);
                break;

            case 'p':
                p = 1;
                bport = atoi(optarg);
                break;

            case 's':
                s = 1;
                sport = atoi(optarg);
                break;

            case 'f':
                f = 1;
                detach = false;
                break;

            case 'd':
                d = 1;
                debug_opt = 1;
                printf("Debug mode: will delay IO\n");
                break;

            case 'c':
                c = 1;
                sprintf(config, "%s", optarg);
                break;

        }
    }

    for (int i = argc - 1; i >= 1; i--){
        char var[20];
        int port = 0;
        sscanf(argv[i], "%[^:]%*[:]%d", var, &port);
        if (port){
            if (debug_opt)  // (debug) check ip and port
                cout << "PEERS: " << var << ", " << port << endl;
            if (!strcmp(var, "localhost"))
                sprintf(var, "127.0.0.1");
            syns.push_peer(var, port);
            peers_check = 1;
        }
        
    }

    // read config from config file
    FILE* pfile;
    char buffer[100];
    char option[100];
    pfile = fopen(config, "rw+");
    while(fscanf(pfile, "%[^=]%*[=]", option) != EOF){
        int result;
        // printf("Option: %s, var: %s\n", option, var);
        if ( strcmp(option, "THMAX") == 0 ){
            fscanf(pfile, "%d", &result);
            if (!T){
                t_max = result;
            }
            if (debug_opt)
                cout << "Tmax: " << t_max << endl;
        }else if ( strcmp(option, "BBPORT") == 0 ){
            fscanf(pfile, "%d", &result);
            if (!p){
                bport = result;
            }
            if (debug_opt)
                cout << "bport: " << bport << endl;
        }else if ( strcmp(option, "BBFILE") == 0 ){
            char str[100];
            if (b){
                fscanf(pfile, "%[^\n]", str);
            }else{
                fscanf(pfile, "%[^\n]", bbfile);
            }
            if (debug_opt)
                cout << "bbfile: " << bbfile << endl;
        }else if ( strcmp(option, "SYNCPORT") == 0 ){
            fscanf(pfile, "%d", &result);
            if (!s){
                sport = result;
            }
            if (debug_opt)
                cout << "sport: " << sport << endl;
        }else if ( strcmp(option, "DAEMON") == 0 ){
            char var[10];
            result = fscanf(pfile, "%[^\n]", var);
            if (!f){
                if ( (!strcmp(var, "true")) || (!strcmp(var, "1")) ){
                    detach = true;
                }
                if ( (!strcmp(var, "false")) || (!strcmp(var, "0")) ){
                    detach = false;
                }
            }
            if (debug_opt)
                cout << "detach: " << detach << endl;
        }else if ( strcmp(option, "DEBUG") == 0 ){
            char var[10];
            result = fscanf(pfile, "%[^\n]", var);
            if (!d){
                if ( (!strcmp(var, "true")) || (!strcmp(var, "1")) ){
                    debug_opt = 1;
                }
                if ( (!strcmp(var, "false")) || (!strcmp(var, "0")) ){
                    debug_opt = 0;
                }
            }
            if (debug_opt)
                cout << "debug: " << debug_opt << endl;
        }else if ( strcmp(option, "PEERS") == 0 ){
            char var[20];
            int port;
            while( result = fscanf(pfile, "%[^:\n]%*[:]%d%*[ ]", var, &port) ){
                if (debug_opt && !peers_check)  // (debug) check ip and port
                    cout << "PEERS: " << var << ", " << port << endl;
                if (!strcmp(var, "localhost"))
                    sprintf(var, "127.0.0.1");
                if (!peers_check)
                    syns.push_peer(var, port);
            }
        }
        fscanf(pfile, "%*[\n]");

    }
    
    // finish reading config
    fclose(pfile);
    // ***********************************

    t_incr = t_max;
    r_servers_count = 0;
    r_servers = (replica_server*)malloc(sizeof(replica_server)*5);

    syns.setconfig(debug_opt, detach, logfile);

    printf("process ID of current process : %d\n", getpid());
    
    bsock = passivesocket(bport,qlen);
    if (bsock < 0) {
        perror("bulletin server passivesocket");
        return 1;
    }
    printf("bulletin server up and listening on port %d\n", bport);
    
    ssock = passivesocket(sport,qlen);
    if (ssock < 0) {
        perror("Synchronization server passivesocket");
        return 1;
    }
    printf("Synchronization server up and listening on port %d\n", sport);
    
    // update_replica_servers(r_servers);

    signal(SIGHUP,  sigup_handler);
    signal(SIGQUIT,  sigquit_handler);
    // ... and we detach!
    if (detach) {
        // umask:
        umask(0177);

        // ignore SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGALRM, SIGSTOP:
        // (we do not need to do anything about SIGTSTP, SIGTTIN, SIGTTOU)
        signal(SIGINT,  SIG_IGN);
        signal(SIGTERM, SIG_IGN);
        signal(SIGALRM, SIG_IGN);
        signal(SIGSTOP, SIG_IGN);

        // private group:
        setpgid(getpid(),0);

        // become daemon:
        int pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }
        if (pid > 0){
            printf("process ID of child process : %d\n", pid);
            return 0;  // parent dies peacefully
        }

        // close everything (except the master socket) and then reopen what we need:
        for (int i = getdtablesize() - 1; i >= 0 ; i--)
            if (i != ssock && i != bsock)
                close(i);
        // stdin:
        int fd = open("/dev/null", O_RDONLY);
        // stdout:
        fd = open(logfile, O_WRONLY|O_CREAT|O_APPEND,S_IRUSR|S_IWUSR);
        // stderr:
        dup(fd);

        // we detach:
        fd = open("/dev/tty",O_RDWR);
        ioctl(fd,TIOCNOTTY,0);
        close(fd);

        // and now we are a real server.
    }

    // Setting up the thread creation:
    pthread_t tt;
    pthread_t bs;
    pthread_t rst;
    pthread_attr_t ta;
    pthread_attr_init(&ta);
    pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);

    // Launch the thread that becomes a bulletin server:
    if ( pthread_create(&bs, NULL, (void* (*) (void*))bulletin_server, (void*)bsock) != 0 ) {
        snprintf(msg, MAX_LEN, "%s: pthread_create: %s\n", __FILE__, strerror(errno));
        logger(msg);
        return 1;
    }

    alive = true;

    // Continue and become the shell server:
    // shell_server(shsock);

    // If we get this far the shell server has died 
    // snprintf(msg, MAX_LEN, "%s: the shell server died.\n", __FILE__);
    // logger(msg);

    // Continue and become Synchronization server
    replica_server_thread(ssock);

    // keep this thread alive for the file server
    while (alive) {
        sleep(30);
    }

    snprintf(msg, MAX_LEN, "%s: all the servers died, exiting.\n", __FILE__);
    logger(msg);
    
    return 1;
}
