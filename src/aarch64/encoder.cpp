#include "aarch64/encoder.h"

#include <climits>
#include <string>
#include <stdexcept>

namespace armcave {
namespace aarch64 {

namespace {

bool signed_delta(uint64_t source, uint64_t target, int64_t &delta)
{
    if (target >= source)
    {
        uint64_t distance = target - source;
        if (distance > (uint64_t)INT64_MAX)
            return false;
        delta = (int64_t)distance;
        return true;
    }
    uint64_t distance = source - target;
    if (distance > (uint64_t)INT64_MAX)
        return false;
    delta = -(int64_t)distance;
    return true;
}

bool fits_scaled(uint64_t source, uint64_t target, int bits, int scale)
{
    int64_t delta = 0;
    if (!signed_delta(source, target, delta) || delta % scale != 0)
        return false;
    int64_t immediate = delta / scale;
    return immediate >= -(1LL << (bits - 1)) &&
           immediate < (1LL << (bits - 1));
}

int64_t require_scaled_delta(uint64_t source, uint64_t target,
                             int bits, int scale, const char *what)
{
    int64_t delta = 0;
    if (!signed_delta(source, target, delta) || delta % scale != 0)
        throw std::runtime_error(std::string(what) + " target is not aligned");
    int64_t immediate = delta / scale;
    if (immediate < -(1LL << (bits - 1)) ||
        immediate >= (1LL << (bits - 1)))
        throw std::runtime_error(std::string(what) + " target is out of range");
    return immediate;
}

void append_word(std::vector<uint8_t> &out, uint32_t instruction)
{
    out.push_back((uint8_t)(instruction & 0xffU));
    out.push_back((uint8_t)((instruction >> 8) & 0xffU));
    out.push_back((uint8_t)((instruction >> 16) & 0xffU));
    out.push_back((uint8_t)((instruction >> 24) & 0xffU));
}

std::vector<uint8_t> make_address_sequence_impl(uint64_t source,
                                                uint64_t target,
                                                uint8_t rd)
{
    std::vector<uint8_t> out;
    if (fits_adrp(source, target))
    {
        append_word(out, encode_adrp(rd, source, target));
        append_word(out, encode_add_imm(rd, rd, (uint16_t)(target & 0xfffU)));
        return out;
    }
    return make_load_immediate(rd, target);
}

}

bool fits_branch26(uint64_t source, uint64_t target)
{
    return fits_scaled(source, target, 26, 4);
}

bool fits_branch19(uint64_t source, uint64_t target)
{
    return fits_scaled(source, target, 19, 4);
}

bool fits_branch14(uint64_t source, uint64_t target)
{
    return fits_scaled(source, target, 14, 4);
}

bool fits_adr(uint64_t source, uint64_t target)
{
    return fits_scaled(source, target, 21, 1);
}

bool fits_adrp(uint64_t source, uint64_t target)
{
    return fits_scaled(source & ~0xfffULL, target & ~0xfffULL, 21, 0x1000);
}

bool fits_ldr_literal(uint64_t source, uint64_t target)
{
    return fits_scaled(source, target, 19, 4);
}

uint32_t encode_b(uint64_t source, uint64_t target)
{
    int64_t immediate = require_scaled_delta(source, target, 26, 4, "B");
    return 0x14000000U | ((uint32_t)immediate & 0x03ffffffU);
}

uint32_t encode_bl(uint64_t source, uint64_t target)
{
    int64_t immediate = require_scaled_delta(source, target, 26, 4, "BL");
    return 0x94000000U | ((uint32_t)immediate & 0x03ffffffU);
}

uint32_t encode_b_cond(uint8_t condition, uint64_t source, uint64_t target)
{
    if (condition > 0xf)
        throw std::runtime_error("conditional branch has invalid condition");
    int64_t immediate = require_scaled_delta(source, target, 19, 4, "B.cond");
    return 0x54000000U | (((uint32_t)immediate & 0x7ffffU) << 5) | condition;
}

uint32_t encode_cbz(bool nonzero, bool sf, uint8_t rt,
                    uint64_t source, uint64_t target)
{
    if (rt > 31)
        throw std::runtime_error("CBZ has invalid register");
    int64_t immediate = require_scaled_delta(source, target, 19, 4, "CBZ");
    return 0x34000000U | (sf ? 0x80000000U : 0) |
           (nonzero ? 0x01000000U : 0) |
           (((uint32_t)immediate & 0x7ffffU) << 5) | rt;
}

uint32_t encode_tbz(bool nonzero, uint8_t bit, uint8_t rt,
                    uint64_t source, uint64_t target)
{
    if (bit > 63 || rt > 31)
        throw std::runtime_error("TBZ has invalid register or bit");
    int64_t immediate = require_scaled_delta(source, target, 14, 4, "TBZ");
    return 0x36000000U | (nonzero ? 0x01000000U : 0) |
           ((uint32_t)(bit >> 5) << 31) | ((uint32_t)(bit & 31) << 19) |
           (((uint32_t)immediate & 0x3fffU) << 5) | rt;
}

uint32_t encode_adr(uint8_t rd, uint64_t source, uint64_t target)
{
    if (rd > 31)
        throw std::runtime_error("ADR has invalid destination register");
    int64_t immediate = require_scaled_delta(source, target, 21, 1, "ADR");
    uint32_t value = (uint32_t)immediate & 0x1fffffU;
    return 0x10000000U | ((value & 3U) << 29) |
           (((value >> 2) & 0x7ffffU) << 5) | rd;
}

uint32_t encode_adrp(uint8_t rd, uint64_t source, uint64_t target)
{
    if (rd > 31)
        throw std::runtime_error("ADRP has invalid destination register");
    int64_t immediate = require_scaled_delta(source & ~0xfffULL,
                                             target & ~0xfffULL,
                                             21, 0x1000, "ADRP");
    uint32_t value = (uint32_t)immediate & 0x1fffffU;
    return 0x90000000U | ((value & 3U) << 29) |
           (((value >> 2) & 0x7ffffU) << 5) | rd;
}

uint32_t encode_add_imm(uint8_t rd, uint8_t rn, uint16_t immediate)
{
    if (rd > 31 || rn > 31 || immediate > 0xfff)
        throw std::runtime_error("ADD immediate has invalid operand");
    return 0x91000000U | ((uint32_t)immediate << 10) |
           ((uint32_t)rn << 5) | rd;
}

uint32_t encode_movz(uint8_t rd, uint16_t immediate, uint8_t halfword)
{
    if (rd > 31 || halfword > 3)
        throw std::runtime_error("MOVZ has invalid operand");
    return 0xd2800000U | ((uint32_t)halfword << 21) |
           ((uint32_t)immediate << 5) | rd;
}

uint32_t encode_movk(uint8_t rd, uint16_t immediate, uint8_t halfword)
{
    if (rd > 31 || halfword > 3)
        throw std::runtime_error("MOVK has invalid operand");
    return 0xf2800000U | ((uint32_t)halfword << 21) |
           ((uint32_t)immediate << 5) | rd;
}

uint32_t encode_br(uint8_t rn)
{
    if (rn > 31)
        throw std::runtime_error("BR has invalid register");
    return 0xd61f0000U | ((uint32_t)rn << 5);
}

uint32_t encode_blr(uint8_t rn)
{
    if (rn > 31)
        throw std::runtime_error("BLR has invalid register");
    return 0xd63f0000U | ((uint32_t)rn << 5);
}

uint32_t encode_ldr_literal(uint32_t instruction,
                            uint64_t source, uint64_t target)
{
    int64_t immediate = require_scaled_delta(source, target, 19, 4,
                                             "LDR literal");
    return (instruction & ~0x00ffffe0U) |
           (((uint32_t)immediate & 0x7ffffU) << 5);
}

uint32_t encode_ldr_register(uint32_t instruction, uint8_t rn)
{
    if (rn > 31)
        throw std::runtime_error("LDR has invalid base register");
    uint32_t base = instruction & 0xff000000U;
    uint32_t load = 0;
    switch (base)
    {
    case 0x18000000U: load = 0xb9400000U; break;
    case 0x58000000U: load = 0xf9400000U; break;
    case 0x98000000U: load = 0xb9800000U; break;
    case 0x1c000000U: load = 0xbd400000U; break;
    case 0x5c000000U: load = 0xfd400000U; break;
    case 0x9c000000U: load = 0x3dc00000U; break;
    case 0xd8000000U:
    case 0xdc000000U: load = 0xf9800000U; break;
    default:
        throw std::runtime_error("unsupported LDR literal relocation");
    }
    return load | ((uint32_t)rn << 5) | (instruction & 0x1fU);
}

std::vector<uint8_t> make_load_immediate(uint8_t rd, uint64_t value)
{
    std::vector<uint8_t> out;
    append_word(out, encode_movz(rd, (uint16_t)value, 0));
    append_word(out, encode_movk(rd, (uint16_t)(value >> 16), 1));
    append_word(out, encode_movk(rd, (uint16_t)(value >> 32), 2));
    append_word(out, encode_movk(rd, (uint16_t)(value >> 48), 3));
    return out;
}

std::vector<uint8_t> make_address_sequence(uint64_t source, uint64_t target,
                                            uint8_t rd)
{
    if (rd > 31)
        throw std::runtime_error("address sequence has invalid register");
    return make_address_sequence_impl(source, target, rd);
}

std::size_t address_sequence_size(uint64_t source, uint64_t target)
{
    return fits_adrp(source, target) ? 8 : 16;
}

std::vector<uint8_t> make_branch_sequence(uint64_t source, uint64_t target,
                                          bool link, uint8_t scratch)
{
    if (source & 3U || target & 3U)
        throw std::runtime_error("branch target must be 4-byte aligned");
    std::vector<uint8_t> out;
    if (fits_branch26(source, target))
    {
        append_word(out, link ? encode_bl(source, target)
                              : encode_b(source, target));
        return out;
    }
    out = make_address_sequence(source, target, scratch);
    append_word(out, link ? encode_blr(scratch) : encode_br(scratch));
    return out;
}

std::size_t branch_sequence_size(uint64_t source, uint64_t target,
                                 bool link, uint8_t scratch)
{
    (void)link;
    (void)scratch;
    return fits_branch26(source, target) ? 4 :
           (address_sequence_size(source, target) + 4);
}

}
}
