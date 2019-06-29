#pragma once

#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string>

namespace wxg {

class buffer {
   private:
    const int MAX_READ = 4096;
    const int DEFAULT_SIZE = 128;

    unsigned char *originbuf_;
    unsigned char *buf_;

    int misalign_ = 0;
    int off_ = 0;
    int totallen_ = 0;

   public:
    buffer() {
        totallen_ = DEFAULT_SIZE;
        originbuf_ = new unsigned char[totallen_];
        buf_ = originbuf_;
    }
    ~buffer() { delete originbuf_; }

    unsigned char *get() const { return buf_; }
    int length() const { return off_; }

    void clear() {
        misalign_ = off_ = 0;
        buf_ = originbuf_;
    }

    int read(int fd, int count) {
        if (count < 0 || count > MAX_READ) count = MAX_READ;

        if (__expand(count) == -1) return -1;

        unsigned char *p = buf_ + off_;
        int n = ::read(fd, p, count);
        if (n <= 0) return n;

        off_ += n;

        if (misalign_ + off_ < totallen_) buf_[off_] = '\0';
        return n;
    }

    int read(int fd) { return read(fd, -1); }

    int write(int fd) {
        int n = ::write(fd, buf_, off_);
        if (n == -1 || n == 0) return n;
        __drain(n);

        return n;
    }

    int push(void *data, int length) {
        int need = off_ + misalign_ + length;

        if (totallen_ < need && __expand(length) == -1) return -1;

        std::memcpy(buf_ + off_, data, length);
        off_ += length;

        return 0;
    }

    int push(buffer *input, int length) {
        if (!input) return 0;
        if (length > input->off_ || length < 0) length = input->off_;

        int res = push(input->buf_, length);

        if (res == 0) input->__drain(length);

        return res;
    }

    inline int push(const std::string &s) {
        return push((void *)s.c_str(), s.length());
    }

    int pop(void *data, int length) {
        if (length > off_) length = off_;
        std::memcpy(data, buf_, length);

        if (length > 0) __drain(length);

        return length;
    }

    /**
     * string line end with '\r\n' '\n\r' '\r' '\n'
     */
    std::string pop_line() {
        char *data = (char *)buf_;
        int i;

        for (i = 0; i < off_; i++) {
            if (data[i] == '\r' || data[i] == '\n') break;
        }

        if (i == off_) return ""; /* not found */

        int onemore = 0;
        if (i + 1 < off_)  // check \r\n \n\r
        {
            if ((data[i] == '\r' && data[i + 1] == '\n') ||
                (data[i] == '\n' && data[i + 1] == '\r'))
                onemore++;
        }

        data[i] = '\0';
        data[i + onemore] = '\0';
        std::string line(data);
        __drain(i + onemore + 1);
        return line;
    }

    unsigned char *find(const char *what, int length) const {
        int remain = off_;
        unsigned char *search = buf_, *p;

        while (remain > length && (p = (unsigned char *)std::memchr(
                                       search, *what, remain)) != nullptr) {
            if (std::memcmp(p, what, length) == 0) return p;

            search = p + 1;
            remain = off_ - (int)(search - buf_);
        }

        return nullptr;
    }

    unsigned char *find(const std::string &s) const {
        return find(s.c_str(), s.length());
    }

   private:
    int __expand(int count) {
        int need = misalign_ + off_ + count;

        if (totallen_ >= need) return 0;

        if (misalign_ >= count)
            __align();
        else {
            unsigned char *newbuf;
            int length = totallen_;

            if (length < 256) length = 256;
            while (length < need) length <<= 1;

            if (originbuf_ != buf_) __align();

            if ((newbuf = (unsigned char *)std::realloc(originbuf_, length)) ==
                nullptr) {
                std::cerr << "realloc error";
                return -1;
            }
            originbuf_ = buf_ = newbuf;
            totallen_ = length;
        }
        return 0;
    }

    void __align() {
        std::memmove(originbuf_, buf_, off_);
        buf_ = originbuf_;
        misalign_ = 0;
    }

    void __drain(int length) {
        if (length >= off_) {
            off_ = 0;
            buf_ = originbuf_;
            misalign_ = 0;
        } else {
            buf_ += length;
            misalign_ += length;
            off_ -= length;
        }
        if (misalign_ + off_ < totallen_) buf_[off_] = '\0';
    }
};

}  // namespace wxg
