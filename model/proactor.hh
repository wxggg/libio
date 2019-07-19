#pragma once

#include <core/lock.hh>
#include "reactor.hh"

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace wxg {

using Callback = std::function<void()>;
using Lock = std::unique_lock<std::mutex>;

class descriptor_data {
   public:
    std::queue<Callback> write_queue;
    std::queue<Callback> read_queue;
};

template <class IoMultiplex>
class proactor {
   private:
    std::atomic<bool> stopped;
    std::atomic<bool> inloop;

    std::mutex mutex;
    std::condition_variable cv;

    std::unique_ptr<reactor<IoMultiplex>> task_ = nullptr;

    std::map<int, std::unique_ptr<descriptor_data>> ops;

    lock_queue<Callback> op_queue;

   public:
    proactor() : stopped(false), inloop(false) {
        task_ = std::make_unique<reactor<IoMultiplex>>();
    }
    ~proactor() {}

    template <typename F, typename... Args>
    void async_accept(int socket, F &&f, Args &&... args) {
        async_read(socket, f, args...);
    }

    template <typename F, typename... Args>
    void async_write(int socket, F &&f, Args &&... args) {
        cout << "write handler " << socket << endl;
        if (socket < 0) return;

        Lock lock(mutex);

        init_descriptor(socket);

        ops[socket]->write_queue.push([f, args...]() { f(args...); });

        task_->set_write_handler(socket, [this, socket]() {
            if (!ops.count(socket)) return;

            auto &write_queue = ops[socket]->write_queue;
            if (write_queue.empty()) return;

            op_queue.push(std::move(write_queue.front()));
            write_queue.pop();

            if (write_queue.empty()) task_->remove_write_handler(socket);

            if (ops[socket]->read_queue.empty() &&
                ops[socket]->write_queue.empty())
                ops.erase(socket);
        });
    }

    template <typename F, typename... Args>
    void async_read(int socket, F &&f, Args &&... args) {
        if (socket < 0) return;

        Lock lock(mutex);

        init_descriptor(socket);

        ops[socket]->read_queue.push([f, args...]() { f(args...); });

        task_->set_read_handler(socket, [this, socket]() {
            cout << "read handler " << socket << endl;
            if (!ops.count(socket)) return;

            auto &read_queue = ops[socket]->read_queue;
            if (read_queue.empty()) return;

            op_queue.push(std::move(read_queue.front()));
            read_queue.pop();

            if (read_queue.empty()) {
                cout << "remove read handler" << endl;
                task_->remove_read_handler(socket);
            }

            if (ops[socket]->read_queue.empty() &&
                ops[socket]->write_queue.empty()) {
                ops.erase(socket);
                cout << "erase" << endl;
            }
        });
    }

    void run() {
        Callback cb;
        while (!stopped) {
            cout << "enter while" << endl;
            if (!op_queue.empty()) {
                cout << std::this_thread::get_id() << ":op_queue not empty"
                     << endl;
                bool isPop = op_queue.pop(cb);
                if (!op_queue.empty()) {
                    Lock lock(mutex);
                    cv.notify_one();
                }
                if (isPop) cb();
            } else if (!ops.empty() && !inloop) {
                cout << std::this_thread::get_id() << ":ops not empty" << endl;
                Lock lock(mutex);
                inloop = true;
                task_->loop(false, true);
                inloop = false;
            } else {
                cout << std::this_thread::get_id() << ":wait" << endl;
                Lock lock(mutex);
                cv.wait(lock,
                        [this]() { return !op_queue.empty() || stopped; });
            }
        }
    }

   private:
    void init_descriptor(int fd) {
        if (ops.count(fd)) return;

        ops[fd] = std::make_unique<descriptor_data>();
    }
};

}  // namespace wxg
