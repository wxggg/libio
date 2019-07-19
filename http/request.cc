#include "request.hh"
#include <core/time.hh>

namespace wxg {

parse_status_t request::parse(buffer *buf) {
    switch (status) {
        case READING_FIRSTLINE:
            return parse_firstline(buf);
        case READING_HEADERS:
            return parse_headers(buf);
        case READING_BODY:
            return parse_body(buf);
        case READING_TRAILER:
            return parse_trailer(buf);
        default:
            return CORRUPTED;
            break;
    }
}

parse_status_t request::parse_firstline(buffer *buf) {
    if (buf->empty()) return NEEDMORE;
    auto line = buf->pop();
    if (line.empty()) return NEEDMORE;

    switch (kind) {
        case REQUEST:
            if (parse_request_line(line) == -1) return CORRUPTED;
            break;
        case RESPONSE:
            if (parse_response_line(line) == -1) return CORRUPTED;
            break;

        default:
            break;
    }

    status = READING_HEADERS;
    return parse_headers(buf);
}

parse_status_t request::parse_headers(buffer *buf) {
    string k, v;
    while (!buf->empty()) {
        auto line = buf->pop();
        if (line.empty()) {  // finish headers

            switch (kind) {
                case REQUEST:
                    if (type != POST) return ALLREAD;
                    break;
                case RESPONSE:
                    if (response_code == HTTP_NOCONTENT ||
                        response_code == HTTP_NOTMODIFIED ||
                        (response_code >= 100 && response_code < 200))
                        return ALLREAD;
                    break;

                default:
                    return CORRUPTED;
                    break;
            }

            if (status == READING_HEADERS) {
                status = READING_BODY;
                return parse_body(buf);
            } else if (status == READING_TRAILER)
                return ALLREAD;
            else
                return CORRUPTED;
        }

        /* Check if this is a continuation line */
        if (line[0] == ' ' || line[0] == '\t') {
            if (k.empty()) return CORRUPTED;
            this->headers[k] = v + ltrim(line, " \t");
            continue;
        }

        auto pos = line.find(':');
        if (pos == string::npos) return CORRUPTED;

        k = line.substr(0, pos);
        v = line.substr(pos + 1);
        this->headers[trim(k)] = trim(v);
    }

    return NEEDMORE;
}

parse_status_t request::parse_body(buffer *buf) {
    if (headers["Transfer-Encoding"] == "chunked")
        chunked = true, ntoread = -1;
    else {
        if (get_body_length() == -1) return CORRUPTED;
    }

    if (chunked) {
        while (!buf->empty()) {
            if (ntoread < 0) {
                auto line = buf->pop();
                // read chunk size
                ntoread = std::stol(line, 0, 16);
                if (line.empty() || ntoread < 0) return CORRUPTED;

                if (ntoread == 0) {  // read chunk finished
                    status = READING_TRAILER;
                    return parse_trailer(buf);
                }
            } else {
                if (static_cast<int>(buf->length()) < ntoread) break;

                buf_->push(buf, ntoread + 2);  // 2 means "\r\n"
                ntoread = -1;
            }
        }
        return NEEDMORE;
    } else if (ntoread < 0) {
        buf_->push(buf);  // read until connection close
    } else if (static_cast<int>(buf->length()) >= ntoread) {
        buf_->push(buf, ntoread);
        return ALLREAD;
    }

    return NEEDMORE;
}

parse_status_t request::parse_trailer(buffer *buf) {
    return parse_headers(buf);
}

/* request format: method uri protocol */
int request::parse_request_line(const string &line) {
    size_t k1 = line.find(' ');
    if (k1 == string::npos) return -1;
    size_t k2 = line.find(' ', k1 + 1);
    if (k2 == string::npos) return -1;

    string method = line.substr(0, k1);
    string uri = line.substr(k1 + 1, k2 - k1 - 1);
    string protocol = line.substr(k2 + 1);

    if (method == "GET")
        this->type = GET;
    else if (method == "POST")
        this->type = POST;
    else if (method == "HEAD")
        this->type = HEAD;
    else
        return -1;

    size_t k3 = uri.find('?');
    if (k3 != string::npos) {
        this->uri = uri.substr(0, k3);
        this->query = uri.substr(k3 + 1);
    } else
        this->uri = uri;

    this->uri = string_from_utf8(this->uri);

    if (protocol == "HTTP/1.0")
        major = 1, minor = 0;
    else if (protocol == "HTTP/1.1")
        major = minor = 1;
    else
        return -1;

    return 0;
}

/* response format: protocol response_code response_line */
int request::parse_response_line(const string &line) {
    size_t k1 = line.find(' ');
    if (k1 == string::npos) return -1;
    size_t k2 = line.find(' ', k1 + 1);
    if (k2 == string::npos) return -1;

    string protocol = line.substr(0, k1);
    string code = line.substr(k1 + 1, k2 - k1 - 1);
    string response_line = line.substr(k2 + 1);

    if (protocol == "HTTP/1.0")
        major = 1, minor = 0;
    else if (protocol == "HTTP/1.1")
        major = minor = 1;
    else
        return -1;

    this->response_code = static_cast<http_code_t>(std::stoi(code));
    this->response_line = response_line;
    return 0;
}

int request::get_body_length() {
    string content_length = headers["Content-Length"];
    string connection = headers["Connection"];

    if (content_length.empty())
        ntoread = -1;
    else {
        ntoread = std::stol(content_length);
        if (ntoread < 0) return -1;
    }
    return 0;
}

void request::set_response(http_code_t code, const std::string &reason,
                           buffer *content) {
    this->kind = RESPONSE;
    this->response_code = code;
    this->response_line = reason;

    if (content)
        buf_->push(content);
    else {
        switch (code) {
            case HTTP_OK:
                break;
            case HTTP_NOTFOUND:
                push_not_found();
                break;
            case HTTP_NOCONTENT:
                break;
            default:
                push_error(code, reason);
                break;
        }
    }

    firstline = "HTTP/" + std::to_string(major) + "." + std::to_string(minor) +
                " " + std::to_string(response_code) + " " + response_line +
                "\r\n";

    headers.clear();
    if (major == 1) {
        if (minor == 1) {
            if (headers["Date"].empty()) headers["Date"] = time::get_date();
            headers["Connection"] = "keep-alive";
        }

        if (headers["Connection"] == "keep-alive")
            headers["Content-Length"] = std::to_string(buf_->length());
    }

    if (buf_->length() > 0) {
        headers["Content-Length"] = std::to_string(buf_->length());
        headers["Content-Type"] = "text/html; charset=utf-8";
    }
}

void request::set_request(request_type_t type, const std::string &uri,
                          buffer *content) {
    this->type = type;
    this->uri = uri;

    if (content) buf_->push(content);

    headers.clear();
    headers["Content-Type"] = "text/html; charset=utf-8";

    string method;
    switch (type) {
        case GET:
            method = "GET";
            break;
        case POST:
            method = "POST";
            headers["Content-Length"] = std::to_string(buf_->length());
            break;
        case HEAD:
            method = "HEAD";
            break;
        default:
            break;
    }
    firstline = method + " " + uri + " HTTP/" + std::to_string(major) + "." +
                std::to_string(minor) + "\r\n";
}

void request::send_to(buffer *buf) {
    buf->push(firstline);

    for (const auto &kv : headers)
        if (!kv.second.empty()) buf->push(kv.first + ": " + kv.second + "\r\n");

    buf->push("\r\n");

    if (this->buf_->length() > 0) buf->push(this->buf_.get());
}

void request::push_not_found() {
    std::string urihtml = this->uri;
    htmlescape(urihtml);

    buf_->push("<html><head><title>404 Not Found</title></head>\n");
    buf_->push("<body><h1>Not Found</h1>\n");
    buf_->push("<p>The requested URL " + urihtml +
               " was not found on this server.</p>");
    buf_->push("</body></html>\n");
}

void request::push_error(int error, const std::string &reason) {
    buf_->push("<html><head><title>" + std::to_string(error) + " " + reason +
               "</title></head>\n");
    buf_->push("<body>\n<h1>Method Not Implemented</h1>\n");
    buf_->push("<p>Invalid method in request</p>\n");
    buf_->push("</body></html>\n");
}

}  // namespace wxg
