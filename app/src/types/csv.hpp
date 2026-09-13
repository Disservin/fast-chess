#pragma once

#include <string>

namespace fastchess::config {

struct Csv {
    std::string file;
    // column separator, ';' is handy for spreadsheets using a comma as decimal mark
    std::string separator = ",";
    // how many pairs (or games without -report penta) between two rows, 0 -> ratinginterval
    int interval     = 0;
    bool append_file = false;
};

}  // namespace fastchess::config
