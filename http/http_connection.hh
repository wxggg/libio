#pragma once

#include <core/buffer.hh>
#include <core/epoll.hh>
#include <model/reactor.hh>

#include "request.hh"

#include <queue>
#include <string>

namespace wxg {

class http_thread;
class http_connection {
   private:
    http_thread* thread = nullptr;
    int fd = -1;
    std::string address;
    unsigned short port;

    std::queue<std::unique_ptr<request>> requests;

    std::unique_ptr<buffer> in = nullptr;
    std::unique_ptr<buffer> out = nullptr;

    bool closed;

   public:
    http_connection(http_thread* thread, int _fd, const std::string& _addr,
                    unsigned short _port);
    ~http_connection();

    reactor<epoll>* get_reactor() const;
    wxg::buffer* get_read_buffer() const { return in.get(); }
    wxg::buffer* get_write_buffer() const { return out.get(); }

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
