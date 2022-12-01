#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

class session
  : public std::enable_shared_from_this<session> {
public:
  session(tcp::socket socket)
    : socket_(std::move(socket)) {
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
              memset(data_, 0, length);

              do_parse(header);
              if(REQUEST_URI.size())
                do_write();
            }
        });
  }

  void do_write() {
    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(respond_header, strlen(respond_header)),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
              int pid;
              if ((pid = fork()) == -1) {
                std::cerr << "fork: error" << std::endl;
                exit(0);
              }

              if (pid == 0) { // child
                setenv("REQUEST_METHOD", REQUEST_METHOD.c_str(), 1);
                setenv("REQUEST_URI", REQUEST_URI.c_str(), 1);
                setenv("QUERY_STRING", QUERY_STRING.c_str(), 1);
                setenv("SERVER_PROTOCOL", SERVER_PROTOCOL.c_str(), 1);
                setenv("HTTP_HOST", HTTP_HOST.c_str(), 1);
                setenv("SERVER_ADDR", SERVER_ADDR.c_str(), 1);
                setenv("SERVER_PORT", SERVER_PORT.c_str(), 1);
                setenv("REMOTE_ADDR", REMOTE_ADDR.c_str(), 1);
                setenv("REMOTE_PORT", REMOTE_PORT.c_str(), 1);

                int sockfd = socket_.native_handle();
                dup2(sockfd, STDIN_FILENO);
                dup2(sockfd, STDOUT_FILENO);

                socket_.close();
                
                if(execlp(REQUEST_URI.c_str(), REQUEST_URI.c_str()) == -1) {
                  std::cerr << "Unknown command: [" << REQUEST_URI << "]." << std::endl;
                }
                exit(0);
              }
              socket_.close();
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

  tcp::socket socket_;
  enum {max_length = 1024};
  char data_[max_length];
  char respond_header[20] = "HTTP/1.1 200 OK\r\n";
  std::string REQUEST_METHOD, REQUEST_URI, QUERY_STRING, SERVER_PROTOCOL,
              HTTP_HOST, SERVER_ADDR, SERVER_PORT, REMOTE_ADDR, REMOTE_PORT;
};

class server {
public:
  server(boost::asio::io_context& io_context, short port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
    do_accept();
  }

private:
  void do_accept() {
    acceptor_.async_accept(
      [this](boost::system::error_code ec, tcp::socket socket) {
        if (!ec) {
          std::make_shared<session>(std::move(socket))->start();
        }
        do_accept();
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

    signal(SIGCHLD,SIG_IGN);

    boost::asio::io_context io_context;

    server s(io_context, std::atoi(argv[1]));

    io_context.run();
  } catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
