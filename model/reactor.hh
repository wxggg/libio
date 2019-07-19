#pragma once

#include <functional>
#include <iostream>
#include <map>
#include <vector>

#include <core/socket.hh>
#include <core/time.hh>

namespace wxg {

using Callback = std::function<void()>;

struct channel {
    int fd;

    Callback readcb = nullptr;
    Callback writecb = nullptr;
    Callback errorcb = nullptr;
};

template <class IoMultiplex>
class reactor {
   private:
    wxg::time *timeManager = nullptr;
    IoMultiplex *io = nullptr;
    std::map<int, channel *> channels;

    std::vector<int> needclean;

    bool terminated = false;

   public:
    reactor() {
        timeManager = new wxg::time;
        io = new IoMultiplex;
    }
    ~reactor() {
        delete timeManager;
        delete io;
    }

    bool empty() const { return channels.empty(); }
    int size() const { return channels.size(); }
    void clear() { channels.clear(); }

    void set_terminated() { terminated = true; }

   public:
    wxg::time *get_time_manager() const { return timeManager; }

    template <typename F, typename... Args>
    int set_timer(int sec, F &&f, Args &&... args) {
        return timeManager->set_timer(sec, f, args...);
    }

    template <typename F, typename... Args>
    void set_read_handler(int fd, F &&f, Args &&... args) {
        if (fd < 0) return;
        if (!channels.count(fd)) {
            channels[fd] = new channel;
            channels[fd]->fd = fd;
        }
        add_read(fd);
        channels[fd]->readcb = [f, &args...]() {
            f(std::forward<decltype(args)>(args)...);
        };
    }

    template <typename F, typename... Args>
    void set_write_handler(int fd, F &&f, Args &&... args) {
        if (fd < 0) return;
        if (!channels.count(fd)) {
            channels[fd] = new channel;
            channels[fd]->fd = fd;
        }
        add_write(fd);
        channels[fd]->writecb = [f, &args...]() {
            f(std::forward<decltype(args)>(args)...);
        };
    }

    template <typename F, typename... Args>
    void set_error_handler(int fd, F &&f, Args &&... args) {
        if (fd < 0) return;
        if (!channels.count(fd)) {
            channels[fd] = new channel;
            channels[fd]->fd = fd;
        }
        channels[fd]->errorcb = [f, &args...]() {
            f(std::forward<Args>(args)...);
        };
    }

    void remove_read_handler(int fd) {
        if (!channels.count(fd)) return;

        auto ch = channels[fd];
        ch->readcb = nullptr;
        if (!ch->writecb) needclean.push_back(fd);

        remove_read(fd);
    }

    void remove_write_handler(int fd) {
        if (!channels.count(fd)) return;

        auto ch = channels[fd];
        ch->writecb = nullptr;
        if (!ch->readcb) needclean.push_back(fd);

        remove_write(fd);
    }

    void add_read(int fd) {
        wxg::set_nonblock(fd);
        io->add(fd, io->RD);
    }
    void add_write(int fd) {
        wxg::set_nonblock(fd);
        io->add(fd, io->WR);
    }
    void remove_read(int fd) { io->remove(fd, io->RD); }
    void remove_write(int fd) { io->remove(fd, io->WR); }

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
                channel *ch = channels[fd];

                if (io->is_readable(fd) && ch->readcb) ch->readcb();
                if (io->is_writeable(fd) && ch->writecb) ch->writecb();
            }

            for (const auto &fd : needclean) {
                if (channels.count(fd) > 0) {
                    auto ch = channels[fd];
                    channels.erase(fd);
                    if (!ch) delete ch;
                }
            }
            std::vector<int>().swap(needclean);

            if (once || terminated) return;
        }
    }
};

}  // namespace wxg