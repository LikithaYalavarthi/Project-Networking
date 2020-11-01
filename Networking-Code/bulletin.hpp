#include <pthread.h>
#include <map>
#include <cstdio>
#include <fstream>
#include <string>
#include <cstring>
#include <iostream>

using namespace std;

class bulletin_resources{

protected:

    char filename[1024];
    int read;
    map< int, string> messages;
    int messages_num;

public:

    bulletin_resources();

    int get_messages_num();
    char* get_filename();

    int set_filename(char* name);
    // load from bbfile
    int load_board();
    int load_message(int q, char* msg);

    int write_message(int q, char* user, char* msg, int size);

};