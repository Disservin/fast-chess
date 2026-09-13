#include <game/book/pgn_reader.hpp>

#include <fstream>
#include <iostream>
#include <memory>

#include <chess.hpp>

#include <core/logger/logger.hpp>
#include <game/book/opening.hpp>
#include <types/exception.hpp>

#ifdef USE_ZLIB
#    include <core/logger/gzstream.hpp>
#endif

namespace fastchess::book {

class PGNVisitor : public chess::pgn::Visitor {
   public:
    PGNVisitor(std::vector<Opening>& pgns, int plies_limit, bool is_frc)
        : pgns_(pgns), plies_limit_(plies_limit), is_frc_(is_frc) {
        board_.set960(is_frc_);
    }
    virtual ~PGNVisitor() {}

    void startPgn() override {
        board_.setFen(chess::constants::STARTPOS);

        pgn_.fen_epd = chess::constants::STARTPOS;
        pgn_.moves.clear();
        ply_count_  = 0;
        early_stop_ = false;
    }

    void header(std::string_view key, std::string_view value) override {
        if (key == "FEN") {
            board_.setFen(value);
            pgn_.fen_epd = value;
        }
    }

    void startMoves() override {}

    void move(std::string_view move, std::string_view) override {
        if (early_stop_) return;
        if (ply_count_++ >= plies_limit_ && plies_limit_ != -1) return;

        chess::Move move_i;

        try {
            move_i = chess::uci::parseSan(board_, move);
        } catch (const chess::uci::SanParseError& e) {
            LOG_ERR("Failed to parse move: {}", e.what());
            early_stop_ = true;
            return;
        }

        board_.makeMove<true>(move_i);

        if (board_.isGameOver().second != chess::GameResult::NONE) {
            early_stop_ = true;
            return;
        }

        pgn_.moves.push_back(move_i);
    }

    void endPgn() override { pgns_.push_back(pgn_); }

   private:
    std::vector<Opening>& pgns_;
    Opening pgn_;
    chess::Board board_;
    const int plies_limit_;
    int ply_count_   = 0;
    bool is_frc_     = false;
    bool early_stop_ = false;
};

PgnReader::PgnReader(const std::string& pgn_file_path, int plies_limit, bool is_frc)
    : file_name_(pgn_file_path), plies_limit_(plies_limit) {
    auto vis        = std::make_unique<PGNVisitor>(pgns_, plies_limit_, is_frc);
    bool is_gzipped = file_name_.size() >= 3 && file_name_.substr(file_name_.size() - 3) == ".gz";

    std::unique_ptr<std::istream> input_stream;

    if (is_gzipped) {
#ifdef USE_ZLIB
        input_stream = std::make_unique<igzstream>(file_name_.c_str());

        if (dynamic_cast<igzstream*>(input_stream.get())->rdbuf()->is_open() == false) {
            throw fastchess_exception::format("Failed to open file: {}", file_name_);
        }
#else
        throw fastchess_exception("Compressed book is provided but program wasn't compiled with zlib.");
#endif
    } else {
        input_stream = std::make_unique<std::ifstream>(file_name_);

        if (dynamic_cast<std::ifstream*>(input_stream.get())->is_open() == false) {
            throw fastchess_exception::format("Failed to open file: {}", file_name_);
        }
    }

    chess::pgn::StreamParser parser(*input_stream);
    parser.readGames(*vis);
    if (pgns_.empty()) {
        throw fastchess_exception::format("No openings found in file: {}", file_name_);
    }
}

}  // namespace fastchess::book
