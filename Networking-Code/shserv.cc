

#include "shfd.h"
#include "tokenize.h"

extern char **environ;

/*
 * Note that we wait for all the children so we do not need any zombie
 * reaper.
 */

/*
 * run_it(s, c, a, o) executes the command c with arguments a and
 * within the inherited environment.  In addition, run_it also awaits
 * for the completion of c and returns an appropriate answer through
 * communication socket s.  The output of c is put into the file o for
 * subsequent access.
 */
void run_it (int sd, const char* command, char* const argv[], const char* out_file) {
    char ans[MAX_LEN];
    int status;
    char msg[MAX_LEN];  // logger string

    int childp = fork();

    if (childp == 0) { // child does execvp
        // set up output
        close(1);
        close(2);
        // open what would become the output file
        int ofd = open(out_file, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, S_IRUSR | S_IWUSR);
        if (ofd < 0) {
            snprintf(ans, MAX_LEN, "FAIL %d output redirection error: %s\r\n", errno, strerror(errno));
            send(sd, ans, strlen(ans), 0);
            snprintf(msg, MAX_LEN, "%s: output redirection error %d: %s\n", 
                     __FILE__, errno, strerror(errno));
            logger(msg);
            if (debugs[DEBUG_COMM]) {
                snprintf(msg, MAX_LEN, "%s: <-- %s\n", __FILE__, ans);
                logger(msg);
            } /* DEBUG_COMM */
            exit(err_exec);
        }
        while (dup2(ofd,2) < 0 && (errno == EBUSY || errno == EINTR))
            /* NOP */ ;

        // attempt to execute the command (with inherited environment
        // including the search path)
        execvp(command, argv);

        // execvp call failed (errno set)
        snprintf(ans, MAX_LEN, "FAIL %d exec error: %s\r\n", errno, strerror(errno));
        send(sd, ans, strlen(ans), 0);
        snprintf(msg, MAX_LEN, "%s: exec error %d: %s\n", __FILE__, errno, strerror(errno));
        logger(msg);
        if (debugs[DEBUG_COMM]) {
            snprintf(msg, MAX_LEN, "%s: <-- %s\n", __FILE__, ans);
            logger(msg);
        } /* DEBUG_COMM */
        close(1);
        close(2);
        exit(err_exec);
    }

    else { // parent just waits for child completion
        waitpid(childp, &status,0);
        int r = WEXITSTATUS(status);
        snprintf(msg, MAX_LEN, "%s: command completed with status %d\n", __FILE__, r);
        logger(msg);
        if (r != err_exec) {  // err_exec problems reported earlier
            if (r != 0) {
                snprintf(ans, MAX_LEN, "ERR %d command completed with a non-null exit code\r\n", r);
            }
            else {
                snprintf(ans, MAX_LEN, "OK 0 command completed\r\n");
            }
            send(sd, ans, strlen(ans), 0);
            if (debugs[DEBUG_COMM]) {
                snprintf(msg, MAX_LEN, "%s: <-- %s\n", __FILE__, ans);
                logger(msg);
            } /* DEBUG_COMM */
        }
    }
}



void* shell_rec(client_t* clnt){
    int error =0,ls =0;
    socklen_t len = sizeof(error);
    printf("started function:");
    // char mag[MAX_LEN];
    // snprintf(msg, MAX_LEN," ");
    while(1){
        // ls = send(clnt -> sd,msg,2,MSG_NOSIGNAL);
        ls = getsockopt(clnt -> sd, SOL_SOCKET, SO_KEEPALIVE ,&error, &len);
        printf("getsockopt : %d with error :%s\n",ls,strerror(error));
        if(ls == -1){
            clnt -> is_server_busy = false;
            break;
        }
        sleep(3);

    }
    return 0;
}


/*
 * Client handler for the shell server
 */
void* shell_client (client_t* clnt) {
    int sd = clnt -> sd;
    // long int sp = clnt -> sd;
    char* ip = clnt -> ip;
    clnt->is_server_busy = true;
    // pthread_t st;

   
    char command[MAX_LEN];   // buffer for commands
    command[MAX_LEN - 1] = '\0';
    char* com_tok[MAX_LEN];  // buffer for the tokenized commands
    size_t num_tok;          // number of tokens
    bool no_command = true;  // true before the first shell command is issued
    char ans[MAX_LEN];
    int n;
    char msg[MAX_LEN];  // logger string

    snprintf(msg, MAX_LEN, "%s: new client from %s assigned socket descriptor %d\n",
             __FILE__, ip, sd);
    logger(msg);
    snprintf(msg, MAX_LEN, 
             "Welcome to shfd v.1 [%s]. CPRINT and shell comands spoken here.\r\n",
             ip);
    send(sd, msg, strlen(msg),0);

    // Prepare an output file:
    char out_file[MAX_LEN];
    snprintf(out_file, MAX_LEN, "/tmp/shfd-tmp-%d", sd);

    // pthread_create(&st, NULL, (void* (*) (void*))shell_rec, (void*)clnt) ;

    while ((n = readline(sd, command, MAX_LEN-1)) != recv_nodata) {
        

        if ( n >= 1 && command[n-1] == '\r' ) 
            command[n-1] = '\0';
        // Tokenize input:
        num_tok = str_tokenize(command, com_tok, strlen(command));
        com_tok[num_tok] = 0;      // null termination for execv*
        if (debugs[DEBUG_COMM]) {
            snprintf(msg, MAX_LEN, "%s: --> %s\n", __FILE__, command);
            logger(msg);
        } /* DEBUG_COMM */

        if (strlen(command) == 0) {
            snprintf(ans, MAX_LEN, "FAIL %d provide a command to run\r\n", EBADMSG);
            send(sd, ans, strlen(ans), 0);
            if (debugs[DEBUG_COMM]) {
                snprintf(msg, MAX_LEN, "%s: <-- %s\n", __FILE__, ans);
                logger(msg);
            } /* DEBUG_COMM */
        }

        else if (strncmp(command, "CPRINT", strlen("CPRINT")) == 0) {  // print last output
            if (no_command) {
                snprintf(ans, MAX_LEN, "FAIL %d no command executed in this session\r\n", EIO);
                send(sd, ans, strlen(ans), 0);
                if (debugs[DEBUG_COMM]) {
                    snprintf(msg, MAX_LEN, "%s: <-- %s\n", __FILE__, ans);
                    logger(msg);
                } /* DEBUG_COMM */
            }
            else {
                int r;
                int ofd = open(out_file, O_RDONLY);
                if (ofd < 0) {
                    snprintf(ans, MAX_LEN, "ERR %d retrieving last output: %s\r\n", 
                             errno, strerror(errno));
                    send(sd, ans, strlen(ans), 0);
                    if (debugs[DEBUG_COMM]) {
                        snprintf(msg, MAX_LEN, "%s: <-- %s\n", __FILE__, ans);
                        logger(msg);
                    } /* DEBUG_COMM */
                    snprintf(msg, MAX_LEN, "%s: %s while retrieving last output\n", 
                             __FILE__, strerror(errno));
                    logger(msg);
                }
                memset(ans, 0, MAX_LEN);
                while ((r = read(ofd, ans, MAX_LEN)) != 0) {
                    if (r < 0) {
                        snprintf(ans, MAX_LEN, "FAIL %d %s\r\n", errno, strerror(errno));
                        send(sd, ans, strlen(ans), 0);
                        if (debugs[DEBUG_COMM]) {
                            snprintf(msg, MAX_LEN, "%s: <-- %s\n", __FILE__, ans);
                            logger(msg);
                        } /* DEBUG_COMM */
                        break;
                    }
                    send(sd, ans, strlen(ans), 0);
                    memset(ans, 0, MAX_LEN);
                }
                snprintf(ans, MAX_LEN, "OK 0 end of output\r\n");
                send(sd, ans, strlen(ans), 0);
                if (debugs[DEBUG_COMM]) {
                    snprintf(msg, MAX_LEN, "%s: <-- %s\n", __FILE__, ans);
                    logger(msg);
                } /* DEBUG_COMM */
                close(ofd);
            }
        }
        else { // normal command
            unlink(out_file);
            no_command = false;
            run_it(sd, com_tok[0], com_tok, out_file);
        }

    }

    // end of communication
    printf("Done with connection\n");
    snprintf(msg, MAX_LEN, "%s: client on socket descriptor %d went away, closing\n", __FILE__, sd);
    logger(msg);
    shutdown(sd, SHUT_RDWR);
    close(sd);
    clnt->is_server_busy = false;
    clnt->sd = -1;
    unlink(out_file);
    // delete clnt;
    return 0;
}
