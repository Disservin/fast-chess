#include <cassert>

#include "helper.hpp"

namespace fast_chess
{

#if defined(__GNUC__) // GCC, Clang

Square lsb(Bitboard b)
{
    assert(b);
    return Square(__builtin_ctzll(b));
}

Square msb(Bitboard b)
{
    assert(b);
    return Square(63 ^ __builtin_clzll(b));
}

#elif defined(_MSC_VER)
#ifdef _WIN32 // MSVC, WIN64
#include <intrin.h>
Square lsb(Bitboard b)
{
    unsigned long idx;
    _BitScanForward64(&idx, b);
    return (Square)idx;
}

Square msb(Bitboard b)
{
    unsigned long idx;
    _BitScanReverse64(&idx, b);
    return (Square)idx;
}

#endif

#else

#error "Compiler not supported."

#endif

int popcount(Bitboard mask)
{
#if defined(_MSC_VER) || defined(__INTEL_COMPILER)

    return (uint8_t)_mm_popcnt_u64(mask);

#else

    return __builtin_popcountll(mask);

#endif
}

Square poplsb(Bitboard &mask)
{
    const int8_t s = lsb(mask);
    mask &= mask - 1; // compiler optimizes this to _blsr_
    return Square(s);
}

} // namespace fast_chess
