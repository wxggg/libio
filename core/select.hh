#pragma once

#include <sys/select.h>

#include <cstring>
#include <iostream>
#include <vector>

using std::cerr;
using std::cout;
using std::endl;

namespace wxg {

class select {
   private:
    fd_set *readset_in = nullptr;
    fd_set *writeset_in = nullptr;
    fd_set *readset_out = nullptr;
    fd_set *writeset_out = nullptr;

    int size = 0;
    int highest_fd = 0;

    std::vector<int> activeFd;

    static const int MAX_SELECT_FD = 1024;

   public:
    static const int RD = 0x1;
    static const int WR = 0x2;
    static const int RDWR = RD | WR;

   public:
    select() {
        int n = ((32 + NFDBITS) / sizeof(NFDBITS)) * sizeof(fd_mask);
        resize(n);
    }
    ~select() {
        delete readset_in;
        delete writeset_in;
        delete readset_out;
        delete writeset_out;
    }

    bool is_readset(int fd) const { return FD_ISSET(fd, readset_in); }
    bool is_writeset(int fd) const { return FD_ISSET(fd, writeset_in); }
    bool is_readable(int fd) const { return FD_ISSET(fd, readset_out); }
    bool is_writeable(int fd) const { return FD_ISSET(fd, writeset_out); }

    const std::vector<int> &get_active_fd() const { return activeFd; }

   public:
    int resize(size_t n) {
        fd_set *newset;
        newset = (fd_set *)realloc(readset_in, n);
        if (!newset) return -1;
        readset_in = newset;

        newset = (fd_set *)realloc(readset_out, n);
        if (!newset) return -1;
        readset_out = newset;

        newset = (fd_set *)realloc(writeset_in, n);
        if (!newset) return -1;
        writeset_in = newset;

        newset = (fd_set *)realloc(writeset_out, n);
        if (!newset) return -1;
        writeset_out = newset;

        std::memset(readset_in + size, 0, n - size);
        std::memset(writeset_in + size, 0, n - size);

        size = n;
        return 0;
    }

    /* type: RD WR RDWR */
    int add(int fd, int type) {
        if (fd > MAX_SELECT_FD) {
            cerr << "select add fd > " << MAX_SELECT_FD << endl;
            return -1;
        }

        if (highest_fd < fd) highest_fd = fd;

        if (fd > size - 1) {  // need resize
            size_t n = size;
            if (n < sizeof(fd_mask)) n = sizeof(fd_mask);

            int need = ((fd + NFDBITS) / NFDBITS) * sizeof(fd_mask);
            while (need < size) need *= 2;

            if (need != size) resize(need);
        }

        if (type & RD) FD_SET(fd, readset_in);
        if (type & WR) FD_SET(fd, writeset_in);

        return 0;
    }

    int remove(int fd, int type) {
        if ((type & RD)) FD_CLR(fd, readset_in);
        if ((type & WR)) FD_CLR(fd, writeset_in);
        return 0;
    }

    int listen(int timeout) {
        std::memcpy(readset_out, readset_in, size);
        std::memcpy(writeset_out, writeset_in, size);

        struct timeval *ptv = nullptr;
        struct timeval tv({0, 0});
        if (timeout >= 0) {
            tv.tv_sec = timeout;
            ptv = &tv;
        }
        std::vector<int>().swap(activeFd);

        int res =
            ::select(highest_fd + 1, readset_out, writeset_out, nullptr, ptv);

        for (int i = 0; i <= highest_fd; i++)
            if (is_readable(i) || is_writeable(i)) activeFd.push_back(i);

        return res;
    }

   private:
    void check() {
        cout << "size=" << size << endl;
        for (int i = 0; i < size; i++) {
            bool r = FD_ISSET(i, readset_in);
            bool w = FD_ISSET(i, writeset_in);
            if (r | w) std::cout << i << ":" << r << "," << w << "\n";
        }
    }
};

}  // namespace wxg
