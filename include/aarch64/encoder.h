#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace armcave {
namespace aarch64 {

constexpr std::size_t kMaxBranchSequenceBytes = 20;

bool fits_branch26(uint64_t source, uint64_t target);
bool fits_branch19(uint64_t source, uint64_t target);
bool fits_branch14(uint64_t source, uint64_t target);
bool fits_adr(uint64_t source, uint64_t target);
bool fits_adrp(uint64_t source, uint64_t target);
bool fits_ldr_literal(uint64_t source, uint64_t target);

uint32_t encode_b(uint64_t source, uint64_t target);
uint32_t encode_bl(uint64_t source, uint64_t target);
uint32_t encode_b_cond(uint8_t condition, uint64_t source, uint64_t target);
uint32_t encode_cbz(bool nonzero, bool sf, uint8_t rt,
                    uint64_t source, uint64_t target);
uint32_t encode_tbz(bool nonzero, uint8_t bit, uint8_t rt,
                    uint64_t source, uint64_t target);
uint32_t encode_adr(uint8_t rd, uint64_t source, uint64_t target);
uint32_t encode_adrp(uint8_t rd, uint64_t source, uint64_t target);
uint32_t encode_add_imm(uint8_t rd, uint8_t rn, uint16_t immediate);
uint32_t encode_movz(uint8_t rd, uint16_t immediate, uint8_t halfword);
uint32_t encode_movk(uint8_t rd, uint16_t immediate, uint8_t halfword);
uint32_t encode_br(uint8_t rn);
uint32_t encode_blr(uint8_t rn);

uint32_t encode_ldr_literal(uint32_t instruction,
                            uint64_t source, uint64_t target);
uint32_t encode_ldr_register(uint32_t literal_instruction, uint8_t rn);

std::vector<uint8_t> make_load_immediate(uint8_t rd, uint64_t value);
std::vector<uint8_t> make_address_sequence(uint64_t source, uint64_t target,
                                            uint8_t rd);
std::size_t address_sequence_size(uint64_t source, uint64_t target);

std::vector<uint8_t> make_branch_sequence(uint64_t source, uint64_t target,
                                          bool link, uint8_t scratch = 16);
std::size_t branch_sequence_size(uint64_t source, uint64_t target,
                                 bool link, uint8_t scratch = 16);

}
}
