#pragma once

#include <core/lock.hh>
#include <core/select.hh>
#include <core/thread.hh>
#include <model/reactor.hh>

#include "http_thread.hh"

#include <functional>
#include <string>

using std::pair;
using std::string;

namespace wxg {

class request;
using RequestHandler = std::function<void(request *, http_connection *)>;

class http_multithread_server {
   private:
    std::unique_ptr<thread_pool> pool_ = nullptr;
    std::unique_ptr<reactor<select>> reactor_ = nullptr;

    std::vector<std::unique_ptr<http_thread>> threads;
    int size = 2;
    int index = 0;

   public:
    /* <fd, <address, port>> */
    // lock_queue<pair<int, pair<string, unsigned short>>> clientQueue;

    std::map<string, RequestHandler> requestHandlers;
    RequestHandler generalHandler;

   public:
    http_multithread_server() {
        pool_ = std::make_unique<thread_pool>();
        reactor_ = std::make_unique<reactor<select>>();
    }
    ~http_multithread_server() {}

    inline void resize(int n) { size = n; }

    inline void set_request_handler(const std::string &uri,
                                    RequestHandler &&handler) {
        requestHandlers[uri] = handler;
    }
    inline void set_general_handler(RequestHandler &&handler) {
        generalHandler = handler;
    }

    void wakeup_random(int n);
    void init();
    void start(const std::string &address, unsigned short port);
};
}  // namespace wxg
