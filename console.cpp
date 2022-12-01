#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <vector>
#include <map>

#define connect_number 5
using boost::asio::ip::tcp;

static std::map<char, std::string> escape_dic = {
    {'\t', "&Tab;"},
    {'\n', "&NewLine;"},
    {' ', "&nbsp;"},
    {'\"', "&quot;"},
    {'<', "&lt;"},
    {'>', "&gt;"},
    {'\r', ""},
    {'\'', "&#39;"},
    {'(', "&#40;"},
    {')', "&#41;"}
};

struct Con_Info {
  std::string hostname;
  std::string port;
  std::string file;
};

std::vector<struct Con_Info> Parse_Query();
void output_shell(int session, std::string output);
void output_command(int session, std::string output);
void print_terminal(std::string &output, std::vector<struct Con_Info> server_info, int body, int number);
void print_html_label(std::vector<struct Con_Info> server_info);
void html_escape(std::string &output);

class session
  : public std::enable_shared_from_this<session> {
public:
  session(boost::asio::io_context& io_context, struct Con_Info con_info, int id)
    : socket_(io_context),
      resolver_(io_context),
      info(con_info),
      body(id) {
  }

  void start() {
    cmd_count = 0;
    memset(data_, 0, max_length);
    do_read_file();
  }

private:
    void do_read_file() {
        std::string file_name = "./test_case/" + info.file;
        char buffer[1024];
        int filefd = open(file_name.c_str(), O_RDONLY);
        while(read(filefd, buffer, 1023)) {
            std::string cmdline(buffer);
            std::stringstream read_str(cmdline);
            std::string cmd;
            while(std::getline(read_str, cmd, '\n')) {
                cmd += "\n\0";
                cmds.push_back(cmd);
            }
        }
        close(filefd);
        do_resolve();
    }

    void do_resolve() {
        auto self(shared_from_this());
        resolver_.async_resolve(info.hostname, info.port,
            [this, self](boost::system::error_code ec,
            tcp::resolver::results_type r_end_point) {
                if(!ec) {
                    end_point = r_end_point;
                    do_connect();
                }
            });
    }

    void do_connect() {
        auto self(shared_from_this());
        boost::asio::async_connect(socket_, end_point,
            [this, self](boost::system::error_code ec, tcp::endpoint) {
                if (!ec) {
                    do_read();
                }
        });
    }

    void do_read() {
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(data_, max_length - 1),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec) {
                    data_[length] = '\0';
                    std::string data(data_);
                    memset(data_, 0, length);
                    output_shell(body , data);

                    if (data.find("%") != std::string::npos) {
                        do_write();
                    } else {
                        do_read();
                    }
                }
            });
    }

    void do_write() {
        auto self(shared_from_this());
        if (cmd_count == cmds.size()) {
            return;
        } else {
            cmd = cmds[cmd_count++];
        }
        output_command(body , cmd);

        boost::asio::async_write(socket_, boost::asio::buffer(cmd.c_str(), cmd.length()),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec) {
                    if(cmd.find("exit") != std::string::npos)
                        return;
                    do_read();
                }
            });
    }

    tcp::socket socket_;
    tcp::resolver resolver_;
    enum {max_length = 32768};
    char data_[max_length];
    struct Con_Info info;
    int body, length;
    long unsigned int cmd_count;
    std::vector<std::string> cmds;
    tcp::resolver::results_type end_point;
    std::string cmd;
};

int main() {
    std::vector<struct Con_Info> query_info = Parse_Query();
    print_html_label(query_info);
    try {
    boost::asio::io_context io_context;
    for(int i = 0 ; i < connect_number ; ++i) {
        if(query_info[i].hostname == "")
            break;
        std::make_shared<session>(io_context, query_info[i], i)->start();
    }
    io_context.run();

    } catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}

std::vector<struct Con_Info> Parse_Query() {
    std::vector<struct Con_Info> res;

    std::string Query(getenv("QUERY_STRING"));
    std::stringstream read_query(Query);
    std::string line;

    for(int i = 0; i < connect_number; ++i) {
        struct Con_Info user;

        std::getline(read_query, line, '&');
        user.hostname = line.substr(line.find("=") + 1);
        std::getline(read_query, line, '&');
        user.port = line.substr(line.find("=") + 1);
        std::getline(read_query, line, '&');
        user.file = line.substr(line.find("=") + 1);
        res.push_back(user);
    }
    return res;
}

void output_shell(int session, std::string output) {
    html_escape(output);
    std::cout << "<script>document.getElementById('s" << std::to_string(session) <<
                 "').innerHTML += '" << output <<
                 "';</script>" << std::flush;
}

void output_command(int session, std::string output) {
    html_escape(output);
    std::cout << "<script>document.getElementById('s" << std::to_string(session) <<
                 "').innerHTML += '<b>" << output <<
                 "</b>';</script>" << std::flush;
}

void html_escape(std::string &output) {
    for(long unsigned int i = 0; i < output.length();) {
        if(output[i] == '&') {
            output.replace(i, 1, "&amp;");
            i+=5;
        } else
        ++i;
    }
    for (auto escape_pair : escape_dic) {
        long unsigned int index;
        while((index = output.find(escape_pair.first)) != std::string::npos) {
            output.replace(index, 1, escape_pair.second);
        }
    }
}

void print_terminal(std::string &output, std::vector<struct Con_Info> server_info, int body, int number) {
    output +=
        "<table class=\"table table-dark table-bordered\">"
            "<thead>"
                "<tr>";

    for(int i = 0; i < number; ++i) {
        output +=
                    "<th scope=\"col\"><pre>";
        output +=
            server_info[body + i].hostname + ":";
        output +=
            server_info[body + i].port + "</pre></th>";
    }
    output +=
                "</tr>"
            "</thead>"
            "<tbody>"
                "<tr>";

    for(int i = 0; i < number; ++i) {
    
        output +=
                    "<td><pre id=\"s";
        output += std::to_string(body + i);
        output += "\" class=\"mb-0\"></pre></td>";
    }

    output +=
                "</tr>"
            "</tbody>"
        "</table>";
}

void print_html_label(std::vector<struct Con_Info> server_info) {
    std::string out;
    std::cout << "Content-type: text/html\r\n\r\n";
    out =
"<!DOCTYPE html>"
"<html lang=\"en\">"
    "<head>"
        "<meta charset=\"UTF-8\" />"
        "<title>NP Project 3 Sample Console</title>"
        "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\" integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\" crossorigin=\"anonymous\" />"
        "<link href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\" rel=\"stylesheet\" />"
        "<link rel=\"icon\" type=\"image/png\" href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\" />"
        "<style>"
            "* {"
                "font-family: 'Source Code Pro', monospace;"
                "font-size: 1rem !important;"
            "}"
            "body {"
                "background-color: #212529;"
            "}"
            "pre {"
                "color: #cccccc;"
            "}"
            "b {"
                "color: #01b468;"
            "}"
            "table {"
                "border: 1px solid white;"
                "table-layout: fixed;"
                "width: 600px;"
            "}"
            "th, td {"
                "border: 1px solid white;"
                "width: 700px;"
                "overflow-x: auto;"
                "vertical-align: top"
            "}"
        "</style>"
    "</head>"
    "<body>";

    for(long unsigned int i = 0; i < connect_number; i += 2) {
        if(server_info[i].hostname == "")
            break;
        int number = 1;
        if(i + 1 < connect_number && server_info[i + 1].hostname != "")
            number = 2;
        print_terminal(out, server_info, i, number);
    }
    out +=
    "</body>"
"</html>"
    ;
    std::cout << out << std::flush;
}
