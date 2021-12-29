#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

using namespace std;
using boost::asio::ip::tcp;

struct session_info{
    string id, host, port, file;
}session_list[5];

vector<string> parse_query(){
    vector<string> tmp, result;
    string env = getenv("QUERY_STRING");
    boost::split(tmp, env, boost::is_any_of("&"), boost::token_compress_on);
    for(int i = 0; i < int(tmp.size()); i++){
        if(tmp[i].length() > 3){
            if(tmp[i].substr(2,1) == "="){
                result.push_back(tmp[i].substr(0,2));
                result.push_back(tmp[i].substr(3));
            }
        }
    }
    return result;
}

int vec_str_find(vector<string> vs, string s){
    int idx = -1;
    for(int i = 0; i < int(vs.size()); i++){
        if(s == vs[i]){
            idx = i;
            break;
        }
    }
    return idx;
}

void print_html(){
    string content =
    "Content-type: text/html\r\n\r\n"
    "<!DOCTYPE html>\n"
    "<html lang=\"en\">\n"
        "<head>\n"
            "<meta charset=\"UTF-8\" />\n"
            "<title>NP Project 3 Console</title>\n"
            "<link\n"
                "rel=\"stylesheet\"\n"
                "href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\n"
                "integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\n"
                "crossorigin=\"anonymous\"\n"
            "/>\n"
            "<link\n"
                "href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\n"
                "rel=\"stylesheet\"\n"
            "/>\n"
            "<link\n"
                "rel=\"icon\"\n"
                "type=\"image/png\"\n"
                "href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\n"
            "/>\n"
            "<style>\n"
                "* {\n"
                    "font-family: 'Source Code Pro', monospace;\n"
                    "font-size: 1rem !important;\n"
                "}\n"
                "body {\n"
                    "background-color: #212529;\n"
                "}\n"
                "pre {\n"
                    "color: #cccccc;\n"
                "}\n"
                "b {\n"
                    "color: #01b468;\n"
                "}\n"
            "</style>\n"
        "</head>\n"
        "<body>\n"
            "<table class=\"table table-dark table-bordered\">\n"
                "<thead>\n"
                    "<tr>\n";
    for(int i = 0; i < 5; i++){
        if(session_list[i].host != "")
            content += "<th scope=\"col\">" + session_list[i].host + ":" + session_list[i].port + "</th>\n";
    }
    content +=
                    "</tr>\n"
                "</thead>\n"
                "<tbody>\n"
                    "<tr>\n";
    for(int i = 0; i < 5; i++){
        if(session_list[i].host != "")
            content += "<td><pre id=\"s" + session_list[i].id + "\" class=\"mb-0\"></pre></td>\n";
    }
    content +=
                    "</tr>\n"
                "</tbody>\n"
            "</table>\n"
        "</body>\n"
    "</html>\n";

    cout << content << endl;
}

class session : public enable_shared_from_this<session>{
public:
    session(tcp::socket socket, string id, tcp::endpoint endpoint) : socket_(move(socket)){
        id_ = id;
        endpoint_ = endpoint;
        fp.open("./test_case/" + session_list[stoi(id_)].file);
    }

    void start(){
        auto self(shared_from_this());
        socket_.async_connect(
            endpoint_,
            [this, self](boost::system::error_code ec){
                if(!ec){
                    do_read();
                }
            }
        );
    }

private:
    tcp::socket socket_;
    tcp::endpoint endpoint_;
    char data_[1536000];
    string id_;
    ifstream fp;

    void html_escape(string& s){
        boost::replace_all(s, "&", "&amp;");
        boost::replace_all(s, "\"", "&quot;");
        boost::replace_all(s, "\'", "&apos;");
        boost::replace_all(s, "<", "&lt;");
        boost::replace_all(s, ">", "&gt;");
        boost::replace_all(s, "\n", "&NewLine;");
        boost::replace_all(s, "\r", "");
    }

    void output_html(string str, int c){
        // c = 1  ->  command (bold)
        html_escape(str);
        if(c)
            cout << "<script>document.getElementById(\'s" + id_ + "\').innerHTML += \'<b>" + str + "</b>\';</script>" << endl;
        else
            cout << "<script>document.getElementById(\'s" + id_ + "\').innerHTML += \'" + str + "\';</script>" << endl;
    }

    void do_read(){
        auto self(shared_from_this());
        socket_.async_read_some(
            boost::asio::buffer(data_, 1536000),
            [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    string data = string(data_);
                    memset(data_, 0, 1536000);
                    output_html(data, 0);
                    if(strstr(data.c_str(), "% "))
                        do_write();
                    else
                        do_read();
                }
            }
        );
    }

    void do_write(){
        auto self(shared_from_this());
        string cmd;
        getline(fp, cmd);
        cmd = cmd + "\n";
        output_html(cmd, 1);

        boost::asio::async_write(
            socket_,
            boost::asio::buffer(cmd.c_str(), strlen(cmd.c_str())),
            [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    do_read();
                }
            }
        );
    }
};

int main(){
    boost::asio::io_context io_context;
    // parse query_string to session_list
    vector<string> query_strings = parse_query();
    for(int i = 0; i < 5; i++){
        session_list[i].id = to_string(i);
        vector<string> keys = {"h" + to_string(i), "p" + to_string(i), "f" + to_string(i)};
        if(vec_str_find(query_strings, keys[0]) != -1 && vec_str_find(query_strings, keys[1]) != -1 && vec_str_find(query_strings, keys[2]) != -1){
            session_list[i].host = query_strings[vec_str_find(query_strings, keys[0]) + 1];
            session_list[i].port = query_strings[vec_str_find(query_strings, keys[1]) + 1];
            session_list[i].file = query_strings[vec_str_find(query_strings, keys[2]) + 1];
        }
        else{
            session_list[i].host = "";
            session_list[i].port = "";
            session_list[i].file = "";
        }
    }

    print_html();
    for(int i = 0; i < 5; i++){
        if(session_list[i].host != ""){
            tcp::resolver::query query(session_list[i].host, session_list[i].port);
            tcp::resolver resolver(io_context);
            tcp::resolver::iterator it = resolver.resolve(query);
            tcp::socket socket(io_context);
            make_shared<session>(move(socket), session_list[i].id, it->endpoint())->start();
        }
    }
    io_context.run();
    return 0;
}
