#include <stdlib.h>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <fcntl.h>
#include <csignal>
#include <sys/shm.h>
#define SHMKEY ((key_t)345)
using namespace std;

struct env{
    string key;
    string value;
};

struct numpipe{
    int in, out;
    int cnt, op;
    int owner_id, send_id, recv_id;
};

struct prog{
    // op : 0 = null, 1 = |n, 2 = !n, 3 = pipe, 4 = redirection, 5 = user_pipe
    int cnt, op;
    // use for user pipe
    int send_id, recv_id;
    string progname;
    vector<string> arguments;
};

struct client_info{
    int id, fd, pid;
    unsigned short port;
    char ip[20];
    char name[100];
    char msg[2048];
    int send_list[30];
    int recv_list[30];
};

int shmID;
client_info *client_start;
client_info *client_current;
vector<struct numpipe> pipelist;

void str_rmtail(string &s){
    while(1){
        if(s.size() > 0 && (s[s.size()-1] == '\r' || s[s.size()-1] == '\n'))
            s.erase(s.size()-1);
        else
            break;
    }
}

bool HasClient(int _id){
    for(int i = 0; i < 30; i++){
        if(client_start[i].id == _id)
            return true;
    }
    return false; 
}

void init_all_client(){
    for(int i = 0; i < 30; i++){
        (client_start+i)->id = -1;
        (client_start+i)->fd = -1;
        (client_start+i)->pid = -1;
        for(int j = 0; j < 30; j++){
            ((client_start+i)->send_list)[j] = -1;
            ((client_start+i)->recv_list)[j] = -1;
        }
        strcpy((client_start+i)->msg, "");
    }
}

client_info* new_client(){
    for(int i = 0; i < 30; i++){
        if((client_start+i)->pid == -1){
            (client_start+i)->id = i+1;
            return (client_start+i);
        }
    }
    return NULL;
}

void set_user_pipe_info(int _send_id, int _recv_id){
    for(int i = 0 ; i < 30 ; i++){
        if((client_current->send_list)[i] == -1){
            (client_current->send_list)[i] = _recv_id;
            break;
        }
    }
    for(int i = 0 ; i < 30 ; i++){
        if((client_start[_recv_id-1].recv_list)[i] == -1){
            (client_start[_recv_id-1].recv_list)[i] = _send_id;
            break;
        }
    }
}

void welcome_msg(int fd){
    string msg = "****************************************\n";
    msg = msg +  "** Welcome to the information server. **\n";
    msg = msg +  "****************************************\n";
    write(fd, msg.c_str(), msg.length());
}

void broadcast(string msg, int op, int _id, int _id2){
    // op code
    // 0 : login,   1 : logout,     2 : yell,       3 : name
    // 4 : user pipe(read),         5 : user pipe(write)        6 : user pipe(read + write)
    string content;
    str_rmtail(msg);
    
    switch(op){
        case 0:
            content = "*** User '" + string(client_current->name) + "' entered from " + string(client_current->ip) + ":" + to_string(client_current->port) + ". ***\n";
            break;
        case 1:
            content = "*** User '" + string(client_current->name) + "' left. ***\n";
            break;
        case 2:
            content = "*** " + string(client_current->name) + " yelled ***: " + msg + "\n";
            break;
        case 3:
            content = "*** User from " + string(client_current->ip) + ":" + to_string(client_current->port) + " is named '" + string(client_current->name) + "'. ***\n";
            break;
        case 4:
            content = "*** " + string(client_current->name) + " (#" + to_string(client_current->id) + ") just received from " + string(client_start[_id-1].name) + " (#" + to_string(client_start[_id-1].id) + ") by '" + msg + "' ***\n";
            break;
        case 5:
            content = "*** " + string(client_current->name) + " (#" + to_string(client_current->id) + ") just piped '" + msg + "' to " + string(client_start[_id-1].name) + " (#" + to_string(client_start[_id-1].id) + ") ***\n";
            break;
        case 6:
            content = "*** " + string(client_current->name) + " (#" + to_string(client_current->id) + ") just received from " + string(client_start[_id-1].name) + " (#" + to_string(client_start[_id-1].id) + ") by '" + msg + "' ***\n";
            content = content + "*** " + string(client_current->name) + " (#" + to_string(client_current->id) + ") just piped '" + msg + "' to " + string(client_start[_id2-1].name) + " (#" + to_string(client_start[_id2-1].id) + ") ***\n";
            break;
    }
    for(int i = 0; i < 30; i++){
        if((client_start+i)->pid  != -1){
            strcpy((client_start+i)->msg, content.c_str());
            kill((client_start+i)->pid, SIGUSR1);
        }
    }
}

void remove_client(){
    for(int i = 0; i < pipelist.size(); i++){
        if(pipelist[i].owner_id == client_current->id){
            close(pipelist[i].in);
            close(pipelist[i].out);
            pipelist.erase(pipelist.begin() + i);
        }
    }
}

void count_down(){
    for(int i = 0; i < pipelist.size(); i++){
        if(pipelist[i].owner_id == client_current->id && (pipelist[i].op == 1 || pipelist[i].op == 2))
            pipelist[i].cnt--;
    }
}

bool check_user_pipe(int _send_id, int _recv_id){
    // I am recver
    if(_recv_id == client_current->id){
        for(int i = 0 ; i < 30 ; i++){
            if((client_current->recv_list)[i] == _send_id)
                return true;
        }
    }
    // I am sender
    else{
        for(int i = 0 ; i < 30 ; i++){
            if((client_current->send_list)[i] == _recv_id)
                return true;
        }
    }
    return false;
}

bool check_same_pipe(int _id, int _cnt, int _op){
    for(int i = 0; i < pipelist.size(); i++){
        if(pipelist[i].owner_id == _id && pipelist[i].cnt == _cnt && pipelist[i].op == _op)
            return true;
    }
    return false;
}

void create_pipe(int _cnt, int _op, int _owner_id, int _send_id, int _recv_id){
    int pipe_fd[2];
    struct numpipe tmp;

    if(pipe(pipe_fd) == -1)
        exit(1);

    tmp.in = pipe_fd[0];
    tmp.out = pipe_fd[1];
    tmp.cnt = _cnt;
    tmp.op = _op;
    tmp.owner_id = _owner_id;
    tmp.send_id = _send_id;
    tmp.recv_id = _recv_id;
    pipelist.push_back(tmp);
}

void clear_pipe(){
    for(int i = 0; i < pipelist.size(); i++){
        if(pipelist[i].cnt == 0 && pipelist[i].owner_id == client_current->id){
            close(pipelist[i].in);
            close(pipelist[i].out);
            pipelist.erase(pipelist.begin() + i);
            break;
        }
    }
}

void pipe_end(){
    for(int i = 0; i < pipelist.size(); i++){
        if(pipelist[i].op == 3 && pipelist[i].owner_id == client_current->id)
            pipelist[i].cnt--;
    }
}

vector<string> split(string line, char delimiter){
    istringstream iline(line);
    vector<string> res;
    string tmp;
    if(delimiter == ' '){
        while(iline >> tmp)
            res.push_back(tmp);
    }
    else{
        while(getline(iline, tmp, delimiter))
            res.push_back(tmp);
    }
    return res;
}

int vec_str_find(vector<string> vs, string s){
    int idx = -1;
    for(int i = 0; i < vs.size(); i++){
        if(s == vs[i]){
            idx = i;
            break;
        }
    }
    return idx;
}

void client_exit(){
    broadcast("", 1, -1, -1);
    client_current->id = -1;
    client_current->pid = -1;

    // reset client info
    for(int i = 0; i < 30; i++){
        (client_current->send_list)[i] = -1;
        (client_current->recv_list)[i] = -1;
        // clear user pipe about current client
        if(client_start[i].pid != -1){
            for(int j = 0; j < 30; j++){
                if((client_start[i].send_list)[j] == client_current->id)
                    (client_start[i].send_list)[j] = -1;
                if((client_start[i].recv_list)[j] == client_current->id)
                    (client_start[i].recv_list)[j] = -1;
            }
        }
    }
    remove_client();
    shmdt(client_start);
    shmctl(shmID, IPC_RMID, 0);
}

vector<struct prog> parse_cmd(vector<string> cmd){
    vector<struct prog> res;
    struct prog tmp;
    tmp.cnt = 0;
    tmp.op = 0;
    tmp.send_id = 0;
    tmp.recv_id = 0;
    int no_prog = 1;

    for(int i = 0; i < cmd.size(); i++){
        if(no_prog){
            tmp.progname = cmd[i];
            no_prog = 0;
        }
        // user pipe (read)
        else if(cmd[i][0] == '<' && cmd[i].length() > 1){
            tmp.recv_id = stoi(&cmd[i][1]);
        }
        // delimiter that means end of prog
        else if(cmd[i][0] != '|' && cmd[i][0] != '!' && cmd[i][0] != '>')
            tmp.arguments.push_back(cmd[i]);
        // this is end of prog
        else{
            // number pipe
            if(cmd[i][0] == '|' && cmd[i].length() > 1){
                tmp.op = 1;
                tmp.cnt = stoi(&cmd[i][1]);
            }
            // number pipe2
            if(cmd[i][0] == '!' && cmd[i].length() > 1){
                tmp.op = 2;
                tmp.cnt = stoi(&cmd[i][1]);
            }
            // pipe
            if(cmd[i][0] == '|' && cmd[i].length() == 1){
                tmp.op = 3;
                tmp.cnt = 1;
            }
            // redirection
            if(cmd[i][0] == '>' && cmd[i].length() == 1){
                tmp.op = 4;
                tmp.cnt = 0;
                no_prog = 1;
                tmp.arguments.push_back(cmd[i+1]);
                res.push_back(tmp);
                break;
            }
            // user pipe (write)
            if(cmd[i][0] == '>' && cmd[i].length() > 1){
                tmp.op = 5;
                tmp.send_id = stoi(&cmd[i][1]);
                // check >1 <2
                if(i != cmd.size() - 1){
                    if(cmd[i+1][0] == '<' && cmd[i+1].length() > 1){
                        tmp.recv_id = stoi(&cmd[i+1][1]);
                        tmp.cnt = 0;
                        no_prog = 1;
                        // no args
                        res.push_back(tmp);
                        break;
                    }
                }
            }

            no_prog = 1;
            res.push_back(tmp);
            tmp.arguments.clear();
            tmp.cnt = 0;
            tmp.op = 0;
            tmp.recv_id = 0;
            tmp.send_id = 0;
        }
    }

    if(!no_prog)
        res.push_back(tmp);
    return res;
}

bool find_cmd(struct prog program, string &prog_path){
    string path = getenv("PATH");
    vector<string> pathlist = split(path, ':');
    struct stat s;
    for(int i = 0; i < pathlist.size(); i++){
        prog_path = pathlist[i] + "/" + program.progname;
        if(stat(prog_path.c_str(), &s) == 0 && s.st_mode & S_IFREG && s.st_mode & S_IXUSR)
            return true;
    }

    // check absolute path
    prog_path = program.progname;
    if(stat(prog_path.c_str(), &s) == 0 && s.st_mode & S_IFREG && s.st_mode & S_IXUSR)
        return true;

    return false;
}

int create_fifo(const char *fifo_name){
    struct stat s;
    if(stat(fifo_name, &s) < 0){
        if ( errno != ENOENT ){
            perror( "stat failed" );
            return -1;
        }
    }
    // file exist
    else{
        if(unlink(fifo_name) < 0){
            perror( "unlink failed" );
            return -1;
        }
    }

    if(mkfifo(fifo_name, 0666) < 0){
        perror( "mkfifo failed" );
        return -1;
    }
    return 0;
}

void control_fd(int (&fd_table)[3], int _cnt, int _op, int _send_id, int _recv_id, int user_pipe_error[2], int user_pipe_flag){
    int ioflag[2] = {0, 0};

    // pipe error (in)
    if(user_pipe_error[0] == 1){
        fd_table[0] = -1;
        ioflag[0] = 1;
    }
    // pipe error (out)
    if(user_pipe_error[1] == 1 && _op == 5){
        fd_table[1] = -1;
        ioflag[1] = 1;
    }

    // user pipe (in)
    if(ioflag[0] == 0 && _recv_id > 0){
        string recv_fname = "user_pipe/" + to_string(_recv_id) + "_" + to_string(client_current->id);
        // fd_table[0] = open(recv_fname.c_str(), O_RDONLY | O_NONBLOCK);
        fd_table[0] = open(recv_fname.c_str(), O_RDONLY);
        ioflag[0] = 1;
    }

    // number pipe & pipe (in)
    for(int i = 0; i < pipelist.size(); i++){
        if(ioflag[0] == 0 && pipelist[i].cnt == 0 && _recv_id == 0 && pipelist[i].owner_id == client_current->id){
            close(pipelist[i].out);
            fd_table[0] = pipelist[i].in;
            ioflag[0] = 1;
        }
    }

    // set fd (out)
    for(int i = 0; i < pipelist.size(); i++){
        // number pipe
        if(ioflag[1] == 0 && _op == 1 && pipelist[i].cnt == _cnt && pipelist[i].op == 1 && pipelist[i].owner_id == client_current->id){
            fd_table[1] = pipelist[i].out;
            ioflag[1] = 1;
        }
        // number pipe (with error)
        if(ioflag[1] == 0 && _op == 2 && pipelist[i].cnt == _cnt && pipelist[i].op == 2 && pipelist[i].owner_id == client_current->id){
            fd_table[1] = pipelist[i].out;
            fd_table[2] = pipelist[i].out;
            ioflag[1] = 1;
        }
        // pipe
        if(ioflag[1] == 0 && _op == 3 && pipelist[i].cnt == 1 && pipelist[i].op == 3 && pipelist[i].owner_id == client_current->id){
            fd_table[1] = pipelist[i].out;
            ioflag[1] = 1;
        }
    }

    // set fd (user pipe out)
    if(user_pipe_flag == 1){
        string send_fname = "user_pipe/" + to_string(client_current->id) + "_" + to_string(_send_id);
        create_fifo(send_fname.c_str());
        fd_table[1] = open(send_fname.c_str(), O_WRONLY);
    }
}

void replace_fd(int *fd_table, int _fd){
    int error_io[2];

    dup2(_fd, 1);
    dup2(_fd, 2);
    close(_fd);

    // in
    if(fd_table[0] == -1){
        error_io[0] = open("/dev/null", O_RDONLY);
        dup2(error_io[0], 0);
        close(error_io[0]);
    }
    else if(fd_table[0] != 0){
        dup2(fd_table[0], 0);
        close(fd_table[0]);
    }

    // out
    if(fd_table[1] == -1){
        error_io[1] = open("/dev/null", O_WRONLY);
        dup2(error_io[1], 1);
    }
    else if(fd_table[1] != _fd)
        dup2(fd_table[1], 1);
    
    // err
    if(fd_table[2] != _fd){
        dup2(fd_table[2], 2);
        close(fd_table[2]);
    }

    // close
    if(fd_table[1] != _fd){
        if(fd_table[0] == -1)
            close(error_io[1]);
        else
            close(fd_table[1]);
    }
}

void execute(struct prog program, int fd_table[3], int _fd){
    replace_fd(fd_table, _fd);

    // test command
    string prog_path;
    if(!find_cmd(program, prog_path)){
        string un_cmd;
        un_cmd = "Unknown command: [" + program.progname + "].\n";
        write(2, un_cmd.c_str(), un_cmd.length());
        exit(1);
    }

    // if there is an executable file, set argv
    char **argv = new char* [program.arguments.size()+2];   // extra room for program name and sentinel
    argv[0] = new char [program.progname.size()+1];
    strcpy(argv[0], program.progname.c_str());
    for(int i = 0; i < program.arguments.size(); i++){
        argv[i+1] = new char [program.arguments[i].size()+1];
        strcpy(argv[i+1], program.arguments[i].c_str());
    }
    argv[program.arguments.size()+1] = NULL;

    execv(prog_path.c_str(), argv);
}
