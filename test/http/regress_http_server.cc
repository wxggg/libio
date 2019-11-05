#include <iostream>
#include <string>

#include <http/http_multithread_server.hh>

using namespace std;

int main(int argc, char const *argv[]) {
    const string fine = "Everything is fine";
    const string funny = "This is funny";

    wxg::http_multithread_server server;
    server.resize(4);

    server.set_request_handler(
        "/test", [&](wxg::request *req, wxg::http_connection *conn) {
            wxg::buffer buf;
            buf.push(funny);
            wxg::request r;
            r.set_response(wxg::HTTP_OK, fine, &buf);
            if (!req->get_header("X-Negative").empty())
                r.set_header("Content-Length", "-100");
            conn->send_request(&r);
        });

    server.set_request_handler(
        "/largedelay", [&](wxg::request *req, wxg::http_connection *conn) {
            conn->get_reactor()->set_timer(3, [&]() {
                cout << "timer out" << endl;
                conn->send_reply(wxg::HTTP_OK, fine, funny);
            });
        });

    server.set_request_handler(
        "/post", [&](wxg::request *req, wxg::http_connection *conn) {
            if (req->type != wxg::POST) {
                cerr << "error not post" << endl;
                return;
            }

            string s = "message from client";
            if (memcmp(s.c_str(), req->get_buffer()->get(), s.length()) != 0)
                conn->send_reply(wxg::HTTP_NOCONTENT, "post message error");
            else
                conn->send_reply(wxg::HTTP_OK, fine, funny);
        });

    server.set_request_handler("/dispatch", [&](wxg::request *req,
                                                wxg::http_connection *conn) {
        if (req->query != "arg=")
            conn->send_reply(wxg::HTTP_NOTFOUND, "not found", "404 Not Found");
        else
            conn->send_reply(wxg::HTTP_OK, fine, funny);
    });

    const vector<string> CHUNKS = {"This is funny", "but no hilarious.",
                                   "bwv 1052"};

    server.set_request_handler(
        "/chunked", [&](wxg::request *req, wxg::http_connection *conn) {
            conn->send_chunk_start(wxg::HTTP_OK, fine);
            for (int i = 1; i <= 3; i++) {
                wxg::buffer buf;
                buf.push(CHUNKS[i - 1]);
                conn->send_chunk(&buf);
            }
            conn->send_chunk_end();
        });

    server.set_request_handler(
        "/keep/*", [&](wxg::request *req, wxg::http_connection *conn) {
            conn->send_reply(wxg::HTTP_OK, fine, req->uri + "is alive");
        });

    server.start("127.0.0.1", 8082);

    return 0;
}
