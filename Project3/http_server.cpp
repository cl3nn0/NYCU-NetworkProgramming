#include <iostream>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <boost/asio.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

using boost::asio::ip::tcp;
using namespace std;

void child_handler(int signo){
	int status;
    while(waitpid(-1, &status, WNOHANG) > 0){
        // pass
    }
}

class session : public enable_shared_from_this<session>{
public:
    session(tcp::socket socket) : socket_(move(socket)){}
    void start(){do_read();}

private:
    tcp::socket socket_;
    char data_[131072];
    string path;
    vector<string> req_header;

    void parse_request(){
        string data = string(data_);
        vector<string> req_list, first_line;
        boost::split(req_list, data, boost::is_any_of("\r\n"), boost::token_compress_on);
        boost::split(first_line, req_list[0], boost::is_any_of(" "), boost::token_compress_on);
        req_header.push_back("REQUEST_METHOD");
        req_header.push_back(first_line[0]);
        req_header.push_back("REQUEST_URI");
        req_header.push_back(first_line[1]);
        req_header.push_back("SERVER_PROTOCOL");
        req_header.push_back(first_line[2]);
        req_header.push_back("HTTP_HOST");
        req_header.push_back(req_list[1].substr(req_list[1].find(" ")+1));
        req_header.push_back("SERVER_ADDR");
        req_header.push_back(socket_.local_endpoint().address().to_string());
        req_header.push_back("SERVER_PORT");
        req_header.push_back(to_string(socket_.local_endpoint().port()));
        req_header.push_back("REMOTE_ADDR");
        req_header.push_back(socket_.remote_endpoint().address().to_string());  
        req_header.push_back("REMOTE_PORT");
        req_header.push_back(to_string(socket_.remote_endpoint().port())); 
        // check query string
        req_header.push_back("QUERY_STRING");
        if(strstr(first_line[1].c_str(), "?")){
            path = "." + first_line[1].substr(0, first_line[1].find("?"));
            req_header.push_back(first_line[1].substr(first_line[1].find("?")+1));
        }
        else{
            path = "." + first_line[1];
            req_header.push_back("");
        }
    }

    void prepare_child(){
        // setenv
        for(int i = 0; i < int(req_header.size()/2); i++)
            setenv(req_header[i*2].c_str(), req_header[i*2 + 1].c_str(), 1);
        // control fd
        dup2(socket_.native_handle(), 0);
        dup2(socket_.native_handle(), 1);
        close(socket_.native_handle());
    }

    void do_read(){
        auto self(shared_from_this());
        socket_.async_read_some(
            boost::asio::buffer(data_, 131072),
            [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    parse_request();
                    do_write();
                }
            }
        );
    }

    void do_write(){
        auto self(shared_from_this());
        strcpy(data_, "HTTP/1.1 200 OK\r\n"); 
        socket_.async_write_some(
            boost::asio::buffer(data_, strlen(data_)),
            [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    pid_t pid = fork();
                    // child
                    if(pid == 0){
                        char **argv = new char*[2];
                        argv[0] = new char[path.size()+1];
                        strcpy(argv[0], path.c_str());
                        argv[1] = NULL;

                        prepare_child();
                        execv(path.c_str(), argv);
                        exit(0);
                    }
                    // parent
                    else
                        socket_.close();
                }
            }
        );
    }
};

class server{
public:
    server(boost::asio::io_context& io_context, short port) : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)){
        do_accept();
    }

private:
    tcp::acceptor acceptor_;
    void do_accept(){
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket){
                if(!ec){
                    make_shared<session>(move(socket))->start();
                }
                do_accept();
            }
        );
    }
};

int main(int argc, char** argv){
    signal(SIGCHLD, child_handler);
    if (argc != 2){
        cerr << "Usage: ./http_server <port>\n";
        return 1;
    }

    boost::asio::io_context io_context;
    server s(io_context, atoi(argv[1]));
    io_context.run();
    return 0;
}
