#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
using namespace std;

struct numpipe{
    int in;
    int out;
    int cnt;
    int op;
};

struct prog{
    string progname;
    vector<string> arguments;
    int cnt;
    // op : 0 = null, 1 = |n, 2 = !n, 3 = pipe, 4 = redirection
    int op;
};

void count_down(vector<struct numpipe> &pipelist){
    for(int i = 0; i < pipelist.size(); i++)
        pipelist[i].cnt--;
}

bool check_same_pipe(vector<struct numpipe> pipelist, int _cnt, int _op){
    for(int i = 0; i < pipelist.size(); i++){
        if(pipelist[i].cnt == _cnt && pipelist[i].op == _op)
            return true;
    }
    return false;
}

void create_pipe(vector<struct numpipe> &pipelist, int _cnt, int _op){
    int pipe_fd[2];
    struct numpipe tmp;

    if(pipe(pipe_fd) == -1)
        exit(1);

    tmp.in = pipe_fd[0];
    tmp.out = pipe_fd[1];
    tmp.cnt = _cnt;
    tmp.op = _op;
    pipelist.push_back(tmp);
}

void clear_pipe(vector<struct numpipe> &pipelist){
    for(int i = 0; i < pipelist.size(); i++){
        if(pipelist[i].cnt == 0){
            close(pipelist[i].in);
            close(pipelist[i].out);
            pipelist.erase(pipelist.begin() + i);
            break;
        }
    }
}

void pipe_end(vector<struct numpipe> &pipelist){
    for(int i = 0; i < pipelist.size(); i++){
        if(pipelist[i].op == 3)
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
    int flag = 1;
    for(int i = 0; i < cmd.size(); i++){
        if(flag){
            tmp.progname = cmd[i];
            flag = 0;
        }
        else if(cmd[i][0] != '|' && cmd[i][0] != '!' && cmd[i][0] != '>')
            tmp.arguments.push_back(cmd[i]);
        // this is end of prog
        else{
            if(cmd[i][0] == '|' && cmd[i].length() > 1){
                tmp.op = 1;
                tmp.cnt = stoi(&cmd[i][1]);
            }
            if(cmd[i][0] == '!' && cmd[i].length() > 1){
                tmp.op = 2;
                tmp.cnt = stoi(&cmd[i][1]);
            }
            if(cmd[i][0] == '|' && cmd[i].length() == 1){
                tmp.op = 3;
                tmp.cnt = 1;
            }
            if(cmd[i][0] == '>' && cmd[i].length() == 1){
                tmp.op = 4;
                tmp.cnt = 0;
                flag = 1;
                tmp.arguments.push_back(cmd[i+1]);
                res.push_back(tmp);
                break;
            }

            flag = 1;
            res.push_back(tmp);
            tmp.arguments.clear();
            tmp.cnt = 0;
            tmp.op = 0;
        }
    }
    if(!flag)
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

void control_fd(int (&fd_table)[3], vector<struct numpipe> pipelist, int _cnt, int _op){
    // pipe in
    for(int i = 0; i < pipelist.size(); i++){
        if(pipelist[i].cnt == 0){
            close(pipelist[i].out);
            fd_table[0] = pipelist[i].in;
        }
    }
    // pipe out
    for(int i = 0; i < pipelist.size(); i++){
        // numberpipe_1
        if(_op == 1 && pipelist[i].cnt == _cnt && pipelist[i].op == 1)
            fd_table[1] = pipelist[i].out;
        // numberpipe_2
        if(_op == 2 && pipelist[i].cnt == _cnt && pipelist[i].op == 2){
            fd_table[1] = pipelist[i].out;
            fd_table[2] = pipelist[i].out;
        }
        // pipe
        if(_op == 3 && pipelist[i].cnt == 1 && pipelist[i].op == 3)
            fd_table[1] = pipelist[i].out;
    }
}

void replace_fd(int *fd_table){
    dup2(fd_table[0], 0);
    dup2(fd_table[1], 1);
    dup2(fd_table[2], 2);
    if(fd_table[0] != 0)
        close(fd_table[0]);
    if(fd_table[1] != 1)
        close(fd_table[1]);
    if(fd_table[2] != 2)
        close(fd_table[2]);
}

void execute(struct prog program, int fd_table[3]){
    replace_fd(fd_table);

    // test command
    string prog_path;
    if(!find_cmd(program, prog_path)){
        string un_cmd;
        un_cmd = "Unknown command: [" + program.progname + "].\n";
        cerr << un_cmd;
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
