#include "aarch64/relocator.h"

#include "aarch64/decoder.h"
#include "aarch64/encoder.h"

#include <cstring>
#include <stdexcept>

namespace armcave {
namespace aarch64 {

namespace {

struct WorkItem {
    DecodedInstruction instruction;
    uint64_t new_address = 0;
    std::size_t output_size = 4;
};

uint32_t read_word(const std::vector<uint8_t> &data, std::size_t offset)
{
    uint32_t value = 0;
    memcpy(&value, data.data() + offset, sizeof(value));
    return value;
}

void append_word(std::vector<uint8_t> &out, uint32_t instruction)
{
    out.push_back((uint8_t)(instruction & 0xffU));
    out.push_back((uint8_t)((instruction >> 8) & 0xffU));
    out.push_back((uint8_t)((instruction >> 16) & 0xffU));
    out.push_back((uint8_t)((instruction >> 24) & 0xffU));
}

uint64_t remap_target(const std::vector<WorkItem> &items,
                      uint64_t source_address, uint64_t target)
{
    if (target < source_address)
        return target;
    uint64_t offset = target - source_address;
    if (offset >= (uint64_t)items.size() * 4 || (offset & 3U))
        return target;
    return items[(std::size_t)(offset / 4)].new_address;
}

uint8_t alternate_scratch(uint8_t requested, uint8_t used_register)
{
    if (requested != used_register)
        return requested;
    if (requested == 16 || used_register == 16)
        return 17;
    return 16;
}

std::size_t item_size(const WorkItem &item, uint64_t target,
                      const RelocationOptions &options)
{
    const auto &insn = item.instruction;
    switch (insn.kind)
    {
    case InstructionKind::B:
    case InstructionKind::BL:
        return branch_sequence_size(item.new_address, target,
                                    insn.kind == InstructionKind::BL,
                                    options.scratch_register);
    case InstructionKind::BCond:
        if (fits_branch19(item.new_address, target))
            return 4;
        return 4 + branch_sequence_size(item.new_address + 4, target, false,
                                        options.scratch_register);
    case InstructionKind::CBZ:
    case InstructionKind::CBNZ:
        if (fits_branch19(item.new_address, target))
            return 4;
        return 4 + branch_sequence_size(item.new_address + 4, target, false,
                                        options.scratch_register);
    case InstructionKind::TBZ:
    case InstructionKind::TBNZ:
        if (fits_branch14(item.new_address, target))
            return 4;
        return 4 + branch_sequence_size(item.new_address + 4, target, false,
                                        options.scratch_register);
    case InstructionKind::ADR:
        return fits_adr(item.new_address, target) ? 4 : 16;
    case InstructionKind::ADRP:
        return fits_adrp(item.new_address, target) ? 4 : 16;
    case InstructionKind::LdrLiteral:
        if (fits_ldr_literal(item.new_address, target))
            return 4;
        if (insn.literal_kind == LiteralKind::Unknown)
            throw std::runtime_error("unsupported PC-relative literal instruction");
        return address_sequence_size(item.new_address, target) + 4;
    case InstructionKind::Other:
        return 4;
    }
    return 4;
}

void append_long_conditional(std::vector<uint8_t> &out,
                             const WorkItem &item, uint64_t target,
                             uint8_t scratch)
{
    std::vector<uint8_t> branch = make_branch_sequence(
        item.new_address + 4, target, false, scratch);
    uint64_t skip = item.new_address + 4 + branch.size();
    const auto &insn = item.instruction;
    if (insn.kind == InstructionKind::BCond)
    {
        if (insn.condition >= 14)
            throw std::runtime_error("cannot invert AL/NV conditional branch");
        append_word(out, encode_b_cond(insn.condition ^ 1,
                                       item.new_address, skip));
    }
    else if (insn.kind == InstructionKind::CBZ ||
             insn.kind == InstructionKind::CBNZ)
    {
        append_word(out, encode_cbz(!insn.nonzero, insn.sf,
                                    insn.register_index, item.new_address, skip));
    }
    else
    {
        append_word(out, encode_tbz(!insn.nonzero, insn.bit,
                                    insn.register_index, item.new_address, skip));
    }
    out.insert(out.end(), branch.begin(), branch.end());
}

uint8_t literal_scratch(const WorkItem &item, uint8_t requested)
{
    return alternate_scratch(requested, item.instruction.register_index);
}

void emit_item(std::vector<uint8_t> &out, const WorkItem &item,
               uint64_t target, const RelocationOptions &options)
{
    const auto &insn = item.instruction;
    switch (insn.kind)
    {
    case InstructionKind::B:
    case InstructionKind::BL:
    {
        auto branch = make_branch_sequence(
            item.new_address, target, insn.kind == InstructionKind::BL,
            options.scratch_register);
        out.insert(out.end(), branch.begin(), branch.end());
        break;
    }
    case InstructionKind::BCond:
    case InstructionKind::CBZ:
    case InstructionKind::CBNZ:
    case InstructionKind::TBZ:
    case InstructionKind::TBNZ:
        if ((insn.kind == InstructionKind::BCond &&
             fits_branch19(item.new_address, target)) ||
            ((insn.kind == InstructionKind::CBZ ||
              insn.kind == InstructionKind::CBNZ) &&
             fits_branch19(item.new_address, target)) ||
            ((insn.kind == InstructionKind::TBZ ||
              insn.kind == InstructionKind::TBNZ) &&
             fits_branch14(item.new_address, target)))
        {
            if (insn.kind == InstructionKind::BCond)
                append_word(out, encode_b_cond(insn.condition,
                                               item.new_address, target));
            else if (insn.kind == InstructionKind::CBZ ||
                     insn.kind == InstructionKind::CBNZ)
                append_word(out, encode_cbz(insn.nonzero, insn.sf,
                                            insn.register_index,
                                            item.new_address, target));
            else
                append_word(out, encode_tbz(insn.nonzero, insn.bit,
                                            insn.register_index,
                                            item.new_address, target));
        }
        else
        {
            append_long_conditional(out, item, target, options.scratch_register);
        }
        break;
    case InstructionKind::ADR:
        if (fits_adr(item.new_address, target))
            append_word(out, encode_adr(insn.register_index,
                                        item.new_address, target));
        else
        {
            auto value = make_load_immediate(insn.register_index, target);
            out.insert(out.end(), value.begin(), value.end());
        }
        break;
    case InstructionKind::ADRP:
        if (fits_adrp(item.new_address, target))
            append_word(out, encode_adrp(insn.register_index,
                                         item.new_address, target));
        else
        {
            auto value = make_load_immediate(insn.register_index,
                                             target & ~0xfffULL);
            out.insert(out.end(), value.begin(), value.end());
        }
        break;
    case InstructionKind::LdrLiteral:
        if (fits_ldr_literal(item.new_address, target))
            append_word(out, encode_ldr_literal(insn.encoding,
                                                item.new_address, target));
        else
        {
            uint8_t scratch = literal_scratch(item, options.scratch_register);
            auto address = make_address_sequence(item.new_address, target,
                                                  scratch);
            out.insert(out.end(), address.begin(), address.end());
            append_word(out, encode_ldr_register(insn.encoding, scratch));
        }
        break;
    case InstructionKind::Other:
        append_word(out, insn.encoding);
        break;
    }
}

}

RelocationResult relocate_block(const std::vector<uint8_t> &code,
                                uint64_t source_address,
                                uint64_t destination_address,
                                const RelocationOptions &options)
{
    if (source_address & 3U || destination_address & 3U)
        throw std::runtime_error("relocation addresses must be 4-byte aligned");
    if (code.size() % 4)
        throw std::runtime_error("relocation input must contain whole instructions");
    if (options.scratch_register > 30)
        throw std::runtime_error("relocation scratch register must be x0-x30");

    std::vector<WorkItem> items;
    items.reserve(code.size() / 4);
    for (std::size_t offset = 0; offset < code.size(); offset += 4)
    {
        WorkItem item;
        item.instruction = decode(read_word(code, offset), source_address + offset);
        items.push_back(item);
    }

    bool stable = false;
    for (std::size_t iteration = 0; iteration < items.size() + 4; ++iteration)
    {
        uint64_t cursor = destination_address;
        for (auto &item : items)
        {
            item.new_address = cursor;
            cursor += item.output_size;
        }

        stable = true;
        for (auto &item : items)
        {
            uint64_t target = item.instruction.has_target
                ? remap_target(items, source_address, item.instruction.target)
                : 0;
            std::size_t size = item_size(item, target, options);
            if (size != item.output_size)
            {
                item.output_size = size;
                stable = false;
            }
        }
        if (stable)
            break;
    }
    if (!stable)
        throw std::runtime_error("AArch64 relocation layout did not converge");

    uint64_t cursor = destination_address;
    for (auto &item : items)
    {
        item.new_address = cursor;
        cursor += item.output_size;
    }

    RelocationResult result;
    result.instruction_count = items.size();
    result.bytes.reserve((std::size_t)(cursor - destination_address));
    for (auto &item : items)
    {
        uint64_t target = item.instruction.has_target
            ? remap_target(items, source_address, item.instruction.target)
            : 0;
        std::size_t before = result.bytes.size();
        emit_item(result.bytes, item, target, options);
        if (result.bytes.size() - before != item.output_size)
            throw std::runtime_error("AArch64 relocator emitted an invalid size");
    }
    return result;
}

std::size_t max_relocated_size(std::size_t input_size)
{
    if (input_size % 4)
        throw std::runtime_error("relocation size must be instruction aligned");
    return (input_size / 4) * 24;
}

}
}
