#pragma once

namespace wxg {

/* Response codes */

enum http_code_t {
    HTTP_OK = 200,
    HTTP_NOCONTENT = 204,
    HTTP_MOVEPERM = 301,
    HTTP_MOVETEMP = 302,
    HTTP_NOTMODIFIED = 304,
    HTTP_BADREQUEST = 400,
    HTTP_NOTFOUND = 404,
    HTTP_SERVUNAVAIL = 503
};

}  // namespace wxg
