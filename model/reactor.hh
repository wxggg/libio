#pragma once

#include <functional>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

#include <core/socket.hh>
#include <core/time.hh>

namespace wxg {

using Callback = std::function<void()>;

struct channel {
    int fd;

    Callback readcb;
    Callback writecb;
    Callback errorcb;
};

template <class IoMultiplex>
class reactor {
   private:
    std::unique_ptr<wxg::time> timeManager = nullptr;
    std::unique_ptr<IoMultiplex> io = nullptr;
    std::unordered_map<int, std::unique_ptr<channel>> channels;

    std::vector<int> needclean;

    bool terminated = false;

   public:
    reactor() {
        timeManager = std::make_unique<wxg::time>();
        io = std::make_unique<IoMultiplex>();
    }
    ~reactor() {}

    bool empty() const { return channels.empty(); }
    int size() const { return channels.size(); }
    void clear() { channels.clear(); }

    void set_terminated() { terminated = true; }

   public:
    wxg::time *get_time_manager() const { return timeManager.get(); }

    template <typename F, typename... Args>
    int set_timer(int sec, F &&f, Args &&... args) {
        return timeManager->set_timer(sec, f, args...);
    }

    template <typename F, typename... Args>
    void set_read_handler(int fd, F &&f, Args &&... args) {
        init_channel(fd);
        add_read(fd);
        channels[fd]->readcb = [f, &args...]() {
            f(std::forward<decltype(args)>(args)...);
        };
    }

    template <typename F, typename... Args>
    void set_write_handler(int fd, F &&f, Args &&... args) {
        init_channel(fd);
        add_write(fd);
        channels[fd]->writecb = [f, &args...]() {
            f(std::forward<decltype(args)>(args)...);
        };
    }

    template <typename F, typename... Args>
    void set_error_handler(int fd, F &&f, Args &&... args) {
        init_channel(fd);
        channels[fd]->errorcb = [f, &args...]() {
            f(std::forward<Args>(args)...);
        };
    }

    void remove_read_handler(int fd) {
        if (!channels.count(fd)) return;

        Callback null;
        null.swap(channels[fd]->readcb);
        if (!channels[fd]->writecb) needclean.push_back(fd);

        remove_read(fd);
    }

    void remove_write_handler(int fd) {
        if (!channels.count(fd)) return;

        Callback null;
        null.swap(channels[fd]->writecb);
        if (!channels[fd]->readcb) needclean.push_back(fd);

        remove_write(fd);
    }

    void add_read(int fd) { io->add(fd, io->RD); }
    void add_write(int fd) { io->add(fd, io->WR); }
    void remove_read(int fd) { io->remove(fd, io->RD); }
    void remove_write(int fd) { io->remove(fd, io->WR); }

    void erase(int fd) {
        auto it = channels.find(fd);
        if (it != channels.end()) channels.erase(it);
    }

    void loop() { loop(false, false); }

    void loop(bool nonblock, bool once) {
        while (!channels.empty() || !timeManager->empty()) {
            int timeout = -1;
            if (nonblock)
                timeout = 0;
            else if (!timeManager->empty())
                timeout = timeManager->shortest_time();

            int res = io->listen(timeout);

            if (res == -1) {
                std::cerr << "listen error res = -1" << std::endl;
            }

            timeManager->process();

            for (const auto &fd : io->get_active_fd()) {
                if (io->is_readable(fd) && channels[fd]->readcb)
                    channels[fd]->readcb();
                if (io->is_writeable(fd) && channels[fd]->writecb)
                    channels[fd]->writecb();
            }

            for (const auto &fd : needclean) erase(fd);

            std::vector<int>().swap(needclean);

            if (once || terminated) return;
        }
    }

   private:
    void init_channel(int fd) {
        if (fd < 0) {
            cerr << "error init fd < 0" << endl;
            exit(-1);
        }
        if (!channels.count(fd)) {
            channels[fd] = std::make_unique<channel>();
            channels[fd]->fd = fd;
        }
        wxg::set_nonblock(fd);
    }
};

}  // namespace wxg