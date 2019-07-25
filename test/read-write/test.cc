#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fcntl.h>
#include <unistd.h>

#include <iostream>

#include <core/epoll.hh>
#include <core/poll.hh>
#include <core/select.hh>
#include <core/signal.hh>
#include <core/time.hh>
#include <model/proactor.hh>
#include <model/process_loop.hh>
#include <model/reactor.hh>
#include <model/thread_loop.hh>

using namespace std;

void test_signal() {
    wxg::signal::set_handler(SIGINT,
                             []() { cout << "signal callback" << endl; });
    wxg::signal::set_persistent(SIGINT);
    // wxg::signal::unblock();
    // wxg::signal::block();

    sleep(10);
}

void test_time() {
    cout << "test time" << endl;
    wxg::time timeManager;

    int id = timeManager.set_timer(3, 0, []() { cout << "tick" << endl; });
    timeManager.set_persistent(id);

    static int i = 0;
    while (i++ < 5) {
        sleep(3);
        timeManager.process();
        cout << "tok" << endl;
    }
}

void test_read_write() {
    const char *fifo = "/tmp/event.fifo";
    unlink(fifo);
    if (mkfifo(fifo, 0600) == -1) {
        cerr << "mkfifo" << endl;
        exit(-1);
    }

    int fd = open(fifo, O_RDWR | O_NONBLOCK, 0);

    if (fd == -1) {
        cerr << "open error" << endl;
        exit(-1);
    }

    wxg::select io;
    // wxg::poll io;
    // wxg::epoll io;
    io.add(fd, io.RD);

    int flag = true;
    wxg::signal::set_handler(SIGINT, [&flag]() {
        cout << "signal event " << SIGINT << endl;
        flag = false;
    });

    while (flag) {
        cout << "listening..." << endl;
        io.listen(-1);

        if (io.is_readable(fd)) {
            char buf[255];
            int len = read(fd, buf, sizeof(buf) - 1);
            if (len == -1) {
                cerr << "read" << endl;
                return;
            } else if (len == 0) {
                cerr << "connection closed" << endl;
                return;
            }
            buf[len] = '\0';
            cout << buf << endl;
        }
    }
}

void test_reactor() {
    // wxg::reactor<wxg::select> re;
    // wxg::reactor<wxg::poll> re;
    wxg::reactor<wxg::epoll> re;

    const char *test = "test string";
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == -1) return;

    write(pair[0], test, strlen(test) + 1);
    shutdown(pair[0], SHUT_WR);  // generate EOF

    re.set_read_handler(pair[1], [pair, &re]() {
        char buf[256];
        int len = read(pair[1], buf, sizeof(buf));
        if (len > 0) {
            cout << buf << endl;
        } else {
            cout << "EOF" << endl;
            re.remove_read_handler(pair[1]);
        }
    });

    re.loop();
}

void test_weof() {
    wxg::reactor<wxg::poll> re;

    wxg::signal::set_handler(SIGPIPE, []() { cout << "pipe" << endl; });
    wxg::signal::set_persistent(SIGPIPE);

    static int fdpair[2];
    static int called = 0;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fdpair) == -1) return;

    re.set_write_handler(fdpair[1], [&re]() {
        const char *test = "test string";
        int len = write(fdpair[1], test, strlen(test) + 1);

        if (len > 0) {
            close(fdpair[0]);
        } else {
            if (called == 1) cout << "test ok!\n";
            re.remove_write_handler(fdpair[1]);
        }

        called++;
    });

    re.loop();
}

void bench_time() {
    wxg::reactor<wxg::select> re;
    auto timeManager = re.get_time_manager();

    for (int i = 0; i < 2000; i++) {
        int timeout = random() % 20 + i;
        timeManager->set_timer(timeout, 0, [timeout, i]() {
            cout << "timer " << i << " out with time " << timeout << endl;
        });
    }

    re.loop();
}

void test_thread_loop() {
    wxg::thread_loop server;

    server.resize(4);

    server.set_read_handler([](int fd) {
        wxg::buffer buf;
        int res = buf.read(fd);

        if (res > 0) {
            cout << buf.get() << endl;
            buf.write(fd);
        } else {
            close(fd);
        }
    });

    server.start("127.0.0.1", 8081);
}

void test_process_loop() {
    wxg::process_loop server;

    server.resize(4);

    server.set_read_handler([](int fd) {
        wxg::buffer buf;
        int res = buf.read(fd);

        if (res > 0) {
            cout << buf.get() << endl;
            buf.write(fd);
        } else {
            close(fd);
        }
    });

    server.start("127.0.0.1", 8081);
}

class session : public std::enable_shared_from_this<session> {
   private:
    int socket = -1;
    wxg::buffer buf;
    wxg::proactor<wxg::epoll> *io_context;

   public:
    session(int socket, wxg::proactor<wxg::epoll> *io_context)
        : socket(socket), io_context(io_context) {}

    void do_read() {
        auto self(shared_from_this());
        io_context->async_read(socket, [this, self]() {
            int res = buf.read(socket);
            if (res > 0) do_write();
        });
    }

    void do_write() {
        auto self(shared_from_this());
        io_context->async_write(socket, [this, self]() {
            int res = buf.write(socket);
            if (res >= 0) do_read();
        });
    }
};

void do_accept(int socket, wxg::proactor<wxg::epoll> *io_context) {
    io_context->async_accept(socket, [socket, io_context]() {
        int fd = wxg::tcp::accept(socket);
        cout << "socket accept fd=" << fd << endl;

        if (fd > 0) {
            std::make_shared<session>(fd, io_context)->do_read();
        }

        do_accept(socket, io_context);
    });
}

void test_proactor() {
    wxg::proactor<wxg::epoll> io_context;

    int socket = wxg::tcp::get_nonblock_socket();
    wxg::tcp::bind(socket, "127.0.0.1", 8081);
    wxg::tcp::listen(socket);

    do_accept(socket, &io_context);

    std::thread t([&io_context]() { io_context.run(); });

    sleep(10);

    io_context.run();
}

int main(int argc, char const *argv[]) {
    cout << "testing..." << endl;

    // test_signal();

    // test_time();

    // test_read_write();

    test_reactor();

    // test_weof();

    // bench_time();

    // test_thread_loop();

    // test_process_loop();

    // test_proactor();

    return 0;
}
