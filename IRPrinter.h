#ifndef IR_PRINTER_H
#define IR_PRINTER_H

#include "IR.h"

#include <filesystem>
#include <string>

enum class IRPrintStatus
{
    Success,
    InvalidIR,
    IoError
};

struct IRPrintResult
{
    IRPrintStatus status = IRPrintStatus::InvalidIR;
    std::string diagnostic;
    std::string text;

    bool succeeded() const noexcept { return status == IRPrintStatus::Success; }
};

//A deterministic, human-readable view of finalized IR.  This is another
//backend consumer: it depends on IR only and has no scanner/parser knowledge.
class IRPrinter
{
public:
    IRPrintResult print(const ir::Module &module) const;
    IRPrintResult print_to_file(const ir::Module &module,
                                const std::filesystem::path &output) const;
};

#endif // IR_PRINTER_H
