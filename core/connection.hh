#pragma once

#include <memory>

#include "buffer.hh"

namespace wxg {

class connection {
   private:
    std::unique_ptr<buffer> in = nullptr;
    std::unique_ptr<buffer> out = nullptr;

   public:
    int fd = -1;
    std::string address;
    unsigned short port;

    connection() {
        in = std::make_unique<buffer>();
        out = std::make_unique<buffer>();
    }
    ~connection() {}

    inline wxg::buffer* get_read_buffer() const { return in.get(); }
    inline wxg::buffer* get_write_buffer() const { return out.get(); }

    /**
     * read data from socket to buffer
     */
    inline int read() { return in->read(fd); }

    /**
     * write data to socket from buffer
     */
    inline int write() { return out->write(fd); }

    /**
     * push string to write buffer
     */
    inline void push(const std::string& s) { out->push(s); }

    /**
     * push buffer content to write buffer
     */
    inline void push(buffer* buf) { out->push(buf); }
};

}  // namespace wxg
