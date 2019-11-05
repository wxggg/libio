#include <iostream>
#include <map>
#include <string>

#include <core/file.hh>
#include <http/http_multithread_server.hh>

using namespace std;

map<string, string> types = {{"html", "text/html"},
                             {"css", "text/css"},
                             {"xml", "text/xml"},
                             {"png", "png"},
                             {"js", "application/javascript"}};

void send_file(wxg::http_connection *conn, string path) {
    wxg::buffer buf;
    string content = wxg::read_file(path);

    if (content.length() == 0) {
        conn->send_reply(wxg::HTTP_NOTFOUND, "404 not found");
        return;
    }

    wxg::request r;
    buf.push(content);
    r.set_response(wxg::HTTP_OK, "OK", &buf);
    string type = "text/html";

    auto vec = wxg::split(path, '.');
    if (vec.size() > 1 && types.count(vec.back()) > 0) type = types[vec.back()];

    r.set_header("Content-Type", type + "; charset=utf-8");
    if (vec[1] == "js") r.set_header("Content-Type", type);
    r.set_header("Accept-Ranges", "bytes");

    conn->send_request(&r);
}

int main(int argc, char const *argv[]) {
    string home = "/run/media/wxg/Data/wxggg.github.io/";

    wxg::http_multithread_server server;
    server.resize(4);

    server.set_request_handler(
        "/", [&](wxg::request *req, wxg::http_connection *conn) {
            send_file(conn, home + "/index.html");
        });

    server.set_general_handler(
        [&](wxg::request *req, wxg::http_connection *conn) {
            send_file(conn, home + req->uri);
        });

    server.start("127.0.0.1", 80);

    return 0;
}
