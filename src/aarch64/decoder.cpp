#include "aarch64/decoder.h"

namespace armcave {
namespace aarch64 {

namespace {

int64_t sign_extend(uint64_t value, unsigned bits)
{
    const uint64_t sign = 1ULL << (bits - 1);
    const uint64_t mask = (1ULL << bits) - 1;
    value &= mask;
    return (int64_t)((value ^ sign) - sign);
}

uint64_t add_signed(uint64_t address, int64_t delta)
{
    return delta >= 0 ? address + (uint64_t)delta
                      : address - (uint64_t)(-delta);
}

}

DecodedInstruction decode(uint32_t encoding, uint64_t address)
{
    DecodedInstruction result;
    result.encoding = encoding;
    result.address = address;

    if ((encoding & 0xfc000000U) == 0x14000000U)
    {
        result.kind = InstructionKind::B;
        result.has_target = true;
        result.target = add_signed(address,
            sign_extend(encoding & 0x03ffffffU, 26) * 4);
        return result;
    }
    if ((encoding & 0xfc000000U) == 0x94000000U)
    {
        result.kind = InstructionKind::BL;
        result.has_target = true;
        result.target = add_signed(address,
            sign_extend(encoding & 0x03ffffffU, 26) * 4);
        return result;
    }
    if ((encoding & 0xff000010U) == 0x54000000U)
    {
        result.kind = InstructionKind::BCond;
        result.condition = (uint8_t)(encoding & 0xfU);
        result.has_target = true;
        result.target = add_signed(address,
            sign_extend((encoding >> 5) & 0x7ffffU, 19) * 4);
        return result;
    }
    if ((encoding & 0x7f000000U) == 0x34000000U ||
        (encoding & 0x7f000000U) == 0x35000000U)
    {
        result.kind = (encoding & 0x01000000U) ? InstructionKind::CBNZ
                                               : InstructionKind::CBZ;
        result.nonzero = (encoding & 0x01000000U) != 0;
        result.sf = (encoding & 0x80000000U) != 0;
        result.register_index = (uint8_t)(encoding & 0x1fU);
        result.has_target = true;
        result.target = add_signed(address,
            sign_extend((encoding >> 5) & 0x7ffffU, 19) * 4);
        return result;
    }
    if ((encoding & 0x7f000000U) == 0x36000000U ||
        (encoding & 0x7f000000U) == 0x37000000U)
    {
        result.kind = (encoding & 0x01000000U) ? InstructionKind::TBNZ
                                               : InstructionKind::TBZ;
        result.nonzero = (encoding & 0x01000000U) != 0;
        result.bit = (uint8_t)(((encoding >> 31) & 1U) * 32U +
                               ((encoding >> 19) & 0x1fU));
        result.register_index = (uint8_t)(encoding & 0x1fU);
        result.has_target = true;
        result.target = add_signed(address,
            sign_extend((encoding >> 5) & 0x3fffU, 14) * 4);
        return result;
    }
    if ((encoding & 0x9f000000U) == 0x10000000U)
    {
        result.kind = InstructionKind::ADR;
        result.register_index = (uint8_t)(encoding & 0x1fU);
        int64_t immediate = sign_extend(
            ((uint64_t)((encoding >> 29) & 3U)) |
            ((uint64_t)((encoding >> 5) & 0x7ffffU) << 2), 21);
        result.has_target = true;
        result.target = add_signed(address, immediate);
        return result;
    }
    if ((encoding & 0x9f000000U) == 0x90000000U)
    {
        result.kind = InstructionKind::ADRP;
        result.register_index = (uint8_t)(encoding & 0x1fU);
        int64_t immediate = sign_extend(
            ((uint64_t)((encoding >> 29) & 3U)) |
            ((uint64_t)((encoding >> 5) & 0x7ffffU) << 2), 21);
        result.has_target = true;
        result.target = add_signed(address & ~0xfffULL, immediate * 0x1000);
        return result;
    }

    switch (encoding & 0xff000000U)
    {
    case 0x18000000U:
        result.literal_kind = LiteralKind::W;
        break;
    case 0x58000000U:
        result.literal_kind = LiteralKind::X;
        break;
    case 0x98000000U:
        result.literal_kind = LiteralKind::WSignExtend;
        break;
    case 0x1c000000U:
        result.literal_kind = LiteralKind::S;
        break;
    case 0x5c000000U:
        result.literal_kind = LiteralKind::D;
        break;
    case 0x9c000000U:
        result.literal_kind = LiteralKind::Q;
        break;
    case 0xd8000000U:
    case 0xdc000000U:
        result.literal_kind = LiteralKind::Prefetch;
        break;
    default:
        return result;
    }

    result.kind = InstructionKind::LdrLiteral;
    result.register_index = (uint8_t)(encoding & 0x1fU);
    result.has_target = true;
    result.target = add_signed(address,
        sign_extend((encoding >> 5) & 0x7ffffU, 19) * 4);
    return result;
}

bool is_branch(InstructionKind kind)
{
    return kind == InstructionKind::B || kind == InstructionKind::BL ||
           kind == InstructionKind::BCond || kind == InstructionKind::CBZ ||
           kind == InstructionKind::CBNZ || kind == InstructionKind::TBZ ||
           kind == InstructionKind::TBNZ;
}

bool is_conditional_branch(InstructionKind kind)
{
    return kind == InstructionKind::BCond || kind == InstructionKind::CBZ ||
           kind == InstructionKind::CBNZ || kind == InstructionKind::TBZ ||
           kind == InstructionKind::TBNZ;
}

}
}
