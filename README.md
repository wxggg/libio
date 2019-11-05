# libio: A high performance multithread C++ network library for linux.
Linux平台高性能多线程网络库，封装多种IO组件及IO模型，封装HTTP模块， 能非常方便的编写高性能的webserver。

## 示例

* [击鼓传花性能测试工具 sample/bench.cc](sample/bench.cc)
* [http性能压测工具 sample/mywebbench.cc](sample/mywebbench.cc)
* [高性能webserver，可用于静态博客 sample/webserver.cc](sample/webserver.cc)

## 核心组件 core
* _缓冲区buffer_：支持动态扩展，描述符读写
* _IO多路复用_：封装select、poll及epoll，提供统一接口，为了接口统一，epoll只提供水平触发模式
* _加锁队列和list_：使用互斥锁 std::mutex和std::unique_lock
* _信号signal_：封装信号处理函数，提供变参模板接口以支持用户自定义处理函数
* _时钟管理time_：使用std::set管理timer，底层红黑树，也可使用最小堆
* _线程池_：利用condition_variable的通知等待机制实现线程池，加锁队列实现任务的分发

## IO模型 model
* _reactor模型_：基于类模板封装，方便进行多种IO多路复用方式的切换
* _多线程reactor模型_：使用线程池支持多线程，加锁队列进行任务分发
* _多进程master/worker模型_：仿Nginx模拟多进程reactor模型，master进程处理信号并管理worker，worker接收连接并进行IO
* _proactor模型_：仿Asio模拟proactor模型，在Linux下面的高性能网络编程基于IO复用，而asio提供的是异步接口，所以asio在linux下借助reactor模拟异步模型proactor，这里做个简单模拟。

## HTTP模块 http
* _请求解析_：http/request使用状态机解析请求，支持http1.0/1.1协议，支持数据分块传输
* _连接管理_：http_connection管理连接，支持长短连接（keepalive），能够进行管线化传输处理请求（pipeline），支持优雅关闭连接
* _多线程server_：http_thread管理线程资源，htp_multithread_server管理线程，并处理客户端连接请求accept

## 性能优化

* _资源管理_：使用RAII进行资源管理，基本上全部使用unique_ptr进行资源的管理，没有使用shared_ptr主要树因为shared_ptr没有明确的资源所属，基于引用计数的话只要有引用没释放，资源就泄露了，并且shared_ptr还可能存在循环引用，虽然weak_ptr能够解决这类问题，但是未免麻烦。
* _对象池_：对于频繁创建和销毁的对象使用池化技术进行重用，比如http连接，客户端的频繁连接和关闭会造成连接的反复创建和销毁，影响性能，所以这里将关闭的连接放入连接池中，需要的时候直接从池子中取用即可。
* _锁竞争的优化_：对于锁的竞争只出现在由server保存的客户端连接队列中，主线程需要异步唤醒子线程并将连接分发给子线程处理。最开始设计的时候是由主线程维护一个队列，每个子线程都从这一个队列中取连接处理，这样的缺点就是不仅有主线程和每个子线程之间有竞争，每个子线程之间也会存在竞争。优化后采用由子线程维护自己的队列，而主线程通过roundrobin的方式，将连接分发给每个子线程的队列，这样就竞争就只存在主线程和每个子线程了。
* _连接获取优化_：在从队列中获取连接的时候，一开始采用的是`conn->parse_request()`的方式来进入请求处理状态机，但是其实这里没有任何数据，可以直接添加读事件就够了。这个地方会明显影响性能的最重要的一点就是影响了客户端连接的获取，应该尽快的获取连接并添加读事件，因为并发的时候不知道哪些连接的数据会先到来，所以最好的方式就是先把尽可能快的先把所有的读事件全部注册了。
    ```c++
    while (clientQueue.pop(cinfo)) {
        auto conn = make_connection(cinfo.first, cinfo.second.first,
                                    cinfo.second.second);
        // conn->parse_request();
        get_reactor()->add_read(conn->fd);
        hashConnections[cinfo.first] = std::move(conn);
    }
    ```
* _编译优化_：另一个一开始没有注意到的地方就是编译参数对性能的影响，开启`-O2`优化之后性能会明显提升。

总之，使用智能指针RAII机制管理资源之后，基本没有出现过资源泄露和coredump的情况。经过特定的性能优化之后，webserver的并发性能提高了大约1倍左右。

## 性能压测
* _测试环境_：4.19.56-1-MANJARO 8G Intel(R) Core(TM) i5-8250U CPU @1.60GHz 4核8线程
* _测试工具_：webbench，即bench/mywebbench.cc
* _参数_：分别用1到1000个客户端连接来测试性能，每次压测60s，去除第一次的热身数据，取之后的三次数据做平均，以每分钟处理的请求数为评判标准

分别对libio、Nginx、muduo进行性能测试，libio和muduo分别开启四个工作线程，Nginx开启四个工作进程。

### 短连接性能对比

| 每分钟处理请求数 pages/min | 1 client | 10 clients | 100 clients | 1000 clients |
|:--------------------------:|---------:|-----------:|------------:|-------------:|
|           libio            |   355880 |    2114264 |     2249564 |      2176209 |
|           muduo            |   657923 |    2100738 |     2340454 |      2277368 |
|           nginx            |  1033189 |    3396090 |     3273879 |      3311860 |

### 长连接性能对比

| 每分钟处理请求数 pages/min | 1 client | 10 clients | 100 clients | 1000 clients |
|:--------------------------:|---------:|-----------:|------------:|-------------:|
|           libio            |  1835737 |    5150794 |     5774514 |      5316412 |
|           muduo            |  2602285 |    5858807 |     5578503 |      5379136 |

从上面的数据中可以分析得出，libio和muduo的性能接近，但是在单个client的时候，libio的性能明显要差很多，这里的原因应该在于，单个client的时候，只有一个线程会处理io事件，所以主线程和io线程之间的竞争很明显，但是在高并发的情况下，这种竞争就比较微弱了。

单纯从性能对比的角度来看的话，Nginx的性能无疑是最强的，不过libio的性能也还行，接近muduo的性能。