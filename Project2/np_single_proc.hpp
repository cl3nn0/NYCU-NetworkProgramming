#include <stdlib.h>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <fcntl.h>
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
    int id, fd;
    unsigned short port;
    string name, ip;
    vector<struct env> env_list;
};

int exist_id[30] = {0};
int msock, nfds;
vector<struct client_info> client_list;
vector<struct numpipe> pipelist;
fd_set afds;

void str_rmtail(string &s){
    while(1){
        if(s.size() > 0 && (s[s.size()-1] == '\r' || s[s.size()-1] == '\n'))
            s.erase(s.size()-1);
        else
            break;
    }
}

void welcome_msg(int fd){
    string msg = "****************************************\n";
    msg = msg +  "** Welcome to the information server. **\n";
    msg = msg +  "****************************************\n";
    write(fd, msg.c_str(), msg.length());
}

int find_client(int _id, int _fd){
    if(_id > _fd){
        for(int i = 0; i < client_list.size(); i++){
            if(client_list[i].id == _id)
                return i;
        }
    }
    else{
        for(int i = 0; i < client_list.size(); i++){
            if(client_list[i].fd == _fd)
                return i;
        }
    }
    return -1;
}

void broadcast(struct client_info client, int another_id, int op, string msg){
    // op code
    // 0 : login,   1 : logout,     2 : yell,       3 : name
    // 4 : user pipe(read),         5 : user pipe(write)
    int idx = 0;
    string content;
    str_rmtail(msg);

    if(another_id)
        idx = find_client(another_id, -1);
    
    switch(op){
        case 0:
            content = "*** User '" + client.name + "' entered from " + client.ip + ":" + to_string(client.port) + ". ***\n";
            break;
        case 1:
            content = "*** User '" + client.name + "' left. ***\n";
            break;
        case 2:
            content = "*** " + client.name + " yelled ***: " + msg + "\n";
            break;
        case 3:
            content = "*** User from " + client.ip + ":" + to_string(client.port) + " is named '" + msg + "'. ***\n";
            break;
        case 4:
            content = "*** " + client.name + " (#" + to_string(client.id) + ") just received from " + client_list[idx].name + " (#" + to_string(client_list[idx].id) + ") by '" + msg + "' ***\n";
            break;
        case 5:
            content = "*** " + client.name + " (#" + to_string(client.id) + ") just piped '" + msg + "' to " + client_list[idx].name + " (#" + to_string(client_list[idx].id) + ") ***\n";
            break;    
    }

    for(int fd = 0; fd < nfds; fd++){
        if(fd != msock && FD_ISSET(fd, &afds))
            write(fd, content.c_str(), content.length());
    }
}

void reset_env(int idx){
    clearenv();
    for(int i = 0; i < client_list[idx].env_list.size(); i++)
        setenv(client_list[idx].env_list[i].key.c_str(), client_list[idx].env_list[i].value.c_str(), 1);
}

void remove_client(struct client_info client){
    for(int i = 0; i < pipelist.size(); i++){
        if(pipelist[i].owner_id == client.id || pipelist[i].recv_id == client.id){
            close(pipelist[i].in);
            close(pipelist[i].out);
            pipelist.erase(pipelist.begin() + i);
        }
    }
    for(int i = 0; i < client_list.size(); i++){
        if(client_list[i].id == client.id){
            client_list.erase(client_list.begin() + i);
            break;
        }
    }
    exist_id[client.id - 1] = 0;
}

void count_down(int _id){
    for(int i = 0; i < pipelist.size(); i++){
        if(pipelist[i].owner_id == _id && (pipelist[i].op == 1 || pipelist[i].op == 2))
            pipelist[i].cnt--;
    }
}

bool check_user_pipe(int _send_id, int _recv_id){
    for(int i = 0; i < pipelist.size(); i++){
        if(pipelist[i].op == 5 && pipelist[i].send_id == _send_id && pipelist[i].recv_id == _recv_id)
            return true;
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

void clear_pipe(int _send_id, int _recv_id){
    for(int i = 0; i < pipelist.size(); i++){
        if(pipelist[i].cnt == 0 && pipelist[i].owner_id == _recv_id){
            close(pipelist[i].in);
            close(pipelist[i].out);
            pipelist.erase(pipelist.begin() + i);
            break;
        }
        else if(pipelist[i].op == 5 && pipelist[i].send_id == _send_id && pipelist[i].recv_id == _recv_id){
            close(pipelist[i].in);
            close(pipelist[i].out);
            pipelist.erase(pipelist.begin() + i);
            break;
        }
    }
}

void pipe_end(int _id){
    for(int i = 0; i < pipelist.size(); i++){
        if(pipelist[i].op == 3 && pipelist[i].owner_id == _id)
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

void control_fd(int idx, int (&fd_table)[3], int _cnt, int _op, int _send_id, int _recv_id, int user_pipe_error[2]){
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

    // set fd (in)
    // user pipe in & number pipe & pipe
    for(int i = 0; i < pipelist.size(); i++){
        if(ioflag[0] == 0 && ((pipelist[i].op == 5 && pipelist[i].send_id == _recv_id && pipelist[i].recv_id == client_list[idx].id)
        || (pipelist[i].cnt == 0 && pipelist[i].owner_id == client_list[idx].id && _recv_id == 0))){
            close(pipelist[i].out);
            fd_table[0] = pipelist[i].in;
            ioflag[0] = 1;
        }
    }

    // set fd (out)
    for(int i = 0; i < pipelist.size(); i++){
        // numberpipe_1
        if(ioflag[1] == 0 && _op == 1 && pipelist[i].cnt == _cnt && pipelist[i].op == 1 && pipelist[i].owner_id == client_list[idx].id){
            fd_table[1] = pipelist[i].out;
            ioflag[1] = 1;
        }
        // numberpipe_2
        if(ioflag[1] == 0 && _op == 2 && pipelist[i].cnt == _cnt && pipelist[i].op == 2 && pipelist[i].owner_id == client_list[idx].id){
            fd_table[1] = pipelist[i].out;
            fd_table[2] = pipelist[i].out;
            ioflag[1] = 1;
        }
        // pipe
        if(ioflag[1] == 0 && _op == 3 && pipelist[i].cnt == 1 && pipelist[i].op == 3 && pipelist[i].owner_id == client_list[idx].id){
            fd_table[1] = pipelist[i].out;
            ioflag[1] = 1;
        }
        // user pipe
        if(ioflag[1] == 0 && _op == 5 && pipelist[i].op == 5 && pipelist[i].send_id == client_list[idx].id && pipelist[i].recv_id == _send_id){
            fd_table[1] = pipelist[i].out;
            ioflag[1] = 1;
        }
    }
    cout << to_string(fd_table[0]) << " " << fd_table[1] << " " << fd_table[2] << endl;
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
