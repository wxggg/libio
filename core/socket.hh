#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string>

using std::cerr;
using std::endl;

namespace wxg {

inline int write(int fd, const std::string &str) {
    return ::write(fd, str.c_str(), str.length());
}

inline int create_eventfd() {
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) {
        cerr << __func__ << " error eventfd\n";
        abort();
    }
    return fd;
}

inline int set_nonblock(int fd) {
    if (fd < 0) {
        cerr << __func__ << " error fd < 0" << endl;
        return -1;
    }

    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
        cerr << __func__ << " error fcntl" << endl;
        return -1;
    }

    if (fcntl(fd, F_SETFD, 1) == -1) {
        cerr << __func__ << " error fcntl" << endl;
        return -1;
    }

    return 0;
}

inline std::pair<int, int> get_socketpair() {
    int fdpair[2];
    fdpair[0] = fdpair[1] = -1;
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fdpair) == -1)
        cerr << "socketpair error" << endl;

    return std::make_pair(fdpair[0], fdpair[1]);
}

inline int check_socket(int fd) {
    int error = 0;
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error,
                     (socklen_t *)sizeof(error)) == -1) {
        cerr << "getsockopt error with errno=" << errno << endl;
        return -1;
    }

    if (error) {
        cerr << "check socket error with " << strerror(error) << endl;
        return -1;
    }
    return 0;
}

class tcp {
   public:
    static int bind(int fd, const std::string &address, unsigned short port) {
        if (address.empty() || port == 0) return -1;

        struct addrinfo *aitop = getaddrinfo(address, port);

        if (!aitop) return -1;

        int on = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)) < 0 ||
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
            cerr << "error setsockopt\n";
            freeaddrinfo(aitop);
            return -1;
        }

        if (::bind(fd, aitop->ai_addr, aitop->ai_addrlen) == -1) {
            cerr << "bind error" << endl;
            freeaddrinfo(aitop);
            return -1;
        }

        freeaddrinfo(aitop);
        return 0;
    }

    static int bind_ipv4(int fd, const std::string &address,
                         unsigned short port) {
        if (fd == -1) {
            cerr << "error fd == -1" << endl;
            exit(-1);
        }

        int on = 1;

        if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)) < 0 ||
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
            cerr << "error setsockopt\n";
            return -1;
        }

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        // addr.sin_addr.s_addr = htonl(INADDR_ANY);
        inet_pton(AF_INET, address.c_str(), &addr.sin_addr);
        addr.sin_port = htons(port);

        if (::bind(fd, (const sockaddr *)&addr, sizeof(addr)) < 0) {
            cerr << "bind error" << endl;
            return -1;
        }

        return 0;
    }

    static int listen(int fd) {
        if (fd == -1) {
            cerr << "error fd == -1" << endl;
            exit(-1);
        }
        if (::listen(fd, 128) < 0) return -1;
        return 0;
    }

    static int accept(int fd) {
        if (fd == -1) {
            cerr << "error fd == -1" << endl;
            exit(-1);
        }
        sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);
        int connfd = ::accept(fd, (sockaddr *)&cliaddr, &clilen);
        if (connfd < 0) {
            cerr << "accept error" << endl;
            return -1;
        }

        return connfd;
    }

    static int accept(int fd, std::string &host, unsigned short &port) {
        struct sockaddr_storage ss_client;
        socklen_t client_addrlen = sizeof(ss_client);
        int sockfd =
            ::accept(fd, (struct sockaddr *)&ss_client, &client_addrlen);

        if (sockfd == -1) {
            if (errno != EAGAIN && errno != EINTR)
                cerr << "accept error with errno=" << errno << endl;
            return -1;
        }

        char ntop[NI_MAXHOST], strport[NI_MAXSERV];
        int ni_result = ::getnameinfo(
            (sockaddr *)&ss_client, client_addrlen, ntop, sizeof(ntop), strport,
            sizeof(strport), NI_NUMERICHOST | NI_NUMERICSERV);

        if (ni_result != 0) {
            cerr << "genameinfo error " << gai_strerror(ni_result) << endl;
            return -1;
        }

        host = std::string(ntop);
        port = std::stoi(strport);
        return sockfd;
    }

    static int connect(const std::string &address, unsigned short port) {
        int fd = get_socket();
        if (connect(fd, address, port) != -1) return fd;

        close(fd);
        return -1;
    }

    static int connect(int fd, const std::string &address,
                       unsigned short port) {
        struct addrinfo *aitop = getaddrinfo(address, port);

        if (!aitop) return -1;

        if (::connect(fd, aitop->ai_addr, aitop->ai_addrlen) == -1) {
            cerr << "connect error with errno=" << errno << endl;
            ::freeaddrinfo(aitop);
            return -1;
        }

        freeaddrinfo(aitop);
        return 0;
    }

    static int connect_ipv4(int fd, const std::string &address,
                            unsigned short port) {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, address.c_str(), &addr.sin_addr);

        if (::connect(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
            cerr << "connect error" << endl;
            return -1;
        }
        return 0;
    }

    static int get_socket() {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            cerr << "socket error" << endl;
            return -1;
        }
        return fd;
    }

    static int get_nonblock_socket() {
        int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            cerr << "socket error" << endl;
            return -1;
        }
        return fd;
    }

   private:
    static struct addrinfo *getaddrinfo(const std::string &address,
                                        unsigned short port) {
        struct addrinfo *aitop, hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;  // turn nullptr host name into INADDR_ANY

        std::string strport = std::to_string(port);
        int ai_result =
            ::getaddrinfo(address.c_str(), strport.c_str(), &hints, &aitop);

        if (ai_result != 0) {
            cerr << "getaddrinfo error with " << gai_strerror(ai_result);
            return nullptr;
        }
        return aitop;
    }
};

}  // namespace wxg
