#pragma once

#include <list>
#include <memory>
#include <string>
#include <unordered_map>

#include <core/epoll.hh>
#include <core/lock.hh>
#include <model/reactor.hh>

#include "http_connection.hh"

using std::pair;

namespace wxg {

class http_multithread_server;
class http_thread {
   private:
    http_multithread_server* server_ = nullptr;
    std::unique_ptr<reactor<epoll>> reactor_ = nullptr;

    int wakeupfd = -1;

    const std::string wakeupmsg = "0x123456";

    // fd -> unique_ptr<http_connection>
    std::unordered_map<int, std::unique_ptr<http_connection>> hashConnections;
    std::queue<std::unique_ptr<http_connection>> emptyConnections;

   public:
    lock_queue<pair<int, pair<string, unsigned short>>> clientQueue;

   public:
    http_thread(http_multithread_server* server);
    ~http_thread() {
        if (wakeupfd > 0) {
            close(wakeupfd);
            wakeupfd = -1;
        }
    }

    inline reactor<epoll>* get_reactor() const { return reactor_.get(); }
    inline http_multithread_server* get_server() const { return server_; }

    void wakeup() { write(wakeupfd, wakeupmsg); }

    inline void loop() { reactor_->loop(); }

    void release_connection(int fd);

   private:
    std::unique_ptr<http_connection> make_connection(int fd,
                                                     const std::string& addr,
                                                     unsigned short port);
};

}  // namespace wxg
