#include "http_connection.hh"
#include "http_multithread_server.hh"
#include "http_thread.hh"

#include <core/buffer.hh>

namespace wxg {

http_connection::http_connection(http_thread* _thread, int _fd,
                                 const std::string& _addr, unsigned short _port)
    : thread(_thread), fd(_fd), address(_addr), port(_port) {
    closed = false;

    in = std::make_unique<buffer>();
    out = std::make_unique<buffer>();

    if (!thread || fd <= 0) {
        cerr << __func__ << " : error" << endl;
        exit(-1);
    }

    get_reactor()->set_read_handler(fd, [this]() {
        int n = in->read(fd);

        if (n == -1) {
            if (errno != EAGAIN && errno != EINTR) {
                cerr << "fixme: read error" << endl;
                get_reactor()->remove_read(fd);
            }
        } else if (n == 0) {  // EOF
            get_reactor()->remove_read(fd);
        } else {
            parse_request();
        }
    });
    get_reactor()->remove_read(fd);

    get_reactor()->set_write_handler(fd, [this]() {
        if (out->empty()) {
            get_reactor()->remove_write(fd);
            if (closed) close();
            return;
        }

        int n = out->write(fd);
        if (n == -1) {
            if (errno != EAGAIN && errno != EINTR && errno != EINPROGRESS) {
                cerr << "fixme: write error" << endl;
                get_reactor()->remove_write(fd);
            }
        } else if (n == 0) {
            cerr << "error write eof" << endl;
            get_reactor()->remove_write(fd);
        } else {
            cout << "write " << n << " bytes" << endl;
        }
    });
    get_reactor()->remove_write(fd);
}

http_connection::~http_connection() {}

reactor<epoll>* http_connection::get_reactor() const {
    return thread->get_reactor();
}

void http_connection::parse_request() {
    cout << __func__ << endl;
    if (in->empty()) {
        if (!closed) get_reactor()->add_read(fd);
        return;
    }

    bool processing = true;

    while (processing && !in->empty()) {
        if (requests.empty()) {
            auto req = std::make_unique<request>();
            req->kind = REQUEST;
            requests.push(std::move(req));
        }

        processing = false;
        parse_status_t res = requests.front()->parse(in.get());
        switch (res) {
            case ALLREAD:
                handle_request(requests.front().get());
                requests.pop();
                processing = true;
                break;  // now incoming request will be destroyed
            case NEEDMORE:
                get_reactor()->add_read(fd);
                break;
            case CORRUPTED:;
                cerr << "corrupted" << endl;
                shutdown(fd, SHUT_RD);
                get_reactor()->remove_read_handler(fd);
                closed = true;
                send_reply(HTTP_BADREQUEST, "bad request", "400 Bad Request");
                break;
            case CANCELD:
            default:
                cerr << "unknow error" << endl;
                shutdown(fd, SHUT_RD);
                get_reactor()->remove_read_handler(fd);
                break;
        }
        if (!out->empty()) get_reactor()->add_write(fd);
    }
}

void http_connection::send_reply(http_code_t code, const std::string& reason,
                                 const std::string& content) {
    if (content.empty()) return send_reply(code, reason, nullptr);
    wxg::buffer buf;
    buf.push(content);
    send_reply(code, reason, &buf);
}

void http_connection::send_reply(http_code_t code, const std::string& reason,
                                 buffer* content) {
    wxg::request r;
    r.set_response(code, reason, content);
    send_request(&r);
}

void http_connection::send_request(request* req) {
    req->send_to(out.get());
    get_reactor()->add_write(fd);
}

void http_connection::send_chunk_start(http_code_t code,
                                       const std::string& reason) {
    wxg::request r;
    r.set_response(code, reason);
    r.set_header("Transfer-Encoding", "chunked");
    send_request(&r);
}

void http_connection::send_chunk(wxg::buffer* buf) {
    std::stringstream ss;
    ss << std::hex << buf->length();
    out->push(ss.str() + "\r\n");
    out->push(buf);
    out->push("\r\n");
    get_reactor()->add_write(fd);
}

void http_connection::send_chunk_end() {
    out->push("0\r\n\r\n");
    get_reactor()->add_write(fd);
}

void http_connection::close() {
    if (fd > 0) {
        shutdown(fd, SHUT_WR);
        ::close(fd);
        fd = -1;
        get_reactor()->remove_read_handler(fd);
        get_reactor()->remove_write_handler(fd);
        cout << "connection close" << endl;
    }
}

void http_connection::handle_request(request* req) {
    if (!req) return;
    cout << __func__ << " uri=" << req->uri << endl;

    if (req->get_header("Connection") == "close") {
        shutdown(fd, SHUT_RD);
        get_reactor()->remove_read_handler(fd);
        closed = true;
    }

    auto server = thread->get_server();

    if (server->requestHandlers.count(req->uri)) {
        server->requestHandlers.at(req->uri)(req, this);
        return;
    }

    for (const auto& kv : server->requestHandlers) {
        auto v1 = split(kv.first, '/');
        auto v2 = split(req->uri, '/');

        if (v1.size() != v2.size()) continue;

        bool flag = true;
        for (size_t i = 0; i < v1.size(); i++)
            if (v1[i] != v2[i] && v1[i] != "*") {
                flag = false;
                break;
            }
        if (flag) {
            kv.second(req, this);
            return;
        }
    }

    if (server->generalHandler) {
        server->generalHandler(req, this);
        return;
    }

    request r;
    r.uri = req->uri;
    r.set_response(HTTP_NOTFOUND, "NOT FOUND");
    if (req->get_header("Connection") == "close")
        r.set_header("Connection", "close");
    r.send_to(get_write_buffer());
}

}  // namespace wxg
