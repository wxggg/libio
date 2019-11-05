#include <iostream>
#include <string>

#include <core/buffer.hh>
#include <core/epoll.hh>
#include <core/socket.hh>
#include <http/request.hh>
#include <model/reactor.hh>

using namespace std;

static const string address = "127.0.0.1";
static const unsigned short port = 8082;

static int buffer_read(wxg::buffer *buf, int fd, wxg::reactor<wxg::epoll> *re) {
    int n = buf->read(fd);

    if (n == -1) {
        cerr << "error read" << endl;
        exit(-1);
    }

    if (n == 0) re->remove_read_handler(fd);
    return n;
}
static int buffer_write(wxg::buffer *buf, int fd,
                        wxg::reactor<wxg::epoll> *re) {
    if (buf->empty()) {
        re->remove_write_handler(fd);
        return 0;
    }

    int n = buf->write(fd);

    if (n <= 0) {
        if (n == -1) cerr << "error write" << endl;
        re->remove_write_handler(fd);
    }
    return n;
}

static void check_test_response(wxg::request *req) {
    string what = "This is funny";

    if (req->response_code != wxg::HTTP_OK) {
        cerr << "fail not http ok" << endl;
        exit(-1);
    }

    if (req->get_header("Content-Type").empty()) {
        cerr << "fail content type not set" << endl;
        exit(-1);
    }

    if (req->get_buffer()->length() != what.length()) {
        cerr << "fail length not equal" << endl;
        exit(-1);
    }

    if (memcmp(req->get_buffer()->get(), what.c_str(), what.length()) != 0) {
        cerr << "fail body not equal" << endl;
        exit(-1);
    }
}

class http_client {
   private:
    int fd = -1;

    std::unique_ptr<wxg::buffer> out = nullptr;
    std::unique_ptr<wxg::buffer> in = nullptr;

    std::unique_ptr<wxg::reactor<wxg::epoll>> reactor_ = nullptr;

   public:
    http_client(const std::string &address, unsigned short port) {
        fd = wxg::tcp::get_socket();
        wxg::tcp::connect(fd, address, port);

        out = std::make_unique<wxg::buffer>();
        in = std::make_unique<wxg::buffer>();

        reactor_ = std::make_unique<wxg::reactor<wxg::epoll>>();

        reactor_->set_read_handler(
            fd, [&]() { buffer_read(in.get(), fd, reactor_.get()); });

        reactor_->set_write_handler(
            fd, [this]() { buffer_write(out.get(), fd, reactor_.get()); });
    }
    ~http_client() { close(fd); }

    wxg::buffer *get_out() const { return out.get(); }
    wxg::buffer *get_in() const { return in.get(); }
    wxg::reactor<wxg::epoll> *get_reactor() const { return reactor_.get(); }

    void send_request(wxg::request *req) {
        req->send_to(out.get());
        run();
    }

    void set_write() {
        if (out->empty()) return;
        reactor_->set_write_handler(
            fd, [this]() { buffer_write(out.get(), fd, reactor_.get()); });
    }

    void set_read(wxg::request *req = nullptr) {
        reactor_->set_read_handler(fd, [=]() {
            int n = buffer_read(in.get(), fd, reactor_.get());
            if (n > 0 && req && req->parse(get_in()) != wxg::NEEDMORE)
                get_reactor()->remove_read_handler(fd);
        });
    }

    void run(wxg::request *req = nullptr) {
        if (req && in->length() > 0 && req->parse(get_in()) != wxg::NEEDMORE)
            return;
        set_write();
        set_read(req);
        reactor_->loop();
    }
};

void http_basic_test(void) {
    cout << __func__ << endl;
    http_client client(address, port);

    wxg::request req;
    req.set_request(wxg::GET, "/test");
    req.set_header("Connection", "close");

    client.send_request(&req);

    wxg::request r;
    r.kind = wxg::RESPONSE;
    if (r.parse(client.get_in()) != wxg::ALLREAD) {
        cerr << "fail parse error" << endl;
        exit(-1);
    }

    check_test_response(&r);
    cout << "ok" << endl;
}

void http_connection_test(bool persistent) {
    cout << __func__ << endl;
    http_client client(address, port);

    wxg::request req;
    req.set_request(wxg::GET, "/test");
    if (!persistent) req.set_header("Connection", "close");

    client.send_request(&req);

    wxg::request r;
    r.kind = wxg::RESPONSE;
    if (r.parse(client.get_in()) != wxg::ALLREAD) {
        cerr << "fail parse error" << endl;
        exit(-1);
    }

    check_test_response(&r);
    cout << "ok" << endl;
}

void http_close_detection(bool withdelay) {
    cout << __func__ << endl;
    http_client client(address, port);

    string uri = "/test";
    if (withdelay) uri = "/largedelay";

    wxg::request req;
    req.set_request(wxg::GET, uri);

    req.send_to(client.get_out());

    wxg::request r;
    r.kind = wxg::RESPONSE;
    client.run(&r);

    // now i deal with one request, but i don't know
    // if the connection is closed, so send another

    client.get_reactor()->set_timer(5, [&]() {
        wxg::request req2;
        req2.set_request(wxg::GET, "/test");
        req2.set_header("Connection", "close");
        req2.send_to(client.get_out());
        client.set_write();
    });

    wxg::request r2;
    r2.kind = wxg::RESPONSE;

    client.run(&r2);

    check_test_response(&r2);
    cout << "ok" << endl;
}

void http_post_test(void) {
    cout << __func__ << endl;
    http_client client(address, port);

    wxg::buffer content;
    content.push("message from client");

    wxg::request req;
    req.set_request(wxg::POST, "/post", &content);
    req.set_header("Connection", "close");

    client.send_request(&req);

    wxg::request r;
    r.kind = wxg::RESPONSE;
    if (r.parse(client.get_in()) != wxg::ALLREAD) {
        cerr << "fail parse error" << endl;
        exit(-1);
    }

    check_test_response(&r);
    cout << "ok" << endl;
}

void http_failure_test(void) {
    cout << __func__ << endl;
    http_client client(address, port);

    client.get_out()->push("illegal request\r\n");

    client.run();

    wxg::request r;
    r.kind = wxg::RESPONSE;
    if (r.parse(client.get_in()) != wxg::ALLREAD) {
        cerr << "fail parse error" << endl;
        exit(-1);
    }

    if (r.response_code != wxg::HTTP_BADREQUEST) {
        cerr << "fail not http_badrequest" << endl;
        exit(-1);
    }

    const string what = "400 Bad Request";
    if (memcmp(r.get_buffer()->get(), what.c_str(), what.length()) != 0) {
        cerr << "fail not 400 bad request" << endl;
        exit(-1);
    }

    cout << "ok" << endl;
}

void http_dispatcher_test(void) {
    cout << __func__ << endl;
    http_client client(address, port);

    wxg::request req;
    req.set_request(wxg::GET, "/dispatch?arg=");
    req.set_header("Connection", "close");

    client.send_request(&req);

    wxg::request r;
    r.kind = wxg::RESPONSE;
    if (r.parse(client.get_in()) != wxg::ALLREAD) {
        cerr << "fail parse error" << endl;
        exit(-1);
    }

    check_test_response(&r);
    cout << "ok" << endl;
}

void http_multiline_header_test(void) {
    cout << __func__ << endl;
    http_client client(address, port);

    const string http_start_request =
        "GET /test HTTP/1.1\r\n"
        "Host: somehost\r\n"
        "Connection: close\r\n"
        "X-Multi:  aaaaaaaa\r\n"
        " a\r\n"
        "\tEND\r\n"
        "X-Last: last\r\n"
        "\r\n";

    client.get_out()->push(http_start_request);

    client.run();

    wxg::request r;
    r.kind = wxg::RESPONSE;
    if (r.parse(client.get_in()) != wxg::ALLREAD) {
        cerr << "fail parse error" << endl;
        exit(-1);
    }

    check_test_response(&r);
    cout << "ok" << endl;
}

void http_negtive_content_length_test() {
    cout << __func__ << endl;
    http_client client(address, port);

    wxg::request req;
    req.set_request(wxg::GET, "/test");
    req.set_header("X-Negative", "makeitso");

    req.send_to(client.get_out());

    wxg::request r;
    r.kind = wxg::RESPONSE;

    client.run(&r);

    cout << "ok" << endl;
}

void http_chunked_test(void) {
    cout << __func__ << endl;
    http_client client(address, port);

    wxg::request req;
    req.set_request(wxg::GET, "/chunked");
    req.set_header("Connection", "close");

    client.send_request(&req);

    wxg::request r;
    r.kind = wxg::RESPONSE;
    if (r.parse(client.get_in()) != wxg::ALLREAD) {
        cerr << "fail parse error" << endl;
        exit(-1);
    }

    cout << "ok" << endl;
}

void http_keepalive_pipeline_test(void) {
    cout << __func__ << endl;
    http_client client(address, port);

    for (int i = 0; i < 20; i++) {
        wxg::request req;
        req.set_request(wxg::GET, "/keep/" + to_string(i));
        // req.set_request(wxg::GET, "/test");
        req.set_header("Connection", "keep-alive");
        req.send_to(client.get_out());
    }

    for (int i = 0; i < 20; i++) {
        wxg::request r;
        r.kind = wxg::RESPONSE;
        client.run(&r);
        if (!r.get_buffer()->find(to_string(i))) {
            cerr << "fail not right request" << endl;
            exit(-1);
        }
    }

    cout << "ok" << endl;
}

int main(int argc, char const *argv[]) {
    for (int i = 0; i < 10; i++) http_basic_test();

    http_connection_test(false);
    // http_connection_test(true);  // keep alive

    // http_close_detection(false);  // need seconds
    // http_close_detection(true);  // need seconds

    http_post_test();

    http_failure_test();

    http_dispatcher_test();

    http_multiline_header_test();

    http_negtive_content_length_test();

    http_chunked_test();

    http_keepalive_pipeline_test();

    return 0;
}
