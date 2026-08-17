#pragma once

#include "Models.hpp"

#include <map>
#include <string>
#include <vector>

// Reads symbols and Intel disassembly using the installed GNU binutils tools.
class ElfAnalyzer {
public:
    bool open(const std::string& path, std::string& message);
    const std::vector<Function>& functions() const;
    std::string pseudocode(const Function& function) const;

private:
    bool isSupportedElf(const std::string& path, std::string& message) const;
    void parseSymbols();
    void parseDisassembly();

    static std::string safeCppName(std::string name);
    static std::string branchTarget(const std::string& instruction);
    static std::string callTarget(const std::string& instruction);

    std::string path_;
    std::vector<Function> functions_;
    std::map<unsigned long, std::size_t> functionIndexByAddress_;
};
