#ifndef NATIVE_TOOLCHAIN_H
#define NATIVE_TOOLCHAIN_H

#include "RestrictedCEmitter.h"

#include <filesystem>
#include <string>

enum class NativeToolchainStatus
{
    Success,
    InvalidInput,
    IoError,
    LaunchError,
    CompilerError
};

struct NativeToolchainResult
{
    NativeToolchainStatus status = NativeToolchainStatus::InvalidInput;
    std::string diagnostic;

    bool succeeded() const noexcept { return status == NativeToolchainStatus::Success; }
};

//This is the only production seam that knows how to turn emitted C into a
//native executable.  The frontend and restricted-C backend remain independent
//of host process and linker conventions.
class NativeToolchain
{
public:
    NativeToolchainResult compile(const RestrictedCResult &emitted,
                                  const std::filesystem::path &source,
                                  const std::filesystem::path &output,
                                  const std::string &host_compiler) const;
};

#endif // NATIVE_TOOLCHAIN_H
