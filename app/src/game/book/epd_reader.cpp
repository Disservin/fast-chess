#include <game/book/epd_reader.hpp>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <core/memory/heap_str.hpp>
#include <types/exception.hpp>

#ifdef USE_ZLIB
#    include <core/logger/gzstream.hpp>
#endif

namespace {
std::istream& safeGetline(std::istream& is, std::string& t) {
    t.clear();

    // The characters in the stream are read one-by-one using a std::streambuf.
    // That is faster than reading them one-by-one using the std::istream.
    // Code that uses streambuf this way must be guarded by a sentry object.
    // The sentry object performs various tasks,
    // such as thread synchronization and updating the stream state.

    std::istream::sentry se(is, true);
    std::streambuf* sb = is.rdbuf();

    for (;;) {
        int c = sb->sbumpc();
        switch (c) {
            case '\n':
                return is;
            case '\r':
                if (sb->sgetc() == '\n') sb->sbumpc();
                return is;
            case std::streambuf::traits_type::eof():
                // Also handle the case when the last line has no line ending
                if (t.empty()) is.setstate(std::ios::eofbit);
                return is;
            default:
                t += (char)c;
        }
    }
}
}  // namespace

namespace fastchess::book {

EpdReader::EpdReader(const std::string& epd_file_path) : epd_file_(epd_file_path) {
    bool is_gzipped = epd_file_path.size() >= 3 && epd_file_path.substr(epd_file_path.size() - 3) == ".gz";

    std::unique_ptr<std::istream> input_stream;
    if (is_gzipped) {
#ifdef USE_ZLIB
        input_stream = std::make_unique<igzstream>(epd_file_path.c_str());

        if (dynamic_cast<igzstream*>(input_stream.get())->rdbuf()->is_open() == false) {
            throw fastchess_exception::format("Failed to open file: {}", epd_file_path);
        }
#else
        throw fastchess_exception("Compressed book is provided but program wasn't compiled with zlib.");
#endif
    } else {
        input_stream = std::make_unique<std::ifstream>(epd_file_path);

        if (dynamic_cast<std::ifstream*>(input_stream.get())->is_open() == false) {
            throw fastchess_exception::format("Failed to open file: {}", epd_file_path);
        }
    }

    std::string line;
    while (safeGetline(*input_stream, line)) {
        if (!line.empty()) openings_.emplace_back(util::heap_string(line));
    }

    if (openings_.empty()) {
        throw fastchess_exception::format("No openings found in file: {}", epd_file_path);
    }
}

}  // namespace fastchess::book
