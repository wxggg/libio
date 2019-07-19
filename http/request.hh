#pragma once

#include <map>
#include <memory>
#include <string>

#include <core/buffer.hh>
#include <core/string.hh>

#include "http.hh"

using std::cout;
using std::endl;
using std::map;
using std::string;

namespace wxg {

enum request_type_t { GET = 0, POST, HEAD };
enum request_kind_t { REQUEST = 0, RESPONSE };
enum request_status_t {
    READING_FIRSTLINE = 0,
    READING_HEADERS,
    READING_BODY,
    READING_TRAILER
};

enum parse_status_t { ALLREAD = 0, NEEDMORE, CORRUPTED, CANCELD };

class request {
   private:
    map<string, string> headers;
    std::unique_ptr<buffer> buf_;

    /* chunked */
    bool chunked = false;
    long ntoread = 0;

   public:
    string firstline;

    request_kind_t kind = RESPONSE;
    request_type_t type;
    request_status_t status = READING_FIRSTLINE;

    int major = 1;
    int minor = 1;

    /* for request */
    string uri;
    string query;

    /* for response */
    http_code_t response_code;
    string response_line;

   public:
    request() { buf_ = std::make_unique<buffer>(); }
    ~request() {}

    inline buffer *get_buffer() const { return buf_.get(); }
    inline string get_header(const string &key) const {
        return headers.count(key) ? headers.at(key) : "";
    }
    inline void set_header(const string &key, const string &value) {
        headers[key] = value;
    }

    inline void set_protocol(int major, int minor) {
        this->major = major, this->minor = minor;
    }
    void set_response(http_code_t code, const std::string &reason,
                      buffer *content = nullptr);
    void set_request(request_type_t type, const std::string &uri,
                     buffer *content = nullptr);

    parse_status_t parse(buffer *buf);

    parse_status_t parse_firstline(buffer *buf);
    parse_status_t parse_headers(buffer *buf);
    parse_status_t parse_body(buffer *buf);
    parse_status_t parse_trailer(buffer *buf);

    /* request format: method uri protocol */
    int parse_request_line(const string &line);
    /* response format: protocol response_code response_line */
    int parse_response_line(const string &line);

    int get_body_length();

    void send_to(buffer *buf);

   private:
    void push_not_found();
    void push_error(int error, const std::string &reason);
};

}  // namespace wxg
