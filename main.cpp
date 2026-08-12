#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <system_error>
#include <cstdlib>
#include "IRPrinter.h"
#include "NativeToolchain.h"
#include "RestrictedCEmitter.h"
#include "scanner.h"
#include "token.h"
#include "SymbolTable.h"
#include "parser.h"

namespace
{

bool paths_alias(const std::filesystem::path &source, const std::filesystem::path &output)
{
    std::error_code error;
    if (std::filesystem::exists(output, error) && !error &&
        std::filesystem::equivalent(source, output, error) && !error)
    {
        return true;
    }
    error.clear();
    const std::filesystem::path source_path = std::filesystem::weakly_canonical(source, error);
    if (error)
    {
        return false;
    }
    error.clear();
    const std::filesystem::path output_path = std::filesystem::weakly_canonical(output, error);
    return !error && source_path == output_path;
}

const char *codegen_status_name(RestrictedCStatus status)
{
    switch (status)
    {
    case RestrictedCStatus::Success: return "success";
    case RestrictedCStatus::Unsupported: return "unsupported";
    case RestrictedCStatus::InvalidIR: return "invalid-ir";
    case RestrictedCStatus::IoError: return "io-error";
    }
    return "invalid-ir";
}

const char *native_status_name(NativeToolchainStatus status)
{
    switch (status)
    {
    case NativeToolchainStatus::Success: return "success";
    case NativeToolchainStatus::InvalidInput: return "invalid-input";
    case NativeToolchainStatus::IoError: return "io-error";
    case NativeToolchainStatus::LaunchError: return "launch-error";
    case NativeToolchainStatus::CompilerError: return "compiler-error";
    }
    return "invalid-input";
}

const char *ir_print_status_name(IRPrintStatus status)
{
    switch (status)
    {
    case IRPrintStatus::Success: return "success";
    case IRPrintStatus::InvalidIR: return "invalid-ir";
    case IRPrintStatus::IoError: return "io-error";
    }
    return "invalid-ir";
}

} // namespace

int main(int argc, char *argv[])
{
    const std::string first_argument = argc > 1 ? argv[1] : std::string();
    const bool reserved_option = first_argument == "--emit-ir" ||
        first_argument == "--emit-c" ||
        first_argument == "-o" || first_argument == "--output" ||
        first_argument == "--native";
    const bool normal_check_only = argc == 2 && !reserved_option;
    const bool emit_ir = argc == 4 && first_argument == "--emit-ir";
    const bool emit_c = argc == 4 && first_argument == "--emit-c";
    const bool emit_native = argc == 4 &&
        (first_argument == "-o" || first_argument == "--output" ||
         first_argument == "--native");
    //The legacy one-argument form is intentionally byte-for-byte unchanged.
    if (!normal_check_only && !emit_ir && !emit_c && !emit_native)
    {
        std::cout << "Error!\nUsage: " << argv[0] << " <file to compile>\n";
        return 1;
    }

    const std::string source_arg = (emit_ir || emit_c || emit_native) ? argv[3] : argv[1];
    const std::filesystem::path source_path(source_arg);
    const std::filesystem::path output_path = (emit_ir || emit_c || emit_native) ?
        std::filesystem::path(argv[2]) : std::filesystem::path();
    if ((emit_ir || emit_c || emit_native) && paths_alias(source_path, output_path))
    {
        std::cerr << (emit_native ? "native" : emit_ir ? "ir" : "codegen")
                  << ": io-error: output aliases source\n";
        return 1;
    }

    std::error_code path_error;
    if (!std::filesystem::is_regular_file(source_path, path_error))
    {
        std::cout << "Error!\nUnable to open source file: " << source_arg << "\n";
        return 1;
    }

    std::ifstream source_file(source_arg);
    if (!source_file.is_open())
    {
        std::cout << "Error!\nUnable to open source file: " << source_arg << "\n";
        return 1;
    }
    source_file.close();

    parser file_parser(source_arg);

    //scanner *first_scan;
    //first_scan = new scanner(source_arg);
    //first_scan->test();
    if (file_parser.error_count() > 0)
    {
        if (emit_ir || emit_c || emit_native)
        {
            std::cerr << (emit_native ? "native" : emit_ir ? "ir" : "codegen")
                      << ": frontend-error\n";
        }
        return 1;
    }
    if (!emit_ir && !emit_c && !emit_native)
    {
        return 0;
    }
    if (file_parser.ir_status() != ir::ModuleStatus::Ready)
    {
        const char *status = file_parser.ir_status() == ir::ModuleStatus::Unsupported ?
                                 "unsupported" : "invalid-ir";
        std::cerr << (emit_native ? "native" : emit_ir ? "ir" : "codegen")
                  << ": " << status;
        if (!file_parser.ir_reason().empty())
        {
            std::cerr << ": " << file_parser.ir_reason();
        }
        std::cerr << "\n";
        return 1;
    }
    if (emit_ir)
    {
        IRPrinter printer;
        const IRPrintResult printed = printer.print_to_file(file_parser.ir_module(), output_path);
        if (!printed.succeeded())
        {
            std::cerr << "ir: " << ir_print_status_name(printed.status);
            if (!printed.diagnostic.empty())
            {
                std::cerr << ": " << printed.diagnostic;
            }
            std::cerr << "\n";
            return 1;
        }
        return 0;
    }
    RestrictedCEmitter emitter;
    const RestrictedCResult emitted = emit_native ? emitter.emit(file_parser.ir_module()) :
        emitter.emit_to_file(file_parser.ir_module(), output_path);
    if (emit_native && file_parser.Lexer != nullptr)
    {
        file_parser.Lexer->source.close();
    }
    if (!emitted.succeeded())
    {
        std::cerr << (emit_native ? "native" : "codegen") << ": "
                  << codegen_status_name(emitted.status);
        if (!emitted.diagnostic.empty())
        {
            std::cerr << ": " << emitted.diagnostic;
        }
        std::cerr << "\n";
        return 1;
    }
    if (emit_native)
    {
        const char *configured_compiler = std::getenv("CC");
        const std::string host_compiler = configured_compiler == nullptr ||
            configured_compiler[0] == '\0' ? "cc" : configured_compiler;
        NativeToolchain toolchain;
        const NativeToolchainResult native = toolchain.compile(
            emitted, source_path, output_path, host_compiler);
        if (!native.succeeded())
        {
            std::cerr << "native: " << native_status_name(native.status);
            if (!native.diagnostic.empty())
            {
                std::cerr << ": " << native.diagnostic;
            }
            std::cerr << "\n";
            return 1;
        }
    }
    return 0;
}
