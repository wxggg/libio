#include "http_thread.hh"
#include "http_multithread_server.hh"

#include <iostream>
#include <string>

using std::cout;
using std::pair;
using std::string;

namespace wxg {

http_thread::http_thread(http_multithread_server* server) : server_(server) {
    reactor_ = std::make_unique<reactor<epoll>>();
    wakeupfd = create_eventfd();

    if (!server_ || !reactor_) {
        cerr << __func__ << ": error nullptr pointer" << endl;
        exit(-1);
    }

    reactor_->set_read_handler(wakeupfd, [this]() {
        if (clientQueue.empty()) return;
        char ch[8];
        ::read(wakeupfd, ch, sizeof(ch));

        pair<int, pair<string, unsigned short>> cinfo;
        while (clientQueue.pop(cinfo)) {
            auto conn = make_connection(cinfo.first, cinfo.second.first,
                                        cinfo.second.second);
            get_reactor()->add_read(conn->fd);
            hashConnections[cinfo.first] = std::move(conn);
        }
    });
}

void http_thread::release_connection(int fd) {
    auto it = hashConnections.find(fd);
    if (it != hashConnections.end()) {
        emptyConnections.push(std::move(it->second));
        hashConnections.erase(it);
    }
}

std::unique_ptr<http_connection> http_thread::make_connection(
    int fd, const std::string& addr, unsigned short port) {
    if (emptyConnections.empty())
        return std::make_unique<http_connection>(this, fd, addr, port);

    auto conn = std::move(emptyConnections.front());
    emptyConnections.pop();

    conn->thread = this;
    conn->fd = fd;
    conn->address = addr;
    conn->port = port;
    conn->status = CONNECTED;
    conn->setup_new_events();
    return conn;
}

}  // namespace wxg
