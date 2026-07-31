#pragma once

#include <cstdint>

namespace armcave {
namespace aarch64 {

enum class InstructionKind {
    Other,
    B,
    BL,
    BCond,
    CBZ,
    CBNZ,
    TBZ,
    TBNZ,
    ADR,
    ADRP,
    LdrLiteral,
};

enum class LiteralKind {
    Unknown,
    W,
    X,
    WSignExtend,
    S,
    D,
    Q,
    Prefetch,
};

struct DecodedInstruction {
    uint32_t encoding = 0;
    uint64_t address = 0;
    InstructionKind kind = InstructionKind::Other;
    LiteralKind literal_kind = LiteralKind::Unknown;
    uint64_t target = 0;
    uint8_t register_index = 0;
    uint8_t condition = 0;
    uint8_t bit = 0;
    bool nonzero = false;
    bool sf = false;
    bool has_target = false;
};

DecodedInstruction decode(uint32_t encoding, uint64_t address);
bool is_branch(InstructionKind kind);
bool is_conditional_branch(InstructionKind kind);

}
}
