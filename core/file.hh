#pragma once
#include <fstream>
#include <sstream>
#include <string>

using std::string;

namespace wxg {

string read_file(string &path) {
    std::stringstream ss;
    {
        std::ifstream ifs(path);
        if (!ifs) return "";

        ss << ifs.rdbuf();
    }
    return ss.str();
}

}  // namespace wxg
