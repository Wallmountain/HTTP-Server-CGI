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
    {'&', "&amp;"},
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

void output_shell(std::string &out, int session, std::string output);
void output_command(std::string &out, int session, std::string output);
void print_terminal(std::string &output, std::vector<struct Con_Info> server_info, int body, int number);
void get_html_label(std::string &out, std::vector<struct Con_Info> server_info);
void html_escape(std::string &output);
void output_panel(std::string &out);

class np_session
  : public std::enable_shared_from_this<np_session> {
public:
  np_session(tcp::socket &h_socket, boost::asio::io_context &io_context, struct Con_Info con_info, int id)
    : http_sockfd(h_socket),
      socket_(io_context),
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
        int filefd = open(file_name.c_str(), ES_READONLY);
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
                } else {
                    std::cerr << "resolve error " << body << std::endl;
                    return;
                }
            });
    }


    void do_connect() {
        auto self(shared_from_this());
        boost::asio::async_connect(socket_, end_point,
            [this, self](boost::system::error_code ec, tcp::endpoint) {
                if (!ec) {
                    do_read();
                } else {
                    std::cerr << "connect error " << body << std::endl;
                    return;
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
                    output_shell(html_msg, body, data);

                    if (data.find("%") != std::string::npos) {
                        do_write();
                    } else {
                        do_send_html();
                        do_read();
                    }
                }  else {
                    std::cerr << "read error " << body << std::endl;
                    return;
                }
            });
    }

    void do_write() {
        auto self(shared_from_this());
        if (cmd_count != cmds.size()) {
            cmd = cmds[cmd_count++];
            output_command(html_msg, body, cmd);
            do_send_html();
            boost::asio::async_write(socket_, boost::asio::buffer(cmd.c_str(), cmd.length()),
                [this, self](boost::system::error_code ec, std::size_t length) {
                    if (!ec) {
                        if(cmd.find("exit") != std::string::npos) {
                            socket_.close();
                            return;
                        } else 
                            do_read();
                    }  else {
                        std::cerr << "write error " << body << std::endl;
                    }
                });
        }
    }

    void do_send_html() {
        auto self(shared_from_this());
        boost::asio::async_write(http_sockfd, boost::asio::buffer(html_msg.c_str(), html_msg.length()),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec) {
                }  else {
                    std::cerr << "send_html error" << body << std::endl;
                }
            });
    }

    tcp::socket &http_sockfd;
    tcp::socket socket_;
    tcp::resolver resolver_;
    tcp::resolver::results_type end_point;
    enum {max_length = 32768};
    char data_[max_length];
    std::string html_msg;
    struct Con_Info info;
    int body;
    long unsigned int cmd_count;
    std::vector<std::string> cmds;
    std::string cmd;
};

class session
  : public std::enable_shared_from_this<session> {
public:
  session(tcp::socket socket)
    : socket_(std::move(socket)) {
  }

  ~session() {
  }

  void start() {
    do_read();
  }

private:
  void do_read()
  {
    auto self(shared_from_this());
    socket_.async_read_some(boost::asio::buffer(data_, max_length),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                data_[strlen(data_)] = '\0';
                std::string header(data_);
                do_parse(header);
                if(REQUEST_URI.size() != 0) {
                    if(REQUEST_URI.find("panel") != std::string::npos) 
                        do_write_panel();
                    else if(REQUEST_URI.find("console") != std::string::npos)
                        do_write();
                }
            }
        });
  }

  void do_write_panel() {
    output_panel(html_msg);
    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(html_msg.c_str(), html_msg.length()),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
            }
        });   
  }

  void do_write() {
    query_info = Parse_Query();
    get_html_label(html_msg, query_info);
    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(html_msg.c_str(), html_msg.length()),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                for(int i = 0; i < connect_number; ++i) {
                    if(query_info[i].hostname == "")
                        break;
                    std::make_shared<np_session>(socket_, io_context, query_info[i], i)->start();
                }
                io_context.run();
                socket_.close();
                do_read();
	        }
        });
  }

  void do_parse(std::string header) {
    std::stringstream read_str(header);
    std::string line;
    std::getline(read_str, line);

    std::stringstream parse_str(line);
    std::string token;

    std::getline(parse_str, token, ' ');
    REQUEST_METHOD = token;

    std::getline(parse_str, token, ' ');
    std::size_t found = token.find(".cgi");
    if (found != std::string::npos)
        REQUEST_URI = "." + token.substr(0, found) + ".cgi";
    else
        REQUEST_URI = "";

    found = token.find("?");
    if (found == std::string::npos) {
        QUERY_STRING = "";
    } else {
        QUERY_STRING = token.substr(found + 1);
    }

    std::getline(parse_str, token, ' ');
    SERVER_PROTOCOL = token;

    std::getline(read_str, line);
    HTTP_HOST = line.c_str() + 6;

    SERVER_ADDR = socket_.local_endpoint().address().to_string();
    SERVER_PORT = std::to_string(socket_.local_endpoint().port());
    REMOTE_ADDR = socket_.remote_endpoint().address().to_string();
    REMOTE_PORT = std::to_string(socket_.remote_endpoint().port());
  }

  std::vector<struct Con_Info> Parse_Query() {
    std::vector<struct Con_Info> res;

    std::stringstream read_query(QUERY_STRING);
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
  
  tcp::socket socket_;
  enum {max_length = 1024};
  char data_[max_length];
  std::string html_msg = "HTTP/1.1 200 OK\r\n";
  std::string REQUEST_METHOD, REQUEST_URI, QUERY_STRING, SERVER_PROTOCOL,
              HTTP_HOST, SERVER_ADDR, SERVER_PORT, REMOTE_ADDR, REMOTE_PORT;
  std::vector<struct Con_Info> query_info;
  boost::asio::io_context io_context;
};

class server {
public:
  server(boost::asio::io_context& io_context_in, short port)
    :acceptor_(io_context_in, tcp::endpoint(tcp::v4(), port)) {
        do_accept();
  }

private:
  void do_accept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
	    if (!ec) {
            std::make_shared<session>(std::move(socket))->start();
            do_accept();
        }
    });
  }
  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) {
  try {
    if (argc != 2) {
      std::cerr << "Usage: async_tcp_echo_server <port>\n";
      return 1;
    }

    boost::asio::io_context io_context;

    server s(io_context, std::atoi(argv[1]));

    io_context.run();
  } catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << "\n" << std::flush;
  }
  return 0;
}

void output_shell(std::string &out, int session, std::string output) {
    html_escape(output);
    out = "<script>document.getElementById('s";
    out += std::to_string(session);
    out += "').innerHTML += '";
    out += output;
    out += "';</script>";
}

void output_command(std::string &out, int session, std::string output) {
    html_escape(output);
    out += "<script>document.getElementById('s";
    out += std::to_string(session);
    out += "').innerHTML += '<b>";
    out += output;
    out += "</b>';</script>";
}

void html_escape(std::string &output) {
    std::string temp = output;
    output = "";
    for(long unsigned int i = 0; i < temp.length(); ++i) {
        bool check = false;
        for (auto escape_pair : escape_dic) {
            if(temp[i] == escape_pair.first) {
                output += escape_pair.second;
                check = true;
                break;
            }
        }
        if(!check)
            output += temp[i];
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

void get_html_label(std::string &out, std::vector<struct Con_Info> server_info) {
    out += "Content-type: text/html\r\n\r\n";
    out +=
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
}

void output_panel(std::string &out) {
  out += "Content-type: text/html\r\n\r\n";
  out += "<!DOCTYPE html>"
         "<html lang=\"en\">"
         "<head>"
         "<title>NP Project 3 Panel</title>"

         "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\" integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\" crossorigin=\"anonymous\" />"
         "<link href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\" rel=\"stylesheet\" />"
         "<link rel=\"icon\" type=\"image/png\" href=\"https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png\" />"
         
         "<style>"
         "* {"
         "font-family: 'Source Code Pro', monospace;"
         "}"
         "</style>"
         "</head>"
         "<body class=\"bg-secondary pt-5\">"
         "<form action=\"console.cgi\" method=\"GET\">"
         "<table class=\"table mx-auto bg-light\" style=\"width: inherit\">"
         "<thead class=\"thead-dark\">"
         "<tr>"
         "<th scope=\"col\">#</th>"
         "<th scope=\"col\">Host</th>"
         "<th scope=\"col\">Port</th>"
         "<th scope=\"col\">Input File</th>"
         "</tr>"
         "</thead>"
         "<tbody>";
   for(int i = 0; i < 5; ++i) {
        out += "<tr>";
        out += "<th scope=\"row\" class=\"align-middle\">Session ";
        out += std::to_string(i + 1); 
        out += "</th>";
        out += "<td>";
        out += "<div class=\"input-group\">";
        out += "<select name=\"h";
        out += std::to_string(i);
        out += "\" class=\"custom-select\">";
        out += "<option></option>";
        for(int j = 0; j < 12; ++j) {
            out += "<option value=\"nplinux";
            out += std::to_string(j + 1);
            out += ".cs.nctu.edu.tw\">nplinux";
            out += std::to_string(j + 1);
            out += "</option>";
        }

        out +=  "</select>"
                "<div class=\"input-group-append\">"
                "<span class=\"input-group-text\">.cs.nctu.edu.tw</span>"
                "</div>"
                "</div>"
                "</td>"
                "<td>"
                "<input name=\"p";
        out += std::to_string(i);
        out +=  "\" type=\"text\" class=\"form-control\" size=\"5\" />"
                "</td>"
                "<td>"
                "<select name=\"f";
        out += std::to_string(i);
        out +=  "\" class=\"custom-select\">"
                "<option></option>";
        for(int j = 0; j < 5; ++j) {
            out += "<option value=\"t";
            out += std::to_string(j + 1);
            out += ".txt\">t";
            out += std::to_string(j + 1);
            out += ".txt</option>";
        }
   }

    out += "</select>"
            "</td>"
            "</tr>"
            "<tr>"
            "<td colspan=\"3\"></td>"
            "<td>"
            "<button type=\"submit\" class=\"btn btn-info btn-block\">Run</button>"
            "</td>"
            "</tr>"
            "</tbody>"
            "</table>"
            "</form>"
            "</body>"
            "</html>";
}
