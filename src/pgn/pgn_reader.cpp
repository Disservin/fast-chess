#include <pgn/pgn_reader.hpp>

#include <iostream>
#include <memory>

#include <chess.hpp>

namespace fast_chess {

class PGNVisitor : public chess::pgn::Visitor {
   public:
    PGNVisitor(std::vector<Opening>& pgns) : pgns_(pgns) {}
    virtual ~PGNVisitor() {}

    bool isFen(const std::string_view line) {
        std::istringstream iss(std::string(line));
        std::string part;
        int count = 0;
    
        while (iss >> part) {
            count++;
        }
    
        // Check the number of parts
        if (count == 6 && line.find(';') == std::string::npos) {
            return true;
        } else {
            return false;
        }
    }

    void startPgn() {
        board_.setFen(chess::constants::STARTPOS);

        pgn_.fen = chess::constants::STARTPOS;
        pgn_.moves.clear();
    }

    void header(std::string_view key, std::string_view value) {
        if (key == "FEN") {
            if (isFen(value)){
               board_.setFen(value);
            } else {
               board_.setEpd(value);
            }
            pgn_.fen = board_.getFen();
        }
    }

    void startMoves() {}

    void move(std::string_view move, std::string_view) {
        chess::Move move_i;

        try {
            move_i = chess::uci::parseSan(board_, move);
        } catch (const chess::uci::SanParseError& e) {
            std::cerr << e.what() << '\n';
            return;
        }

        pgn_.moves.push_back(move_i);
        board_.makeMove(move_i);
    }

    void endPgn() {
        pgn_.stm = board_.sideToMove();
        pgns_.push_back(pgn_);
    }

   private:
    std::vector<Opening>& pgns_;
    Opening pgn_;
    chess::Board board_;
};

PgnReader::PgnReader(const std::string& pgn_file_path) { pgn_file_.open(pgn_file_path); }

std::vector<Opening> PgnReader::getOpenings() { return analyseFile(); }

std::vector<Opening> PgnReader::analyseFile() {
    std::vector<Opening> pgns_;

    auto vis = std::make_unique<PGNVisitor>(pgns_);
    chess::pgn::StreamParser parser(pgn_file_);
    parser.readGames(*vis);

    return pgns_;
}

}  // namespace fast_chess
