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
        char ch[32];
        ::read(wakeupfd, ch, sizeof(ch));
        if (ch[0] != '0' || ch[1] != 'x')
            cout << "warning eventfd receive wrong msg" << endl;

        pair<int, pair<string, unsigned short>> cinfo;
        while (this->server_->clientQueue.pop(cinfo)) {
            auto conn = std::make_unique<http_connection>(
                this, cinfo.first, cinfo.second.first, cinfo.second.second);

            conn->parse_request();

            this->connections.push_back(std::move(conn));
        }
    });
}

}  // namespace wxg
