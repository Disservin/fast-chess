#pragma once

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <ios>
#include <istream>
#include <ostream>
#include <streambuf>

namespace fastchess {

class gzstreambuf : public std::streambuf {
   public:
    gzstreambuf() = default;

    gzstreambuf(const char* filename, std::ios::openmode mode) { open(filename, mode); }

    ~gzstreambuf() override { close(); }

    gzstreambuf(const gzstreambuf&)            = delete;
    gzstreambuf& operator=(const gzstreambuf&) = delete;

    bool is_open() const noexcept { return file_ != nullptr; }

    gzstreambuf* open(const char* filename, std::ios::openmode mode) {
        if (is_open() || filename == nullptr) {
            return nullptr;
        }

        const char* zlib_mode = mode_string(mode);
        if (zlib_mode == nullptr) {
            return nullptr;
        }

        file_ = gzopen(filename, zlib_mode);
        if (file_ == nullptr) {
            return nullptr;
        }

        mode_ = mode;

        if (mode_ & std::ios::in) {
            setg(buffer_, buffer_ + putback_size, buffer_ + putback_size);
        }

        if (mode_ & std::ios::out) {
            setp(buffer_, buffer_ + buffer_size);
        }

        return this;
    }

    gzstreambuf* close() {
        if (!is_open()) {
            return nullptr;
        }

        bool ok = true;

        if ((mode_ & std::ios::out) && sync() != 0) {
            ok = false;
        }

        if (gzclose(file_) != Z_OK) {
            ok = false;
        }

        file_ = nullptr;
        mode_ = {};

        setg(nullptr, nullptr, nullptr);
        setp(nullptr, nullptr);

        return ok ? this : nullptr;
    }

   protected:
    int_type underflow() override {
        if (!is_open() || !(mode_ & std::ios::in)) {
            return traits_type::eof();
        }

        if (gptr() != nullptr && gptr() < egptr()) {
            return traits_type::to_int_type(*gptr());
        }

        std::size_t putback = 0;

        if (eback() != nullptr && gptr() != nullptr) {
            putback = std::min<std::size_t>(putback_size, static_cast<std::size_t>(gptr() - eback()));

            std::memmove(buffer_ + (putback_size - putback), gptr() - putback, putback);
        }

        const int bytes_read =
            gzread(file_, buffer_ + putback_size, static_cast<unsigned int>(buffer_size - putback_size));

        if (bytes_read <= 0) {
            return traits_type::eof();
        }

        setg(buffer_ + (putback_size - putback), buffer_ + putback_size, buffer_ + putback_size + bytes_read);

        return traits_type::to_int_type(*gptr());
    }

    int_type overflow(int_type ch = traits_type::eof()) override {
        if (!is_open() || !(mode_ & std::ios::out)) {
            return traits_type::eof();
        }

        if (!flush_output()) {
            return traits_type::eof();
        }

        if (!traits_type::eq_int_type(ch, traits_type::eof())) {
            *pptr() = traits_type::to_char_type(ch);
            pbump(1);
        }

        return traits_type::not_eof(ch);
    }

    int sync() override {
        if (!is_open()) {
            return -1;
        }

        if ((mode_ & std::ios::out) && !flush_output()) {
            return -1;
        }

        return 0;
    }

   private:
    static constexpr std::size_t buffer_size  = 16 * 1024;
    static constexpr std::size_t putback_size = 8;

    gzFile file_ = nullptr;
    std::ios::openmode mode_{};
    char buffer_[buffer_size]{};

    static const char* mode_string(std::ios::openmode mode) {
        const bool reading   = (mode & std::ios::in) != 0;
        const bool writing   = (mode & std::ios::out) != 0;
        const bool appending = (mode & std::ios::app) != 0;

        // gzFile does not support simultaneous read/write access.
        if (reading == writing) {
            return nullptr;
        }

        if (reading) {
            return "rb";
        }

        if (appending) {
            return "ab";
        }

        return "wb";
    }

    bool flush_output() {
        if (pptr() == nullptr || pbase() == nullptr) {
            return true;
        }

        const auto count = static_cast<unsigned int>(pptr() - pbase());

        if (count > 0) {
            const int written = gzwrite(file_, pbase(), count);

            if (written != static_cast<int>(count)) {
                return false;
            }

            pbump(-static_cast<int>(count));
        }

        return true;
    }
};

class igzstream : public std::istream {
   public:
    igzstream() : std::istream(&buffer_) {}

    explicit igzstream(const char* filename, std::ios::openmode mode = std::ios::in) : std::istream(&buffer_) {
        open(filename, mode);
    }

    gzstreambuf* rdbuf() noexcept { return &buffer_; }

    const gzstreambuf* rdbuf() const noexcept { return &buffer_; }

    bool is_open() const noexcept { return buffer_.is_open(); }

    void open(const char* filename, std::ios::openmode mode = std::ios::in) {
        if (!buffer_.open(filename, mode | std::ios::in)) {
            setstate(std::ios::failbit);
        } else {
            clear();
        }
    }

    void close() {
        if (!buffer_.close()) {
            setstate(std::ios::failbit);
        }
    }

   private:
    gzstreambuf buffer_;
};

class ogzstream : public std::ostream {
   public:
    ogzstream() : std::ostream(&buffer_) {}

    explicit ogzstream(const char* filename, std::ios::openmode mode = std::ios::out) : std::ostream(&buffer_) {
        open(filename, mode);
    }

    gzstreambuf* rdbuf() noexcept { return &buffer_; }

    const gzstreambuf* rdbuf() const noexcept { return &buffer_; }

    bool is_open() const noexcept { return buffer_.is_open(); }

    void open(const char* filename, std::ios::openmode mode = std::ios::out) {
        if (!buffer_.open(filename, mode | std::ios::out)) {
            setstate(std::ios::failbit);
        } else {
            clear();
        }
    }

    void close() {
        if (!buffer_.close()) {
            setstate(std::ios::failbit);
        }
    }

   private:
    gzstreambuf buffer_;
};
}  // namespace fastchess