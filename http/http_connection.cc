#include "http_connection.hh"
#include "http_multithread_server.hh"
#include "http_thread.hh"

#include <core/buffer.hh>

namespace wxg {

http_connection::http_connection(http_thread* _thread, int _fd,
                                 const std::string& _addr, unsigned short _port)
    : thread(_thread) {
    fd = _fd;
    address = _addr;
    port = _port;
    status = CONNECTED;

    if (!thread || fd <= 0) {
        cerr << __func__ << " : error" << endl;
        exit(-1);
    }

    setup_new_events();
}

http_connection::~http_connection() { close(); }

/*
 * when init a connection, the handler should be set
 * when reuse a connection, need to init again, because fd changed
 */
void http_connection::setup_new_events() {
    get_reactor()->set_read_handler(fd, [this]() {
        int n = read();

        if (n == -1) {
            if (errno != EAGAIN && errno != EINTR) {
                perror("connection read");
                get_reactor()->remove_read(fd);
            }
        } else if (n == 0) {  // EOF
            get_reactor()->remove_read(fd);
            status = CLOSING;
        } else {
            parse_request();
        }
    });
    get_reactor()->remove_read(fd);

    get_reactor()->set_write_handler(fd, [this]() {
        if (get_write_buffer()->empty()) {
            get_reactor()->remove_write(fd);
            if (status == CLOSING) close();
            return;
        }

        int n = write();
        if (n == -1) {
            if (errno != EAGAIN && errno != EINTR && errno != EINPROGRESS) {
                cerr << "fixme: write error" << endl;
                get_reactor()->remove_write(fd);
                status = CLOSING;
            }
        } else if (n == 0) {
            cerr << "error write eof" << endl;
            get_reactor()->remove_write(fd);
            status = CLOSING;
        } else {
        }
    });
    get_reactor()->remove_write(fd);
}

reactor<epoll>* http_connection::get_reactor() const {
    return thread->get_reactor();
}

void http_connection::parse_request() {
    if (get_read_buffer()->empty()) {
        if (status == CONNECTED) get_reactor()->add_read(fd);
        return;
    }

    bool processing = true;

    while (processing && !get_read_buffer()->empty()) {
        if (requests.empty()) {
            auto req = std::make_unique<request>();
            req->kind = REQUEST;
            requests.push(std::move(req));
        }

        processing = false;
        parse_status_t res = requests.front()->parse(get_read_buffer());
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
                status = CLOSING;
                send_reply(HTTP_BADREQUEST, "bad request", "400 Bad Request");
                break;
            case CANCELD:
            default:
                cerr << "unknow error" << endl;
                shutdown(fd, SHUT_RD);
                get_reactor()->remove_read_handler(fd);
                status = CLOSING;
                break;
        }
        if (!get_write_buffer()->empty()) get_reactor()->add_write(fd);
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
    req->send_to(get_write_buffer());
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
    push(ss.str() + "\r\n");
    push(buf);
    push("\r\n");
    get_reactor()->add_write(fd);
}

void http_connection::send_chunk_end() {
    push("0\r\n\r\n");
    get_reactor()->add_write(fd);
}

void http_connection::close() {
    if (fd > 0) {
        get_reactor()->erase(fd);
        shutdown(fd, SHUT_WR);
        ::close(fd);

        thread->release_connection(fd);
        fd = -1;

        get_write_buffer()->clear();
        get_read_buffer()->clear();
        status = CLOSED;
        std::queue<std::unique_ptr<request>>().swap(requests);
    }
}

void http_connection::handle_request(request* req) {
    if (!req) return;

    if (req->get_header("Connection") == "close") {
        shutdown(fd, SHUT_RD);
        get_reactor()->remove_read_handler(fd);
        status = CLOSING;
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