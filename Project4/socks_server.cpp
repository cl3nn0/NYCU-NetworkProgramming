#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <regex>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

using namespace std;
using boost::asio::ip::tcp;

string check4a(string dip, string domain){
    if(int(dip.find("0.0.0.")) != -1 && int(dip.find("0.0.0.0")) == -1)
        return domain;
    return dip;
}

int check_firewall(string cd, string dip){
    ifstream fp("./socks.conf");
    vector<string> fs;
    string tmp;
    while(getline(fp, tmp))
        fs.push_back(tmp);
    fp.close();
    for(int i = 0; i < int(fs.size()); i++){
        vector<string> rs;
        boost::split(rs, fs[i], boost::is_any_of(" "), boost::token_compress_on);
        if(rs[0] != "permit")
            continue;
        if((rs[1] == "c" && cd == "1") || (rs[1] == "b" && cd == "2")){
            boost::replace_all(rs[2], "\r", "");
            boost::replace_all(rs[2], "\n", "");
            boost::replace_all(rs[2], ".", "\\.");
            boost::replace_all(rs[2], "*", "\\d{1,3}");
            regex r(rs[2]);
            if(regex_match(dip, r))
                return 1;
        }
    }
    return 0;
}

void server_print(string sh, int sp, string dh, string dp, string cd, string ar){
    string tmp;
    if(cd == "1")
        tmp = "CONNECT";
    else if(cd == "2")
        tmp = "BIND";
    string msg = "<S_IP>: " + sh + "\n";
    msg += "<S_PORT>: " + to_string(sp) + "\n";
    msg += "<D_IP>: " + dh + "\n";
    msg += "<D_PORT>: " + dp + "\n";
    msg += "<Command>: " + tmp + "\n";
    msg += "<Reply>: " + ar + "\n";
    cout << msg << endl;
}

class socks4_bind : public enable_shared_from_this<socks4_bind>{
public:
    socks4_bind(tcp::socket client, boost::asio::io_context& io_context) : client(move(client)), server{io_context}, acceptor_(io_context, tcp::endpoint(tcp::v4(), 0)){
        bind_reply[0] = 0;
        bind_reply[1] = 90;
        bind_reply[2] = (unsigned char)(acceptor_.local_endpoint().port()/256);
        bind_reply[3] = (unsigned char)(acceptor_.local_endpoint().port()%256);
        bind_reply[4] = 0;
        bind_reply[5] = 0;
        bind_reply[6] = 0;
        bind_reply[7] = 0;
        memset(cbuf, 0, 1536000);
        memset(sbuf, 0, 1536000);
    }

    // reply & accept & reply
    void start(){
        auto self(shared_from_this());
        memcpy(cbuf, bind_reply, 8);
        boost::asio::async_write(
            client,
            boost::asio::buffer(cbuf, 8),
            [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    acceptor_.async_accept(
                        [this, self](boost::system::error_code ec, tcp::socket socket){
                            if(!ec){
                                server = move(socket);
                                memcpy(cbuf, bind_reply, 8);
                                boost::asio::async_write(
                                    client,
                                    boost::asio::buffer(cbuf, 8),
                                    [this, self](boost::system::error_code ec, size_t length){
                                        if(!ec){
                                            read_from_client();
                                            read_from_server();
                                        }
                                    }
                                );
                                acceptor_.close();
                            }
                        }
                    );
                }
            }
        );
    }

private:
    tcp::socket client;
    tcp::socket server;
    tcp::acceptor acceptor_;
    unsigned char bind_reply[8], cbuf[1536000], sbuf[1536000];

    void read_from_client(){
        auto self(shared_from_this());
        client.async_read_some(
            boost::asio::buffer(cbuf, 1536000),
            [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    boost::asio::async_write(
                        server,
                        boost::asio::buffer(cbuf, length),
                        [this, self](boost::system::error_code ec, size_t length){
                            if(!ec){
                                read_from_client();
                            }
                        }
                    );
                }
            }
        );
    }

    void read_from_server(){
        auto self(shared_from_this());
        server.async_read_some(
            boost::asio::buffer(sbuf, 1536000),
            [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    boost::asio::async_write(
                        client,
                        boost::asio::buffer(sbuf, length),
                        [this, self](boost::system::error_code ec, size_t length){
                            if(!ec){
                                read_from_server();
                            }
                        }
                    );
                }
            }
        );
    }
};

class socks4_connect : public enable_shared_from_this<socks4_connect>{
public:
    socks4_connect(tcp::socket client, tcp::socket server, tcp::endpoint endpoint) : client(move(client)), server(move(server)), connect_reply{0, 90, 0, 0, 0, 0, 0, 0}{
        endpoint_ = endpoint;
        memset(cbuf, 0, 1536000);
        memset(sbuf, 0, 1536000);
    }

    // connect and write reply
    void start(){
        auto self(shared_from_this());
        server.async_connect(
            endpoint_,
            [this, self](const boost::system::error_code& ec){
                if(!ec){
                    memcpy(cbuf, connect_reply, 8);
                    boost::asio::async_write(
                        client,
                        boost::asio::buffer(cbuf, 8),
                        [this, self](boost::system::error_code ec, size_t length){
                            if(!ec){
                                // read from each endpoint
                                read_from_client();
                                read_from_server();
                            }
                        }
                    );
                }
            }
        );
    }

private:
    tcp::socket client;
    tcp::socket server;
    tcp::endpoint endpoint_;
    unsigned char connect_reply[8], cbuf[1536000], sbuf[1536000];

    void read_from_client(){
        auto self(shared_from_this());
        client.async_read_some(
            boost::asio::buffer(cbuf, 1536000),
            [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    boost::asio::async_write(
                        server,
                        boost::asio::buffer(cbuf, length),
                        [this, self](boost::system::error_code ec, size_t length){
                            if(!ec){
                                read_from_client();
                            }
                        }
                    );
                }
            }
        );
    }

    void read_from_server(){
        auto self(shared_from_this());
        server.async_read_some(
            boost::asio::buffer(sbuf, 1536000),
            [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    boost::asio::async_write(
                        client,
                        boost::asio::buffer(sbuf, length),
                        [this, self](boost::system::error_code ec, size_t length){
                            if(!ec){
                                read_from_server();
                            }
                        }
                    );
                }
            }
        );
    }
};

class socks4_request : public enable_shared_from_this<socks4_request>{
public:
    socks4_request(tcp::socket socket, boost::asio::io_context& io_context) : req(move(socket)), io_context(io_context){}

    void parse_request(){
        auto self(shared_from_this());
        req.async_read_some(
            boost::asio::buffer(data_, 1536000),
            [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    buf[0] = to_string(data_[0]);
                    buf[1] = to_string(data_[1]);
                    buf[2] = to_string(int(data_[2] * 256) + int(data_[3]));
                    // IP
                    buf[3] = to_string(data_[4]);
                    for(int i = 0; i < 3; i++)
                        buf[3] += ("." + to_string(data_[5+i]));
                    // domain
                    int s = -1, e = -1;
                    for(int i = 8; i < int(length); i++){
                        if(data_[i] == 0 && s == -1)
                            s = i;
                        else if(data_[i] == 0)
                            e = i;
                        if(s > -1 && e > -1)
                            break;
                    }
                    buf[4] = "";
                    if(e > s+1){
                        for(int i = s+1; i < e; i++)
                            buf[4] += data_[i];
                    }
                    print_server_msg();
                }
            }
        );
    }

private:
    tcp::socket req;
    boost::asio::io_context& io_context;
    unsigned char data_[1536000];
    // VN, CD, dest_port, dest_IP, domain
    string buf[5];

    void socks4_reject(){
        auto self(shared_from_this());
        unsigned char reject[8] = {0, 91, 0, 0, 0, 0, 0, 0};
        memcpy(data_, reject, 8);
        boost::asio::async_write(
            req,
            boost::asio::buffer(data_, 8),
            [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    req.close();
                }
            }
        );
    }

    void print_server_msg(){
        string s4a = check4a(buf[3], buf[4]);
        tcp::resolver::query query(s4a, buf[2]);
        tcp::resolver resolver(io_context);
        tcp::resolver::iterator it = resolver.resolve(query);
        tcp::socket server(io_context);

        if(check_firewall(buf[1], buf[3])){
            server_print(req.remote_endpoint().address().to_string(), req.remote_endpoint().port(), buf[3], buf[2], buf[1], "Accept");
            // connect request
            if(buf[1] == "1")
                make_shared<socks4_connect>(move(req), move(server), it->endpoint())->start();
            // bind request
            else if(buf[1] == "2")
                make_shared<socks4_bind>(move(req), io_context)->start();
        }
        else{
            // reject
            server_print(req.remote_endpoint().address().to_string(), req.remote_endpoint().port(), buf[3], buf[2], buf[1], "Reject");
            socks4_reject();
        }
    }
};

class server{
public:
    server(boost::asio::io_context& io_context, short port) : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)), io_context(io_context){
        do_accept();
    }

private:
    tcp::acceptor acceptor_;
    boost::asio::io_context& io_context;

    void do_accept(){
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if(!ec){
                    io_context.notify_fork(boost::asio::io_context::fork_prepare);
                    // child
                    if(fork() == 0){
                        io_context.notify_fork(boost::asio::io_context::fork_child);
                        acceptor_.close();
                        make_shared<socks4_request>(move(socket), io_context)->parse_request();
                    }
                    // parent
                    else{
                        io_context.notify_fork(boost::asio::io_context::fork_parent);
                        socket.close();
                        do_accept();
                    }
                }
            }
        );
    }
};

int main(int argc, char** argv){
    if (argc != 2){
        cerr << "Usage: ./socks_server [port]" << endl;
        exit(1);
    }
    boost::asio::io_context io_context;
    server s(io_context, atoi(argv[1]));
    io_context.run();
    return 0;
}
