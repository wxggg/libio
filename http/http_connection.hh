#pragma once

#include <core/buffer.hh>
#include <core/connection.hh>
#include <core/epoll.hh>
#include <model/reactor.hh>

#include "request.hh"

#include <queue>
#include <string>

namespace wxg {

enum connection_status_t { CONNECTED = 0, CLOSING, CLOSED };

class http_thread;
class http_connection : public connection {
   public:
    http_thread* thread = nullptr;

    std::queue<std::unique_ptr<request>> requests;

    connection_status_t status = CLOSED;

   public:
    http_connection(http_thread* thread, int _fd, const std::string& _addr,
                    unsigned short _port);
    ~http_connection();

    void setup_new_events();

    reactor<epoll>* get_reactor() const;

    void parse_request();

    void send_reply(http_code_t code, const std::string& reason,
                    const std::string& content);
    void send_reply(http_code_t code, const std::string& reason,
                    buffer* content = nullptr);

    void send_request(request* req);

    void send_chunk_start(http_code_t code, const std::string& reason);

    void send_chunk(wxg::buffer* buf);
    void send_chunk_end();

    void close();

   private:
    void handle_request(request* req);
};

}  // namespace wxg
