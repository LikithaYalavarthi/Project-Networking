#include "bulletin.hpp"


bulletin_resources::bulletin_resources(){
    read = 0;
    messages_num = 0;
    messages.clear();
}

int bulletin_resources::load_board(){

    fstream fs;
    fs.open(filename, fstream::in | fstream::out);
    int x;
    while(fs >> x){
        if (x > messages_num){
            messages_num++;
        }

        string message;
        getline(fs, message);
        message.erase(message.begin());
        messages[x] = message;
    }
    
    fs.close();

    return 0;
}

int bulletin_resources::set_filename(char* name){
    sprintf(filename, "%s", name);
    return 0;
}

int bulletin_resources::load_message(int q, char* msg){
    if (q > messages_num || q <= 0)
        return 1;
    strcpy(msg, messages[q].c_str());
    return 0;
}

int bulletin_resources::get_messages_num(){
    return messages_num;
}

int bulletin_resources::write_message(int q, char* user, char* msg, int size){
    if ( q > messages_num )
        messages_num++;
    if (size == 0)
        return 1;
    fstream fs;
    fs.open(filename, fstream::out | fstream:: app);
    fs << q << "/" << user << "/";
    fs.write(msg, size);
    fs << endl;
    fs.close();
    return 0;
}

char* bulletin_resources::get_filename(){
    return filename;
}