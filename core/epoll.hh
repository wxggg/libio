#pragma once

#include <sys/epoll.h>
#include <sys/resource.h>

#include <iostream>
#include <unordered_map>
#include <vector>

namespace wxg {

class epoll {
   private:
    int epfd;
    std::unordered_map<int, int> events;  // read/write events set

    struct epoll_event *epevents;
    int size;
    int res;
    std::unordered_map<int, int> result;
    std::vector<int> activeFd;

   public:
    static const int RD = 0x1;
    static const int WR = 0x2;
    static const int RDWR = RD | WR;

   public:
    epoll() {
        struct rlimit rl;
        if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY)
            size = rl.rlim_cur;

        if ((epfd = epoll_create(1)) == -1) std::cerr << "epoll_create error\n";

        epevents = new struct epoll_event[size];
    }
    ~epoll() { delete[] epevents; }

    bool is_readset(int fd) const {
        return events.count(fd) > 0 && events.at(fd) & RD;
    }
    bool is_writeset(int fd) const {
        return events.count(fd) > 0 && events.at(fd) & WR;
    }
    bool is_readable(int fd) const {
        return result.count(fd) > 0 && result.at(fd) & RD;
    }
    bool is_writeable(int fd) const {
        return result.count(fd) > 0 && result.at(fd) & WR;
    }

    const std::vector<int> &get_active_fd() const { return activeFd; }

    int add(int fd, int type) {
        if (fd < 0 || type <= 0) return -1;
        int old = events[fd];
        if (old == RDWR || old == type) return 1;
        int op = EPOLL_CTL_ADD;
        if (old > 0) op = EPOLL_CTL_MOD;

        int event = events[fd] | type;

        struct epoll_event epev = {0, {0}};
        epev.data.fd = fd;
        if (event & RD) epev.events |= EPOLLIN;
        if (event & WR) epev.events |= EPOLLOUT;

        if (epoll_ctl(epfd, op, fd, &epev) == -1) {
            std::cerr << "epoll_ctl error:";
            std::perror("");
            return -1;
        }
        events[fd] = event & RDWR;
        return 0;
    }

    int remove(int fd, int type) {
        if (fd < 0 || type <= 0 || events.count(fd) == 0) return -1;
        int old = events[fd];
        if (!(type & old)) return 1;

        type &= RDWR;
        int op = EPOLL_CTL_DEL;
        int event = old;
        if (old == RDWR && type != RDWR) {
            event = (type == RD ? WR : RD);
            op = EPOLL_CTL_MOD;
        }

        struct epoll_event epev = {0, {0}};
        epev.data.fd = fd;
        if (event & RD) epev.events |= EPOLLIN;
        if (event & WR) epev.events |= EPOLLOUT;

        if (epoll_ctl(epfd, op, fd, &epev) == -1) {
            std::cerr << "epoll_ctl error:";
            std::perror("");
            return -1;
        }

        if (op == EPOLL_CTL_MOD)
            events[fd] = event;
        else
            events.erase(fd);

        return 0;
    }

    int listen(int timeout) {
        res = epoll_wait(epfd, epevents, size, timeout);
        if (res == -1) return -1;

        std::unordered_map<int, int>().swap(result);
        std::vector<int>().swap(activeFd);

        int event, what, fd;
        for (int i = 0; i < res; i++) {
            what = epevents[i].events;
            fd = epevents[i].data.fd;
            event = 0;
            if (what & (EPOLLHUP | EPOLLERR)) what |= (EPOLLIN | EPOLLOUT);
            if (what & EPOLLIN) event |= RD;
            if (what & EPOLLOUT) event |= WR;
            result[fd] = event;
            activeFd.push_back(fd);
        }
        return res;
    }
};

}  // namespace wxg
