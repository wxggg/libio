#pragma once

#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace wxg {

inline std::vector<std::string> split(const std::string &s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream iss(s);
    while (std::getline(iss, token, delimiter)) tokens.push_back(token);
    return tokens;
}

inline bool equal_lower_n(const std::string &a, const std::string &b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (i == a.size() || i == b.size()) return false;
        if (::tolower(a[i] != ::tolower(b[i]))) return false;
    }
    return true;
}

inline bool equal_lower(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) return false;
    return equal_lower_n(a, b, a.size());
}

inline bool is_palindrome(const std::string &s) {
    return std::equal(s.begin(), s.begin() + s.size() / 2, s.rbegin());
}

static const char *ws = " \t\n\r\f\v";

// trim from end of string (right)
inline std::string &rtrim(std::string &s, const char *t = ws) {
    s.erase(s.find_last_not_of(t) + 1);
    return s;
}

// trim from beginning of string (left)
inline std::string &ltrim(std::string &s, const char *t = ws) {
    s.erase(0, s.find_first_not_of(t));
    return s;
}

// trim from both ends of string (left & right)
inline std::string &trim(std::string &s, const char *t = ws) {
    return ltrim(rtrim(s, t), t);
}

inline std::string replace(const std::string &str, const std::string &from,
                    const std::string &to) {
    if (from.empty()) return str;
    std::regex re("\\" + from);
    return std::regex_replace(str, re, to);
}

/*
 * Replaces <, >, ", ' and & with &lt;, &gt;, &quot;,
 * &#039; and &amp; correspondingly.
 */
inline void htmlescape(std::string &html) {
    html = replace(html, "<", "&lt");
    html = replace(html, ">", "&gt");
    html = replace(html, "\"", "&quot");
    html = replace(html, "'", "&#039");
    html = replace(html, "&", "&amp");
}

inline int hex_to_int(char a) {
    if (a >= '0' && a <= '9')
        return (a - 48);
    else if (a >= 'A' && a <= 'Z')
        return (a - 55);
    else
        return (a - 87);
}

inline std::string string_from_utf8(const std::string &in) {
    std::string result;
    std::stringstream ss;
    size_t i = 0, n = in.length();
    bool flag = false;
    while (i < n) {
        char x = in[i++];
        if (x == '%') {
            flag = true;
            int hex1 = hex_to_int(in[i++]), hex2 = hex_to_int(in[i++]);
            ss << static_cast<char>(hex1 * 16 + hex2);
        } else {
            if (flag) {
                result += ss.str();
                std::stringstream().swap(ss);
            }
            result += x;
            flag = false;
        }
    }
    if (flag) result += ss.str();
    return result;
}

}  // namespace wxg
