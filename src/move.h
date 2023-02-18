#pragma once

#include "types.h"

struct Move
{
    Square from_sq;
    Square to_sq;
    PieceType promotion_piece;

    Move(Square from_sq_cp = {}, Square to_sq_cp = {}, PieceType promotion_piece_cp = {})
        : from_sq(from_sq_cp), to_sq(to_sq_cp), promotion_piece(promotion_piece_cp)
    {
    }
};

inline std::ostream &operator<<(std::ostream &os, const Move &m)
{
    os << "from sq " << int(m.from_sq) << " to_sq " << int(m.to_sq) << " promotion " << int(m.promotion_piece);
    return os;
}

inline bool operator==(const Move &lhs, const Move &rhs)
{
    return lhs.from_sq == rhs.from_sq && lhs.to_sq == rhs.to_sq && lhs.promotion_piece == rhs.promotion_piece;
}

inline bool operator!=(const Move &lhs, const Move &rhs)
{
    return !(lhs == rhs);
}
