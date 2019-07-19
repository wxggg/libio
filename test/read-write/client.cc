#include <iostream>
#include <string>

#include <core/buffer.hh>
#include <core/socket.hh>

using namespace std;

int main(int argc, char const *argv[]) {
    int fd = wxg::tcp::get_socket();

    wxg::tcp::connect(fd, "127.0.0.1", 8081);

    string str;
    wxg::buffer buf;

    while (cin >> str) {
        wxg::write(fd, str);

        buf.clear();
        buf.read(fd);
        cout << buf.get() << endl;
    }

    return 0;
}
