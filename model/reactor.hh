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

   public:
    wxg::time *get_time_manager() const { return timeManager; }

    template <typename F, typename... Rest>
    void set_read_handler(int fd, F &&f, Rest &&... rest) {
        if (fd < 0) return;
        if (!channels.count(fd)) {
            channels[fd] = new channel;
            channels[fd]->fd = fd;
            io->add(fd, io->RD);
            add_read(fd);
        }
        channels[fd]->readcb = [f, rest...]() { f(rest...); };
    }

    template <typename F, typename... Rest>
    void set_write_handler(int fd, F &&f, Rest &&... rest) {
        if (fd < 0) return;
        if (!channels.count(fd)) {
            channels[fd] = new channel;
            channels[fd]->fd = fd;
            add_write(fd);
        }
        channels[fd]->writecb = [f, rest...]() { f(rest...); };
    }

    template <typename F, typename... Rest>
    void set_error_handler(int fd, F &&f, Rest &&... rest) {
        if (fd < 0) return;
        if (!channels.count(fd)) {
            channels[fd] = new channel;
            channels[fd]->fd = fd;
        }
        channels[fd]->errorcb = [f, rest...]() { f(rest...); };
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
                return;
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

            if (once) return;
        }
    }
};

}  // namespace wxg