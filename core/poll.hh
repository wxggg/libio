#pragma once

#include <poll.h>

#include <map>
#include <vector>

namespace wxg {

class poll {
   private:
    std::map<int, struct pollfd *> pollfdMap;
    std::map<int, int> result;

    std::vector<int> activeFd;

   public:
    static const int RD = 0x1;
    static const int WR = 0x2;
    static const int RDWR = RD | WR;

   public:
    poll() {}
    ~poll() {
        for (const auto kv : pollfdMap) delete kv.second;
    }

    bool is_readset(int fd) const {
        struct pollfd *pfd = pollfdMap.at(fd);
        if (pfd) return pfd->events & POLLIN;
        return false;
    }

    bool is_writeset(int fd) const {
        struct pollfd *pfd = pollfdMap.at(fd);
        if (pfd) return pfd->events & POLLOUT;
        return false;
    }

    bool is_readable(int fd) const {
        return result.count(fd) > 0 && result.at(fd) & RD;
    }
    bool is_writeable(int fd) const {
        return result.count(fd) > 0 && result.at(fd) & WR;
    }

    const std::vector<int> &get_active_fd() const { return activeFd; }

   public:
    int add(int fd, int type) {
        struct pollfd *pfd = pollfdMap[fd];
        if (!pfd) {
            pfd = new struct pollfd;
            pfd->fd = fd;
            pfd->events = 0;
            pfd->revents = 0;
            pollfdMap[fd] = pfd;
        }

        if (type & RD) pfd->events |= POLLIN;
        if (type & WR) pfd->events |= POLLOUT;

        return 0;
    }

    int remove(int fd, int type) {
        struct pollfd *pfd = pollfdMap.at(fd);
        if (!pfd) return -1;

        if (type & RD) pfd->events &= ~POLLIN;
        if (type & WR) pfd->events &= ~POLLOUT;

        if (!(pfd->events & POLLIN) && !(pfd->events & POLLOUT)) {
            delete pfd;
            pollfdMap.erase(fd);
        }
        return 0;
    }

    int listen(int timeout) {
        int size = pollfdMap.size();
        struct pollfd fds[size];
        int i = 0;
        for (const auto kv : pollfdMap) fds[i++] = *kv.second;

        int res = ::poll(fds, size, timeout);

        if (res == 0 || res == -1) return res;

        std::map<int, int>().swap(result);
        std::vector<int>().swap(activeFd);

        int what, event, fd;
        for (i = 0; i < size; i++) {
            what = fds[i].revents;
            fd = fds[i].fd;
            event = 0;
            if (what & (POLLHUP | POLLERR)) what |= POLLIN | POLLOUT;
            if (what & POLLIN) event |= RD;
            if (what & POLLOUT) event |= WR;
            if (event) {
                result[fd] = event;
                activeFd.push_back(fd);
            }
        }

        return res;
    }
};

}  // namespace wxg
