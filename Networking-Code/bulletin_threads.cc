#include "bulletin_threads.h"

void* bulletin_client(void* y){
    bulletin_threads* x = (bulletin_threads*)y;

    while(1){
        pthread_mutex_lock(&x->process_lock);
        pthread_cond_wait(&x->sig_lock, &x->process_lock);

        char msg[1024];
        char buff[1024];
        char command[100];
        char username[100] = "nobody";

        // struct timeval timeout;      
        // timeout.tv_sec = 10;
        // timeout.tv_usec = 0;

        // if (setsockopt (x->sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
        //     perror("setsockopt failed");

        // function
        snprintf(msg, 1024, "0.0 Welcome to Bulletin Server\n");
        send(x->sock, msg, strlen(msg), 0);
        
        int quit = 0;
        while(read(x->sock, buff, 1024) > 0){
            memset(msg, 0, 1024);
            // printf("received %s", buff);

            sscanf(buff, "%s", command);
            
            if (!strcmp("USER", command)){
                sscanf(buff, "%s %s", command, username);
                sprintf(msg, "1.0 Hello %s welcome back\n", username);
                printf("%s has connected\n", username);

            }else if (!strcmp("READ", command)){
                int q = -1;
                sscanf(buff, "%s %d", command, &q);
                char temp_message[1024] = "";
                int result = 2;
                // lock session
                if (pthread_mutex_trylock(x->read) == 0){
                    // lock successfully
                    pthread_mutex_lock(x->write);
                    (*x->read_num)++;
                    sleep(x->read_delay);
                    x->resources->load_board();
                    result = x->resources->load_message(q, temp_message);
                    // ***unlock***
                    (*x->read_num)--;
                    if (*x->read_num == 0){
                        pthread_mutex_unlock(x->read);
                    }
                    pthread_mutex_unlock(x->write);

                }else{
                    // locked
                    (*x->read_num)++;
                    sleep(x->read_delay);
                    x->resources->load_board();
                    result = x->resources->load_message(q, temp_message);
                    // ***unlock***
                    (*x->read_num)--;
                    if (*x->read_num == 0){
                        pthread_mutex_unlock(x->read);
                    }
                }
                
                if (result == 2){
                    sprintf(msg, "2.2 ERROR READ  Sorry, what did u say? Or maybe Systen is IO processing\n");
                }else if(result){
                    // fail to load message
                    sprintf(msg, "2.1 UNKNOWN %d message not found\n", q);
                }else{
                    // success
                    sprintf(msg, "2.0 %d %s\n", q, temp_message);
                }

            }else if (!strcmp("WRITE", command)){
                char temp_message[1024] = "";
                // error message
                sprintf(msg, "3.2 ERROR WROTE  Sorry, what did u say? Or maybe Systen is IO processing\n");
                int len = 0;
                int check = 0;
                // get messge string
                for (int i = 0; buff[i] != '\r' && buff[i] != '\n';i++ ){
                    if (check){
                        temp_message[len++] = buff[i];
                        temp_message[len] = '\0';
                    }

                    if (buff[i] == ' ')
                        check = 1;
                }
                // lock session
                pthread_mutex_lock(x->write);

                for (int i = 0; (i < x->syns->get_peers_num()) ;i++)
                    x->syns->connectto(i);
                
                // sync
                int sync_check = 1;
                for (int i = 0; (i < x->syns->get_peers_num()) && sync_check; i++){
                    if ( !x->syns->get_status(i) ){
                        for (int j = 0; j < i; j++){
                            if (x->syns->get_status(j))
                                x->syns->calloff(j);
                        }
                        sync_check = 0;
                    }else{
                        if (x->syns->commit(i)){
                            for (int j = 0; j < i; j++){
                                if (x->syns->get_status(j))
                                    x->syns->calloff(j);
                            }
                            sync_check = 0;
                        }
                    }
                }
                sleep(x->write_delay);

                if (sync_check){
                    x->resources->load_board();
                    int result = x->resources->write_message(x->resources->get_messages_num()+1, username, temp_message, len);
                    if (result){
                        // fail
                        for (int i = 0;i < x->syns->get_peers_num(); i++)
                            if (x->syns->get_status(i)){
                                x->syns->calloff(i);
                            }
                    }else{
                        // success
                        sprintf(msg, "3.0 WROTE %d\n", x->resources->get_messages_num());
                        char msg_buff[1024];
                        x->resources->load_board();
                        x->resources->load_message(x->resources->get_messages_num(), msg_buff);
                        if (x->syns->debug)
                            printf("sync: commit\n");
                        for (int i = 0;i < x->syns->get_peers_num(); i++)
                            if (x->syns->get_status(i)){
                                x->syns->sendmessage(i, x->resources->get_messages_num(), msg_buff);
                            }
                    }
                }

                for (int i = 0; i < x->syns->get_peers_num(); i++)
                    x->syns->close_connection(i);

                // ***unlock***
                pthread_mutex_unlock(x->write);
            

            }else if (!strcmp("REPLACE", command)){
                int q = -1;
                char temp_message[1024] = "\0";
                sscanf(buff, "%s %d", command, &q);
                int len = 0;
                int check = 0;
                // get messge string
                for (int i = 0;buff[i] != '\r' && buff[i] != '\n';i++ ){
                    if (check){
                        temp_message[len++] = buff[i];
                        temp_message[len] = '\0';
                    }

                    if (buff[i] == '/')
                        check = 1;
                }

                // error message
                sprintf(msg, "3.2 ERROR WRTE  Sorry, what did u say? Or maybe Systen is IO processing\n");
                if ((temp_message[0] != '\0') && (q != -1) ){
                    // lock session
                    pthread_mutex_lock(x->write);

                    for (int i = 0; (i < x->syns->get_peers_num()) ;i++)
                        x->syns->connectto(i);

                    x->resources->load_board();
                    if ( (q > x->resources->get_messages_num()) || (q <= 0) ){
                        sprintf(msg, "3.1 UNKNOWN %d message not found\n", q);
                    }else{
                        // sync
                        int sync_check = 1;
                        for (int i = 0; (i < x->syns->get_peers_num()) && sync_check; i++){
                            if ( !x->syns->get_status(i) ){
                                for (int j = 0; j < i; j++){
                                    if (x->syns->get_status(j))
                                        x->syns->calloff(j);
                                }
                                sync_check = 0;
                            }else{
                                if (x->syns->commit(i)){
                                    for (int j = 0; j < i; j++){
                                        if (x->syns->get_status(j))
                                            x->syns->calloff(j);
                                    }
                                    sync_check = 0;
                                }
                            }
                        }

                        sleep(x->write_delay);

                        if (sync_check){
                            int result = x->resources->write_message(q, username, temp_message, len);
                            if (result){
                                // fail
                                for (int i = 0;i < x->syns->get_peers_num(); i++)
                                    if (x->syns->get_status(i)){
                                        x->syns->calloff(i);
                                    }
                            }else{
                                // success
                                sprintf(msg, "3.0 WROTE %d\n", q);
                                char msg_buff[1024];
                                x->resources->load_board();
                                x->resources->load_message(q, msg_buff);
                                if (x->syns->debug)
                                    printf("sync: commit\n");
                                for (int i = 0;i < x->syns->get_peers_num(); i++)
                                    if (x->syns->get_status(i)){
                                        x->syns->sendmessage(i, q, msg_buff);
                                    }
                            }
                        }
                    }

                    for (int i = 0; (i < x->syns->get_peers_num()) ;i++)
                        x->syns->close_connection(i);

                    // ***unlock***
                    pthread_mutex_unlock(x->write);
                
                }
            }else if (!strcmp("QUIT", command)){
                quit = 1;
                sprintf(msg, "4.0 BYE %s\n", username);
            }else{
                sprintf(msg, "Unknown command\n");
            }

            write(x->sock, msg, strlen(msg));

            if (quit)
                break;
        }
        for (int i = 0;i < x->syns->get_peers_num(); i++)
            if (x->syns->get_status(i))
                x->syns->close_connection(i);

        close(x->sock);
        x->sock = -1;
        pthread_mutex_unlock(&x->process_lock);
        printf("%s has exited\n", username);
    }
    return NULL;
}