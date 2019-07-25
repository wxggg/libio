#include <iostream>

#include <core/buffer.hh>
#include <core/epoll.hh>
#include <core/poll.hh>
#include <core/select.hh>
#include <core/socket.hh>

#include <model/reactor.hh>

using namespace std;

void test_read() {
    cout << __func__ << endl;

    auto fdpair = wxg::get_socketpair();

    wxg::write(fdpair.first, "hello world");
    ::shutdown(fdpair.first, SHUT_WR);

    wxg::reactor<wxg::select> re;

    static int called = 0;

    re.set_read_handler(fdpair.second, [&re, fdpair]() {
        wxg::buffer buf;
        int n = buf.read(fdpair.second);

        if (n <= 0) {
            if (n == -1) cerr << "read error" << endl;
            if (n == 0 && called == 1) cout << "ok" << endl;
            re.remove_read_handler(fdpair.second);
        } else
            called++;
    });

    re.loop();

    close(fdpair.first);
    close(fdpair.second);
}

void test_write() {
    cout << __func__ << endl;

    auto fdpair = wxg::get_socketpair();

    wxg::reactor<wxg::poll> re;

    re.set_write_handler(fdpair.first, [&re, fdpair]() {
        int n = wxg::write(fdpair.first, "hello world");
        if (n == -1)
            cerr << "write error" << endl;
        else
            cout << "ok" << endl;

        re.remove_write_handler(fdpair.first);
    });

    re.loop();

    close(fdpair.first);
    close(fdpair.second);
}

void test_multiple_read_write() {
    cout << __func__ << endl;

    static auto fdpair = wxg::get_socketpair();

    wxg::reactor<wxg::epoll> re;

    static size_t roff = 0, woff = 0;
    static char rbuf[4096], wbuf[4096];

    memset(rbuf, 0, sizeof(rbuf));
    for (size_t i = 0; i < sizeof(wbuf); i++) wbuf[i] = 'a' + i % 32;

    re.set_write_handler(fdpair.first, [&re]() {
        if (woff >= sizeof(wbuf)) {
            shutdown(fdpair.first, SHUT_WR);
            re.remove_write_handler(fdpair.first);
            return;
        }

        int n = write(fdpair.first, wbuf + woff, sizeof(wbuf) - woff);
        if (n == -1) cerr << "write error" << endl;
        woff += n;
    });

    re.set_read_handler(fdpair.second, [&re]() {
        int n = read(fdpair.second, rbuf + roff, sizeof(rbuf) - roff);

        if (n <= 0) {
            if (n == -1) cerr << "read error" << endl;
            re.remove_read_handler(fdpair.second);
        } else
            roff += n;
    });

    re.loop();

    if (roff == woff && memcmp(rbuf, wbuf, sizeof(rbuf)) == 0)
        cout << "ok" << endl;
    else
        cout << "fail" << endl;

    close(fdpair.first);
    close(fdpair.second);
}

void test_combined_read_write() {
    cout << __func__ << endl;

    static auto fdpair = wxg::get_socketpair();

    wxg::reactor<wxg::epoll> re;

    static size_t nr1 = 0, nr2 = 0, nw1 = 4096, nw2 = 8192;

    auto writecb = [&re](int fd, size_t &nw) {
        if (nw == 0) {
            shutdown(fd, SHUT_WR);
            re.remove_write_handler(fd);
            return;
        }

        char buf[2048];
        size_t len = sizeof(buf);
        if (nw < len) len = nw;

        int n = write(fd, buf, len);
        if (n == -1)
            cerr << "write error" << endl;
        else
            nw -= len;
    };

    auto readcb = [&re](int fd, size_t &nr) {
        char buf[2048];
        int n = read(fd, buf, sizeof(buf));

        if (n <= 0) {
            if (n == -1) cerr << "read error" << endl;
            re.remove_read_handler(fd);
        } else
            nr += n;
    };

    re.set_write_handler(fdpair.first, writecb, fdpair.first, nw1);
    re.set_read_handler(fdpair.first, readcb, fdpair.first, nr1);
    re.set_write_handler(fdpair.second, writecb, fdpair.second, nw2);
    re.set_read_handler(fdpair.second, readcb, fdpair.second, nr2);

    re.loop();

    if (nr1 == 8192 && nr2 == 4096)
        cout << "ok" << endl;
    else
        cout << "fail" << endl;

    close(fdpair.first);
    close(fdpair.second);
}

void test_buffer_read_write() {
    cout << __func__ << endl;

    static auto fdpair = wxg::get_socketpair();

    char buf[8333];
    for (size_t i = 0; i < sizeof(buf); i++) buf[i] = 'a' + i % 32;

    wxg::buffer buf1, buf2;
    buf1.push(buf, sizeof(buf));

    wxg::reactor<wxg::epoll> re;

    re.set_write_handler(fdpair.first, [&re, &buf1]() {
        if (buf1.empty()) {
            shutdown(fdpair.first, SHUT_WR);
            re.remove_write_handler(fdpair.first);
            return;
        }

        int n = buf1.write(fdpair.first);
        if (n == -1) cerr << "write error" << endl;
    });

    re.set_read_handler(fdpair.second, [&re, &buf2]() {
        int n = buf2.read(fdpair.second);

        if (n <= 0) {
            if (n == -1) cerr << "read error" << endl;
            re.remove_read_handler(fdpair.second);
        }
    });

    re.loop();

    if (buf2.length() == 8333)
        cout << "ok" << endl;
    else
        cout << "fail" << endl;

    close(fdpair.first);
    close(fdpair.second);
}

void test_buffer_combined_read_write() {
    cout << __func__ << endl;

    static auto fdpair = wxg::get_socketpair();

    size_t n1 = 8192, n2 = 4096;
    char buf1[n1], buf2[n2];

    wxg::buffer writebuf1, writebuf2;
    writebuf1.push(buf1, sizeof(buf1));
    writebuf2.push(buf2, sizeof(buf2));

    wxg::buffer readbuf1, readbuf2;

    wxg::reactor<wxg::epoll> re;

    auto readcb = [&re](int fd, wxg::buffer &buffer) {
        int n = buffer.read(fd);

        if (n <= 0) {
            if (n == -1) cerr << "read error" << endl;
            re.remove_read_handler(fd);
        }
    };

    auto writecb = [&re](int fd, wxg::buffer &buffer) {
        if (buffer.empty()) {
            shutdown(fd, SHUT_WR);
            re.remove_write_handler(fd);
            return;
        }

        int n = buffer.write(fd);
        if (n == -1) cerr << "write error" << endl;
    };

    re.set_write_handler(fdpair.first, writecb, fdpair.first, writebuf1);
    re.set_read_handler(fdpair.first, readcb, fdpair.first, readbuf1);

    re.set_write_handler(fdpair.second, writecb, fdpair.second, writebuf2);
    re.set_read_handler(fdpair.second, readcb, fdpair.second, readbuf2);

    re.loop();

    if (readbuf1.length() == n2 && readbuf2.length() == n1)
        cout << "ok" << endl;
    else
        cout << "fail" << endl;

    close(fdpair.first);
    close(fdpair.second);
}

int main(int argc, char const *argv[]) {
    test_read();

    test_write();

    test_multiple_read_write();

    test_combined_read_write();

    test_buffer_read_write();

    test_buffer_combined_read_write();

    return 0;
}
