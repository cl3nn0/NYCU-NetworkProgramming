#include "np_single_proc.hpp"
#include <stdio.h>
#include <csignal>
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

int npshell(string cmd, int idx){
    pid_t pid;
    // msg function
    if(cmd.find("yell ") == 0){
        string msg;
        msg = cmd.substr(5);
        broadcast(client_list[idx], 0, 2, msg);
    }
    else if(cmd.find("tell ") == 0){
        int id_len, sendID;
        string msg;
        msg = cmd.substr(5);
        while(msg[0] == ' ')
            msg = msg.substr(1);
        id_len = msg.find(' ');
        sendID = atoi(msg.substr(0, id_len).c_str());
        msg = msg.substr(id_len + 1);
        str_rmtail(msg);
        msg = "*** " + client_list[idx].name + " told you ***: " + msg + "\n";

        int recv_idx = find_client(sendID, -1);
        if(recv_idx != -1)
            write(client_list[recv_idx].fd, msg.c_str(), msg.length());
        else{
            msg = "*** Error: user #" + to_string(sendID) + " does not exist yet. ***\n";
            write(client_list[idx].fd, msg.c_str(), msg.length());
        }
    }
    // other function
    else{
        int pipe_type = -1;
        vector<pid_t> pidlist;
        vector<struct prog> proglist = parse_cmd(split(cmd, ' '));
        vector<string> exist_envp;
        for(int i = 0; i < client_list[idx].env_list.size(); i++)
            exist_envp.push_back(client_list[idx].env_list[i].key);

        // if input a newline char
        if(proglist.size() == 0){
            write(client_list[idx].fd, "% ", 2);
            return 0;
        }

        // built-in command
        if(proglist.size() == 1 && (proglist[0].progname == "setenv" || proglist[0].progname == "printenv" || proglist[0].progname == "exit" || proglist[0].progname == "name" || proglist[0].progname == "who")){
            // setenv
            if(proglist[0].progname == "setenv"){
                int env_find = vec_str_find(exist_envp, proglist[0].arguments[0]);
                // new env
                if(env_find == -1){
                    exist_envp.push_back(proglist[0].arguments[0]);
                    struct env e;
                    e.key = proglist[0].arguments[0];
                    e.value = proglist[0].arguments[1];
                    client_list[idx].env_list.push_back(e);
                }
                // had this env
                else{
                    for(int i = 0; i < client_list[idx].env_list.size(); i++){
                        if(client_list[idx].env_list[i].key == proglist[0].arguments[0]){
                            client_list[idx].env_list[i].value = proglist[0].arguments[1];
                            break;
                        }
                    }
                }
                // normal input
                if(proglist[0].arguments.size() == 2)
                    setenv(proglist[0].arguments[0].c_str(), proglist[0].arguments[1].c_str(), 1);
                // space arg
                else if(proglist[0].arguments.size() == 1)
                    setenv(proglist[0].arguments[0].c_str(), "", 1);
            }

            // printenv
            if(proglist[0].progname == "printenv"){
                int env_find = vec_str_find(exist_envp, proglist[0].arguments[0]);
                if(env_find != -1){
                    char* msg = getenv(proglist[0].arguments[0].c_str());
                    write(client_list[idx].fd, msg, strlen(msg));
                    write(client_list[idx].fd, "\n", 1);
                }
            }

            // name
            if(proglist[0].progname == "name"){
                int has_same_name = 0;
                string tmp_name = proglist[0].arguments[0];
                for(int i = 0; i < client_list.size(); i++){
                    if(tmp_name == client_list[i].name && client_list[i].id != client_list[idx].id){
                        string msg = "*** User '" + tmp_name + "' already exists. ***\n";
                        write(client_list[idx].fd, msg.c_str(), msg.length());
                        has_same_name = 1;
                    }
                }
                if(!has_same_name){
                    client_list[idx].name = tmp_name;
                    broadcast(client_list[idx], 0, 3, tmp_name);
                }
            }

            // who
            if(proglist[0].progname == "who"){
                string msg = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
                write(client_list[idx].fd, msg.c_str(), msg.length());
                for(int i = 0; i < 30; i++){
                    if(exist_id[i]){
                        int tmp_id = i+1;
                        int tmp_idx = find_client(tmp_id, -1);
                        msg = to_string(client_list[tmp_idx].id) + "\t" + client_list[tmp_idx].name + "\t" + client_list[tmp_idx].ip + ":" + to_string(client_list[tmp_idx].port);
                        if(client_list[tmp_idx].id == client_list[idx].id)
                            msg = msg + "\t<-me\n";
                        else
                            msg = msg + "\n";
                        write(client_list[idx].fd, msg.c_str(), msg.length());
                    }
                }
            }

            // exit
            if(proglist[0].progname == "exit")
                return -1;
            
            // end of built-in command
            clear_pipe(-1, client_list[idx].id);
            count_down(client_list[idx].id);
            write(client_list[idx].fd, "% ", 2);
            return 0;
        }

        // execute each program
        for(int i = 0; i < proglist.size(); i++){
            // for each program, reset fd table
            int fd_table[3] = {0, client_list[idx].fd, client_list[idx].fd};
            int user_pipe_error[2] = {0, 0};

            // has user pipe (in)
            if(proglist[i].recv_id){
                if(proglist[i].recv_id > 30){
                    string msg = "*** Error: user #" + to_string(proglist[i].recv_id) + " does not exist yet. ***\n";
                    write(client_list[idx].fd, msg.c_str(), msg.length());
                    user_pipe_error[0] = 1;
                }
                else if(!exist_id[proglist[i].recv_id - 1]){
                    string msg = "*** Error: user #" + to_string(proglist[i].recv_id) + " does not exist yet. ***\n";
                    write(client_list[idx].fd, msg.c_str(), msg.length());
                    user_pipe_error[0] = 1;
                }
                else if(!check_user_pipe(proglist[i].recv_id, client_list[idx].id)){
                    string msg = "*** Error: the pipe #" + to_string(proglist[i].recv_id) + "->#" + to_string(client_list[idx].id) + " does not exist yet. ***\n";
                    write(client_list[idx].fd, msg.c_str(), msg.length());
                    user_pipe_error[0] = 1;
                }
                else
                    broadcast(client_list[idx], proglist[i].recv_id, 4, cmd);
            }

            // numberpipe
            if(proglist[i].op == 1 || proglist[i].op == 2){
                if(!check_same_pipe(client_list[idx].id, proglist[i].cnt, proglist[i].op))
                    create_pipe(proglist[i].cnt, proglist[i].op, client_list[idx].id, 0, 0);
                pipe_type = 1;
            }
            // pipe
            if(proglist[i].op == 3)
                create_pipe(proglist[i].cnt, 3, client_list[idx].id, 0, 0);
            // redirection
            if(proglist[i].op == 4){
                FILE* fp = fopen(proglist[i].arguments.back().c_str(), "w");
                proglist[i].arguments.pop_back();
                // stdout -> fp
                fd_table[1] = fileno(fp);
            }
            // user pipe
            if(proglist[i].op == 5){
                if(proglist[i].send_id > 30){
                    string msg = "*** Error: user #" + to_string(proglist[i].send_id) + " does not exist yet. ***\n";
                    write(client_list[idx].fd, msg.c_str(), msg.length());
                    user_pipe_error[1] = 1;
                }
                else if(!exist_id[proglist[i].send_id-1]){
                    string msg = "*** Error: user #" + to_string(proglist[i].send_id) + " does not exist yet. ***\n";
                    write(client_list[idx].fd, msg.c_str(), msg.length());
                    user_pipe_error[1] = 1;
                }
                else if(check_user_pipe(client_list[idx].id, proglist[i].send_id)){
                    string msg = "*** Error: the pipe #" + to_string(client_list[idx].id) + "->#" + to_string(proglist[i].send_id) + " already exists. ***\n";
                    write(client_list[idx].fd, msg.c_str(), msg.length());
                    user_pipe_error[1] = 1;
                }
                else{
                    broadcast(client_list[idx], proglist[i].send_id, 5, cmd);
                    create_pipe(-1, 5, client_list[idx].id, client_list[idx].id, proglist[i].send_id);
                }
                pipe_type = 1;
            }

            control_fd(idx, fd_table, proglist[i].cnt, proglist[i].op, proglist[i].send_id, proglist[i].recv_id, user_pipe_error);

            // wait for any process
            while((pid = fork()) < 0)
                waitpid(-1, 0, 0);
            // child
            if(pid == 0)
                execute(proglist[i], fd_table, client_list[idx].fd);
            // parent
            else{
                pidlist.push_back(pid);
                clear_pipe(proglist[i].recv_id, client_list[idx].id);
                pipe_end(client_list[idx].id);
            }
        }
        // end of line
        if(pipe_type < 0){
            for(int i = 0; i < pidlist.size(); i++)
                waitpid(pidlist[i], 0, 0);
        }
    }

    count_down(client_list[idx].id);
    write(client_list[idx].fd, "% ", 2);
    return 0;
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

    int ssock;
    int port = atoi(argv[1]);
    struct sockaddr_in client_addr;
    socklen_t client_len;
    setenv("PATH", "bin:.", 1);

    // socket, bind, listen
    msock = passiveTCP(port);
    signal(SIGCHLD, child_handler);

    fd_set rfds;
    nfds = getdtablesize();
    FD_ZERO(&afds);
    FD_SET(msock, &afds);

    while(1){
        // DEBUG
        // cout << "########## afds ##########" << endl;
        // for(int i = 0; i < nfds; i++){
        //     if(FD_ISSET(i, &afds))
        //         cout << i << " " ;
        // }
        // cout << "\n" << endl;
        // DEBUG

        // step 1   ->  check rfds
        memcpy(&rfds, &afds, sizeof(rfds));
        if(select(nfds, &rfds, (fd_set*)0, (fd_set*)0, (timeval*)0) < 0){
            if(errno == EINTR)
                continue;
        }
        // DEBUG
        // cout << "##### rfds after select #####" << endl;
        // for(int i = 0; i < nfds; i++){
        //     if(FD_ISSET(i, &rfds))
        //         cout << i << " " ;
        // }
        // cout << "\n" << endl;
        // DEBUG
        
        // step 2   ->  accept
        if(FD_ISSET(msock, &rfds)){
            client_len = sizeof(client_addr);
            ssock = accept(msock, (struct sockaddr*)&client_addr, &client_len);
            // DEBUG
            cout << "***** accept fd : " << ssock  << " *****\n" << endl;
            // DEBUG
            // initialize new client
            struct client_info c;
            struct env e;
            e.key = "PATH";
            e.value = "bin:.";
            for(int i = 0; i < 30; i++){
                if(exist_id[i] == 0){
                    exist_id[i] = 1;
                    c.id = i+1;
                    break;
                }
            }
            c.fd = ssock;
            c.port = ntohs(client_addr.sin_port);
            c.name = "(no name)";
            c.ip = inet_ntoa(client_addr.sin_addr);
            c.env_list.push_back(e);
            client_list.push_back(c);
            // end of initialize new client
            FD_SET(ssock, &afds);
            welcome_msg(ssock);
            broadcast(c, 0, 0, "");
            write(ssock, "% ", 2);
        }

        // step 3   ->  handle read / write
        for(int fd = 0; fd < nfds; fd++){
            if(fd != msock && FD_ISSET(fd, &rfds)){
                int cmdl, idx;
                char cmd[15000];
                idx = find_client(-1, fd);
                memset(&cmd, '\0', 15000);
               
                if((cmdl = read(fd, cmd, 15000)) < 0){
                    if(errno == EINTR)
                        continue;
                }
                // handle EOF
                else if(cmdl == 0 || (cmdl == 1 && int(cmd[0]) == 4)){
                    broadcast(client_list[idx], 0, 1, "");
                    remove_client(client_list[idx]);
                    close(fd);
                    FD_CLR(fd, &afds);
                }
                else{
                    // DEBUG
                    cout << "id : " << client_list[idx].id << "\texecute : " << cmd << endl;
                    // DEBUG
                    int alive;
                    string str(cmd);
                    reset_env(idx);
                    if((alive = npshell(cmd, idx) < 0)){
                        broadcast(client_list[idx], 0, 1, "");
                        remove_client(client_list[idx]);
                        close(fd);
                        FD_CLR(fd, &afds);
                    }
                }
            }
        }
    }
    return 0;
}
