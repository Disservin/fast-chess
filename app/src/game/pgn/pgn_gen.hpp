#pragma once

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <fmt/include/fmt/core.h>

namespace fastchess::pgn {

struct PGNMove {
    std::string move;
    std::string comment;

    PGNMove(std::string m, std::string c = "") : move(m), comment(c) {}
};

class PGNGenerator {
   private:
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<PGNMove> moves;
    std::string result;
    int startMoveNumber;
    bool blackStarts;

   public:
    PGNGenerator() : result("*"), startMoveNumber(1), blackStarts(false) {}

    void addHeader(std::string_view key, std::string_view value) {
        if (key.empty() || value.empty()) return;
        headers.push_back({std::string(key), std::string(value)});
    }

    void addMove(const std::string& move, const std::string& comment = "") { moves.emplace_back(move, comment); }

    void setStartMoveNumber(int num) { startMoveNumber = num; }

    void setBlackStarts(bool b) { blackStarts = b; }

    void setResult(const std::string& res) {
        result     = res;
        bool found = false;
        for (auto& header : headers) {
            if (header.first == "Result") {
                header.second = res;
                found         = true;
                break;
            }
        }
        if (!found) {
            addHeader("Result", res);
        }
    }

    std::string generate() const {
        std::string output;

        // Append Headers
        for (const auto& header : headers) {
            fmt::format_to(std::back_inserter(output), "[{} \"{}\"]\n", header.first, header.second);
        }

        if (!headers.empty()) {
            output += "\n";
        }

        // Append Moves
        int currentLineLength = 0;

        auto appendText = [&](const std::string& text) {
            // Check if adding this text exceeds 80 chars
            // +1 accounts for the space we might add
            if (currentLineLength > 0 && currentLineLength + text.length() + 1 > 80) {
                output += "\n";
                currentLineLength = 0;
            }

            if (currentLineLength > 0) {
                output += " ";
                currentLineLength++;
            }

            output += text;
            currentLineLength += text.length();
        };

        int moveCounter = startMoveNumber;
        for (size_t i = 0; i < moves.size(); ++i) {
            bool isWhite = !blackStarts ? (i % 2 == 0) : (i % 2 != 0);

            std::string pair;

            if (!moves[i].move.empty()) {
                const auto curr = (moveCounter + 1) / 2;
                if (isWhite) {
                    pair = fmt::format("{}. {}", curr, moves[i].move);
                } else {
                    // Black's turn
                    if (i == 0) {
                        // Special case: Start with Black (e.g. 1... e5)
                        pair = fmt::format("{}... {}", curr, moves[i].move);
                    } else {
                        pair = moves[i].move;
                    }
                }
            }

            moveCounter++;

            // Append Comment if exists
            if (!moves[i].comment.empty()) {
                pair += fmt::format("{}{{{}}}", moves[i].move.empty() ? "" : " ", moves[i].comment);
            }

            appendText(pair);
        }

        // Append Result Terminator
        appendText(result);

        return output;
    }
};

}  // namespace fastchess::pgn
