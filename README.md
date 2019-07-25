# libio
A  simple io interface for linux network programing, containing multiple IO model.

Linux平台简单IO网络库,封装多种io模型,包括reactor,多线程reactor,多进程reactor(master-worker),proactor模型.

## 特点

内置多种基本组件,包括缓冲区,定时器,多种io多路复用机制,线程池,信号管理

* IO复用：select、poll、epoll
* 线程池
* RAII管理资源
* HTTP: 支持1.0/1.1协议，长短连接，管线化传输请求，优雅关闭连接，数据分块传输
* 使用连接池，避免反复申请内存
* 多种IO模型：多线程reactor、多进程master-worker、proactor

## 压测

内置两种压测工具，bench/bench.cc（击鼓传花读写测试），bench/mywebbench.cc（类webbench并发压测工具）

实测并发性能接近 Nginx 和 muduo 的60%，还需要继续优化

## example

以 echo server为例, 封装多种IO模型

__1. 多线程reactor模型__
```c++
#include <core/epoll.hh>
#include <model/thread_loop.hh>

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
```

__2. 多进程reactor模型 (master-worker工作模式)__

仿nginx实现master/worker模型
```c++
#include <core/epoll.hh>
#include <model/process_loop.hh>

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
```

__3. proactor模型__

仿asio库实现proactor模型,基于reactor模型借助线程池模拟proactor异步io.

```c++
#include <core/epoll.hh>
#include <model/proactor.hh>

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
```