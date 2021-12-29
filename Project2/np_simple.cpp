#include "np_simple.hpp"
#include <stdio.h>
#include <csignal>
#include <sys/wait.h>
#include <sys/socket.h> 
#include <netinet/in.h>

void child_handler(int signo){
	int status;
    while(waitpid(-1, &status, WNOHANG) > 0){
        // pass
    }
}

void npshell(int ssock){
    vector<struct numpipe> pipelist;
    setenv("PATH", "bin:.", 1);
    vector<string> exist_envp;
    exist_envp.push_back("PATH");
    pid_t pid;

    write(ssock, "% ", 2);
    while(1){
        int cmdl;
        char cmd[15000];
        vector<pid_t> pidlist;
        int pipe_type = -1;
        memset(&cmd, '\0', sizeof(cmd));
        if((cmdl = read(ssock, cmd, 15000)) < 0){
            if(errno == EINTR)
                continue;
        }
        // EOF or EOT
        if(cmdl == 0 || (cmdl == 1 && int(cmd[0]) == 4))
            exit(0);

        vector<struct prog> proglist = parse_cmd(split(cmd, ' '));

        // if input a newline char
        if(proglist.size() == 0){
            write(ssock, "% ", 2);
            continue;
        }

        // built-in command
        if(proglist.size() == 1 && (proglist[0].progname == "setenv" || proglist[0].progname == "printenv" || proglist[0].progname == "exit")){
            // setenv
            if(proglist[0].progname == "setenv"){
                int env_find = vec_str_find(exist_envp, proglist[0].arguments[0]);
                if(env_find == -1)
                    exist_envp.push_back(proglist[0].arguments[0]);
                if(proglist[0].arguments.size() == 2)
                    setenv(proglist[0].arguments[0].c_str(), proglist[0].arguments[1].c_str(), 1);
                else if(proglist[0].arguments.size() == 1)
                    setenv(proglist[0].arguments[0].c_str(), "", 1);
            }
            // printenv
            if(proglist[0].progname == "printenv"){
                int env_find = vec_str_find(exist_envp, proglist[0].arguments[0]);
                if(env_find != -1){
                    char* msg = getenv(proglist[0].arguments[0].c_str());
                    write(ssock, msg, strlen(msg));
                    write(ssock, "\n", 1);
                }
            }
            // exit
            if(proglist[0].progname == "exit")
                exit(0);
            // end of built-in command
            clear_pipe(pipelist);
            count_down(pipelist);
            write(ssock, "% ", 2);
            continue;
        }

        // execute each program
        for(int i = 0; i < proglist.size(); i++){
            // for each program, reset fd table
            int fd_table[3] = {0, ssock, ssock};

            // numberpipe
            if(proglist[i].op == 1 || proglist[i].op == 2){
                if(!check_same_pipe(pipelist, proglist[i].cnt, proglist[i].op))
                    create_pipe(pipelist, proglist[i].cnt, proglist[i].op);
                pipe_type = 1;
            }
            // pipe
            if(proglist[i].op == 3)
                create_pipe(pipelist, proglist[i].cnt, 3);
            // redirection
            if(proglist[i].op == 4){
                FILE* fp = fopen(proglist[i].arguments.back().c_str(), "w");
                proglist[i].arguments.pop_back();
                // stdout -> fp
                fd_table[1] = fileno(fp);
            }

            control_fd(fd_table, pipelist, proglist[i].cnt, proglist[i].op);

            // wait for any process
            while((pid = fork()) <0)
                waitpid(-1, 0, 0);
            // child
            if(pid == 0)
                execute(proglist[i], fd_table, ssock);
            // parent
            else{
                pidlist.push_back(pid);
                clear_pipe(pipelist);
                pipe_end(pipelist);
            }
        }

        // end of parse line
        if(pipe_type < 0){
            for(int i = 0; i < pidlist.size(); i++)
                waitpid(pidlist[i], 0, 0);
        }

        count_down(pipelist);
        write(ssock, "% ", 2);
    }
}

int passiveTCP(int port){
    struct sockaddr_in server_addr;
    int optval = 1;
    int msock;

    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    msock = socket(PF_INET, SOCK_STREAM, 0);
    setsockopt(msock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    bind(msock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(msock, 0);
    return msock;
}

int main(int argc, char** argv){
    if(argc != 2)
        exit(1);

    int msock, ssock;
    int port = atoi(argv[1]);
    pid_t pid;
    struct sockaddr_in client_addr;
    socklen_t client_len;

    // socket, bind, listen
    msock = passiveTCP(port);
    signal(SIGCHLD, child_handler);
    while(1){
        client_len = sizeof(client_addr);
        if((ssock = accept(msock, (struct sockaddr*)&client_addr, &client_len)) <0){
            if(errno == EINTR)
                continue;
        }

        pid = fork();
        // child
        if(pid == 0){
            close(msock);
            npshell(ssock);
            close(ssock);
            exit(0);
        }
        // parent
        else
            close(ssock);
    }

    return 0;
}
