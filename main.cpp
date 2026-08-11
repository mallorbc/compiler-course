#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <system_error>
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

} // namespace

int main(int argc, char *argv[])
{
    const bool normal_check_only = argc == 2;
    const bool emit_c = argc == 4 && std::string(argv[1]) == "--emit-c";
    //The legacy one-argument form is intentionally byte-for-byte unchanged.
    if (!normal_check_only && !emit_c)
    {
        std::cout << "Error!\nUsage: " << argv[0] << " <file to compile>\n";
        return 1;
    }

    const std::string source_arg = emit_c ? argv[3] : argv[1];
    const std::filesystem::path source_path(source_arg);
    const std::filesystem::path output_path = emit_c ?
        std::filesystem::path(argv[2]) : std::filesystem::path();
    if (emit_c && paths_alias(source_path, output_path))
    {
        std::cerr << "codegen: io-error: output aliases source\n";
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

    parser *file_parser;
    file_parser = new parser(source_arg);

    //scanner *first_scan;
    //first_scan = new scanner(source_arg);
    //first_scan->test();
    if (file_parser->error_count() > 0)
    {
        if (emit_c)
        {
            std::cerr << "codegen: frontend-error\n";
        }
        return 1;
    }
    if (!emit_c)
    {
        return 0;
    }
    if (file_parser->ir_status() != ir::ModuleStatus::Ready)
    {
        const char *status = file_parser->ir_status() == ir::ModuleStatus::Unsupported ?
                                 "unsupported" : "invalid-ir";
        std::cerr << "codegen: " << status;
        if (!file_parser->ir_reason().empty())
        {
            std::cerr << ": " << file_parser->ir_reason();
        }
        std::cerr << "\n";
        return 1;
    }
    RestrictedCEmitter emitter;
    const RestrictedCResult emitted = emitter.emit_to_file(file_parser->ir_module(), output_path);
    if (!emitted.succeeded())
    {
        std::cerr << "codegen: " << codegen_status_name(emitted.status);
        if (!emitted.diagnostic.empty())
        {
            std::cerr << ": " << emitted.diagnostic;
        }
        std::cerr << "\n";
        return 1;
    }
    return 0;
}
