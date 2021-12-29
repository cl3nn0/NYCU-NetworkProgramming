#include "np_multi_proc.hpp"
#include <stdio.h>
#include <sys/wait.h>
#include <sys/socket.h> 
#include <netinet/in.h>
#include <arpa/inet.h>

void child_handler(int signo){
	int status;
    while(waitpid(-1, &status, WNOHANG) > 0){
        // pass
    }
}

void client_handler(int signo){
    string msg(client_current->msg);
    write(client_current->fd, msg.c_str(), msg.length());
    strcpy(client_current->msg, "");
}

int npshell(int ssock){
    string cmd;
    vector<string> exist_envp;
    exist_envp.push_back("PATH");
    pid_t pid;
    vector<pid_t> pidlist;

    while(1){
        write(client_current->fd, "% ", 2);
        getline(cin, cmd);
        str_rmtail(cmd);
        pidlist.clear();
        int broadcast_status = 0;

        // msg function
        if(cmd.find("yell ") == 0){
            string msg = cmd.substr(5);
            broadcast(msg, 2, -1, -1);
            clear_pipe();
            count_down();
            continue;
        }
        else if(cmd.find("tell ") == 0){
            int id_len, sendID;
            string msg = cmd.substr(5);
            while(msg[0] == ' ')
                msg = msg.substr(1);
            id_len = msg.find(' ');
            sendID = atoi(msg.substr(0, id_len).c_str());
            msg = msg.substr(id_len + 1);
            str_rmtail(msg);
            msg = "*** " + string(client_current->name) + " told you ***: " + msg + "\n";
            if(HasClient(sendID)){
                for(int i = 0; i < 30; i++){
                    if((client_start+i)->id  == sendID){
                        strcpy((client_start+i)->msg, msg.c_str());
                        kill((client_start+i)->pid, SIGUSR1);
                        break;
                    }
                }
            }
            else{
                msg = "*** Error: user #" + to_string(sendID) + " does not exist yet. ***\n";
                write(client_current->fd, msg.c_str(), msg.length());
            }
            clear_pipe();
            count_down();
            continue;
        }
        // other function
        else{
            int pipe_type = -1;
            vector<struct prog> proglist = parse_cmd(split(cmd, ' '));

            // if input a newline char
            if(proglist.size() == 0)
                continue;

            // built-in command
            if(proglist.size() == 1 && (proglist[0].progname == "setenv" || proglist[0].progname == "printenv" || proglist[0].progname == "exit" || proglist[0].progname == "name" || proglist[0].progname == "who")){
                // setenv
                if(proglist[0].progname == "setenv"){
                    int env_find = vec_str_find(exist_envp, proglist[0].arguments[0]);
                    // new env
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
                        write(client_current->fd, msg, strlen(msg));
                        write(client_current->fd, "\n", 1);
                    }
                }

                // name
                if(proglist[0].progname == "name"){
                    int has_same_name = 0;
                    string tmp_name = proglist[0].arguments[0];
                    for(int i = 0; i < 30; i++){
                        if(client_start[i].name == tmp_name){
                            string msg = "*** User '" + tmp_name + "' already exists. ***\n";
                            write(client_current->fd, msg.c_str(), msg.length());
                            has_same_name = 1;
                        }
                    }
                    if(!has_same_name){
                        strcpy(client_current->name, tmp_name.c_str());
                        broadcast("", 3, -1, -1);
                    }
                }

                // who
                if(proglist[0].progname == "who"){
                    string msg = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
                    write(client_current->fd, msg.c_str(), msg.length());
                    for(int i = 0; i < 30; i++){
                        if((client_start+i)->pid  != -1){
                            msg = to_string((client_start+i)->id) + "\t" + (client_start+i)->name + "\t" + (client_start+i)->ip + ":" + to_string((client_start+i)->port);
                            write(client_current->fd, msg.c_str(), msg.length());
                            if((client_start+i)->id == client_current->id)
                                write(client_current->fd, "\t<-me\n", 6);
                            else
                                write(client_current->fd, "\n", 1);
                        }
                    }
                }

                // exit
                if(proglist[0].progname == "exit")
                    return -1;

                // end of built-in command
                clear_pipe();
                count_down();
                continue;
            }

            // check user pipe * 2
            if(proglist[0].recv_id > 0){
                if(HasClient(proglist[0].recv_id) && check_user_pipe(proglist[0].recv_id, client_current->id)){
                    if(proglist[proglist.size()-1].send_id > 0){
                        if(HasClient(proglist[proglist.size()-1].send_id) && (!check_user_pipe(client_current->id, proglist[proglist.size()-1].send_id))){
                            broadcast(cmd, 6, proglist[0].recv_id, proglist[proglist.size()-1].send_id);
                            broadcast_status = 3;
                        }
                    }
                }
            }

            // execute each program
            for(int i = 0; i < proglist.size(); i++){
                // for each program, reset fd table
                int fd_table[3] = {ssock, ssock, ssock};
                int user_pipe_error[2] = {0, 0};
                int user_pipe_flag = 0;

                // has user pipe in
                if(proglist[i].recv_id){
                    if(!HasClient(proglist[i].recv_id)){
                        string msg = "*** Error: user #" + to_string(proglist[i].recv_id) + " does not exist yet. ***\n";
                        write(client_current->fd, msg.c_str(), msg.length());
                        user_pipe_error[0] = 1;
                    }
                    // recv_id  ->  current client  (not exist)
                    else if(!check_user_pipe(proglist[i].recv_id, client_current->id)){
                        string msg = "*** Error: the pipe #" + to_string(proglist[i].recv_id) + "->#" + to_string(client_current->id) + " does not exist yet. ***\n";
                        write(client_current->fd, msg.c_str(), msg.length());
                        user_pipe_error[0] = 1;
                    }
                    else if(broadcast_status == 0){
                        broadcast(cmd, 4, proglist[i].recv_id, -1);
                        broadcast_status = 1;
                    }
                }

                // numberpipe
                if(proglist[i].op == 1 || proglist[i].op == 2){
                    if(!check_same_pipe(client_current->id, proglist[i].cnt, proglist[i].op))
                        create_pipe(proglist[i].cnt, proglist[i].op, client_current->id, 0, 0);
                    pipe_type = 1;
                }

                // pipe
                if(proglist[i].op == 3)
                    create_pipe(proglist[i].cnt, 3, client_current->id, 0, 0);
                
                // redirection
                if(proglist[i].op == 4){
                    FILE* fp = fopen(proglist[i].arguments.back().c_str(), "w");
                    proglist[i].arguments.pop_back();
                    fd_table[1] = fileno(fp);
                }

                // user pipe
                if(proglist[i].op == 5){
                    pipe_type = 1;
                    if(!HasClient(proglist[i].send_id)){
                        string msg = "*** Error: user #" + to_string(proglist[i].send_id) + " does not exist yet. ***\n";
                        write(client_current->fd, msg.c_str(), msg.length());
                        user_pipe_error[1] = 1;
                    }
                    // current client already write to send_id
                    else if(check_user_pipe(client_current->id, proglist[i].send_id)){
                        string msg = "*** Error: the pipe #" + to_string(client_current->id) + "->#" + to_string(proglist[i].send_id) + " already exists. ***\n";
                        write(client_current->fd, msg.c_str(), msg.length());
                        user_pipe_error[1] = 1;
                    }
                    else if(broadcast_status == 3){
                        set_user_pipe_info(client_current->id, proglist[i].send_id);
                        user_pipe_flag = 1;
                    }
                    else{
                        broadcast(cmd, 5, proglist[i].send_id, -1);
                        set_user_pipe_info(client_current->id, proglist[i].send_id);
                        user_pipe_flag = 1;
                    }
                }

                // wait for any process
                while((pid = fork()) < 0)
                    waitpid(-1, 0, 0);
                // child
                if(pid == 0){
                    control_fd(fd_table, proglist[i].cnt, proglist[i].op, proglist[i].send_id, proglist[i].recv_id, user_pipe_error, user_pipe_flag);
                    execute(proglist[i], fd_table, ssock);
                }
                // parent
                else{
                    pidlist.push_back(pid);
                    clear_pipe();
                    pipe_end();
                }
            }
            // clear user pipe table after each line
            if(broadcast_status == 3 || broadcast_status == 1){
                // current receive from recv_id
                for(int i = 0; i < 30; i++){
                    if((client_current->recv_list)[i] == proglist[0].recv_id){
                        (client_current->recv_list)[i] = -1;
                        break;
                    }
                }
                // recv_id send to current
                for(int i = 0; i < 30; i++){
                    if((client_start[proglist[0].recv_id-1].send_list)[i] == client_current->id){
                        (client_start[proglist[0].recv_id-1].send_list)[i] = -1;
                        break;
                    }
                }
            }

            // end of line
            if(pipe_type < 0){
                for(int i = 0; i < pidlist.size(); i++)
                    waitpid(pidlist[i], 0, 0);
            }
            count_down();
        }
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
    listen(msock, 5);
    return msock;
}

int main(int argc, char** argv){
    if(argc != 2)
        exit(1);

    int msock, ssock;
    int port = atoi(argv[1]);
    struct sockaddr_in client_addr;
    socklen_t client_len;

    // socket, bind, listen
    msock = passiveTCP(port);
    signal(SIGCHLD, child_handler);
    signal(SIGUSR1, client_handler);

    shmID = shmget(SHMKEY, sizeof(client_info) * 30, IPC_CREAT | 0600);
    client_start = (client_info*)shmat(shmID, NULL, 0);
    init_all_client();

    while(1){
        pid_t pid;
        client_len = sizeof(client_addr);
        if((ssock = accept(msock, (struct sockaddr*)&client_addr, &client_len)) < 0){
            if(errno == EINTR)
                continue;
        }

        pid = fork();
        // child
        if(pid == 0){
            close(msock);
            dup2(ssock, 0);
            dup2(ssock, 1);
            dup2(ssock, 2);
            setenv("PATH", "bin:.", 1);
            client_current = new_client();
            client_current->fd = ssock;
            client_current->pid = getpid();
            strcpy(client_current->name, "(no name)");
            strcpy(client_current->ip, inet_ntoa(client_addr.sin_addr));
            client_current->port = ntohs(client_addr.sin_port);

            welcome_msg(ssock);
            broadcast("", 0, -1, -1);
            npshell(ssock);
            client_exit();
            close(ssock);
            exit(0);
        }
        // parent
        else
            close(ssock);
    }
    return 0;
}
