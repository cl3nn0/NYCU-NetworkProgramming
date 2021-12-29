#include "npshell.hpp"
#include <stdio.h>
#include <csignal>
#include <sys/wait.h>

void child_handler(int signo){
	int status;
    while(waitpid(-1, &status, WNOHANG) > 0){
        // pass
    }
}

int main(){
    setenv("PATH", "bin:.", 1);
    vector<string> exist_envp;
    exist_envp.push_back("PATH");

    vector<struct numpipe> pipelist;
    string cmd;
    pid_t pid;

    cout << "% ";
    while(getline(cin, cmd)){
        vector<struct prog> proglist = parse_cmd(split(cmd, ' '));
        vector<pid_t> pidlist;
        int pipe_type = -1;

        // if input a newline char
        if(proglist.size() == 0){
            cout << "% ";
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
                if(env_find != -1)
                    cout << getenv(proglist[0].arguments[0].c_str()) << endl;
            }
            // exit
            if(proglist[0].progname == "exit")
                exit(0);
            // end of built-in command
            clear_pipe(pipelist);
            count_down(pipelist);
            cout << "% ";
            continue;
        }

        // execute each program
        for(int i = 0; i < proglist.size(); i++){
            // for each program, reset fd table
            int fd_table[3] = {0, 1, 2};

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
            signal(SIGCHLD, child_handler);

            // wait for any process
            while((pid = fork()) <0)
                waitpid(-1, 0, 0);
            // child
            if(pid == 0)
                execute(proglist[i], fd_table);
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
        cout << "% ";
    }

    return 0;
}
