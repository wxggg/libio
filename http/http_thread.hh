#pragma once

#include <list>
#include <memory>
#include <string>

#include <core/epoll.hh>
#include <model/reactor.hh>

#include "http_connection.hh"

namespace wxg {

class http_multithread_server;
class http_thread {
   private:
    http_multithread_server* server_ = nullptr;
    std::unique_ptr<reactor<epoll>> reactor_ = nullptr;

    int wakeupfd = -1;

    const std::string wakeupmsg = "0x123456";

    std::list<std::unique_ptr<http_connection>> connections;

   public:
   public:
    http_thread(http_multithread_server* server);
    ~http_thread() {}

    inline reactor<epoll>* get_reactor() const { return reactor_.get(); }
    inline http_multithread_server* get_server() const { return server_; }

    void wakeup() { write(wakeupfd, wakeupmsg); }

    inline void loop() { reactor_->loop(); }
};

}  // namespace wxg
