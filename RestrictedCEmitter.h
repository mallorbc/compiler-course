#ifndef RESTRICTED_C_EMITTER_H
#define RESTRICTED_C_EMITTER_H

#include "IR.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

//This is deliberately a backend boundary: it consumes finalized IR only and
//does not know about scanner/parser tokens or frontend symbol tables.
enum class RestrictedCStatus
{
    Success,
    Unsupported,
    InvalidIR,
    IoError
};

struct RestrictedCResult
{
    RestrictedCStatus status = RestrictedCStatus::InvalidIR;
    std::string diagnostic;
    //Only a successful in-memory render exposes text.  File failures clear it
    //as well, so callers never observe a partial program.
    std::string text;

    bool succeeded() const noexcept { return status == RestrictedCStatus::Success; }
};

class RestrictedCEmitter
{
public:
    static constexpr std::size_t memory_byte_capacity() noexcept
    {
        return 64U * 1024U * 1024U;
    }
    static constexpr std::size_t memory_word_capacity() noexcept
    {
        return memory_byte_capacity() / sizeof(std::int32_t);
    }
    static constexpr bool storage_count_fits_memory(std::size_t count) noexcept
    {
        return count <= memory_word_capacity();
    }

    RestrictedCResult emit(const ir::Module &module) const;
    RestrictedCResult emit_to_file(const ir::Module &module,
                                   const std::filesystem::path &output) const;
};

#endif // RESTRICTED_C_EMITTER_H
