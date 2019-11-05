#include "http_multithread_server.hh"
#include "http_thread.hh"

namespace wxg {

void http_multithread_server::wakeup_random(int n) {
    if (n < 0 || n > size) n = size;
    for (int i = 0; i < n; i++) threads[rand() % size]->wakeup();
}

void http_multithread_server::init() {
    pool_->resize(size);

    for (int i = 0; i < size; i++)
        threads.push_back(std::move(std::make_unique<http_thread>(this)));
}

void http_multithread_server::start(const std::string &address,
                                    unsigned short port) {
    init();

    int fd = tcp::get_nonblock_socket();
    tcp::bind(fd, address, port);
    tcp::listen(fd);

    reactor_->set_read_handler(fd, [fd, this]() {
        std::string addr;
        unsigned short port;
        int clientfd = tcp::accept(fd, addr, port);
        if (clientfd <= 0) return;

        threads[index]->clientQueue.push(
            std::make_pair(clientfd, std::make_pair(addr, port)));

        threads[index]->wakeup();

        index = (index + 1) % size;
    });

    for (int i = 0; i < size; i++)
        pool_->push([this, i]() { threads[i]->loop(); });

    cout << "running on " << address << ":" << port << endl;
    reactor_->loop();
}

}  // namespace wxg
