#include <functional>
#include <iostream>

#include "reactor.hh"

#include <core/buffer.hh>
#include <core/epoll.hh>
#include <core/select.hh>
#include <core/socket.hh>
#include <core/thread.hh>

namespace wxg {

struct thread_info {
    int id = 0;
    int wakeupfd = -1;
    std::unique_ptr<reactor<epoll>> re = nullptr;

    thread_info(int i) : id(i) { re = std::make_unique<reactor<epoll>>(); }
};

struct client_info {
    int fd;
    std::string address;
    unsigned short port;

    client_info(int _fd, const std::string &_addr, unsigned short _port)
        : fd(_fd), address(_addr), port(_port) {}
};

using Handler = std::function<void(int)>;

class thread_loop {
   private:
    std::unique_ptr<thread_pool> pool = nullptr;

    std::unique_ptr<reactor<wxg::select>> re = nullptr;

    std::vector<std::unique_ptr<thread_info>> threads;

    lock_queue<std::unique_ptr<client_info>> clientQueue;

    int size = 2;

    Handler readcb = nullptr;
    Handler writecb = nullptr;
    Handler errorcb = nullptr;

   public:
    thread_loop() {
        pool = std::make_unique<thread_pool>();

        re = std::make_unique<reactor<wxg::select>>();
    }
    ~thread_loop() {}

    void set_read_handler(Handler &&f) { readcb = f; }
    void set_write_handler(Handler &&f) { writecb = f; }
    void set_error_handler(Handler &&f) { errorcb = f; }

    void resize(int n) { size = n; }

    void start(const std::string &address, unsigned short port) {
        init();

        int fd = tcp::get_nonblock_socket();
        tcp::bind(fd, address, port);
        tcp::listen(fd);

        re->set_read_handler(fd, [fd, this]() {
            std::string addr;
            unsigned short port;
            int clientfd = tcp::accept(fd, addr, port);
            if (clientfd <= 0) return;

            std::cout << "client " << addr << ":" << port << std::endl;
            this->clientQueue.push(
                std::make_unique<client_info>(clientfd, addr, port));

            wakeup_random(2);
        });

        re->loop();
    }

   private:
    void wakeup_random(int n) {
        if (n > size) n = size;
        std::string msg = "0x123456";

        for (int i = 0; i < n; i++)
            wxg::write(threads[rand() % size]->wakeupfd, msg);
    }

    void init() {
        pool->resize(size);

        for (int i = 0; i < size; i++) {
            auto thread = std::make_unique<thread_info>(i);
            thread->wakeupfd = wxg::create_eventfd();
            thread->re->set_read_handler(thread->wakeupfd, [this, i]() {
                buffer buf;
                buf.read(threads[i]->wakeupfd);

                std::unique_ptr<client_info> cinfo;
                while (clientQueue.pop(cinfo)) {
                    if (readcb)
                        threads[i]->re->set_read_handler(cinfo->fd, readcb,
                                                         cinfo->fd);
                    if (writecb)
                        threads[i]->re->set_write_handler(cinfo->fd, writecb,
                                                          cinfo->fd);
                    if (errorcb)
                        threads[i]->re->set_error_handler(cinfo->fd, errorcb,
                                                          cinfo->fd);
                }
            });
            threads.push_back(std::move(thread));

            pool->push([this, i]() { threads[i]->re->loop(); });
        }
    }
};

}  // namespace wxg
