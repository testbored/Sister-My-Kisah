#include "ElfAnalyzer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {

std::string trim(std::string value) {
    const auto isNotSpace = [](unsigned char character) { return !std::isspace(character); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), isNotSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), isNotSpace).base(), value.end());
    return value;
}

std::string shellQuote(const std::string& value) {
    std::string result = "'";
    for (const char character : value) result += character == '\'' ? "'\\''" : std::string(1, character);
    return result + "'";
}

std::string runCommand(const std::string& command) {
    std::array<char, 4096> buffer{};
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) return output;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) output += buffer.data();
    pclose(pipe);
    return output;
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.rfind(prefix, 0) == 0;
}

}  // namespace

bool ElfAnalyzer::open(const std::string& path, std::string& message) {
    functions_.clear();
    functionIndexByAddress_.clear();
    path_ = path;

    if (!isSupportedElf(path, message)) return false;
    parseSymbols();
    parseDisassembly();
    if (functions_.empty()) {
        message = "Tidak menemukan simbol fungsi. Kompilasi tanpa strip simbol.";
        return false;
    }
    message = "Analisis selesai: " + std::to_string(functions_.size()) + " fungsi.";
    return true;
}

const std::vector<Function>& ElfAnalyzer::functions() const {
    return functions_;
}

bool ElfAnalyzer::isSupportedElf(const std::string& path, std::string& message) const {
    std::ifstream file(path, std::ios::binary);
    std::array<unsigned char, 20> header{};
    if (!file.read(reinterpret_cast<char*>(header.data()), header.size())) {
        message = "Tidak dapat membaca file.";
        return false;
    }
    if (std::memcmp(header.data(), "\x7f" "ELF", 4) != 0) {
        message = "File bukan ELF.";
        return false;
    }
    if (header[4] != 2 || header[5] != 1 || header[18] != 0x3e || header[19] != 0) {
        message = "Dibutuhkan ELF 64-bit x86-64 little-endian.";
        return false;
    }
    return true;
}

void ElfAnalyzer::parseSymbols() {
    const std::string command = "nm -n -S --defined-only --demangle " + shellQuote(path_) + " 2>/dev/null";
    std::istringstream lines(runCommand(command));
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream row(line);
        std::string addressText, sizeText, symbolType;
        Function function;
        if (!(row >> addressText >> sizeText >> symbolType) || symbolType.size() != 1) continue;
        if (symbolType[0] != 'T' && symbolType[0] != 't') continue;

        std::getline(row, function.name);
        function.name = trim(function.name);
        try {
            function.address = std::stoul(addressText, nullptr, 16);
            function.size = std::stoul(sizeText, nullptr, 16);
        } catch (...) {
            continue;
        }
        if (function.size == 0 || function.name.empty()) continue;
        functionIndexByAddress_[function.address] = functions_.size();
        functions_.push_back(std::move(function));
    }
}

void ElfAnalyzer::parseDisassembly() {
    const std::string command = "objdump -d -Mintel --demangle --no-show-raw-insn " + shellQuote(path_) + " 2>/dev/null";
    std::istringstream lines(runCommand(command));
    std::string line;
    Function* currentFunction = nullptr;
    while (std::getline(lines, line)) {
        const auto nameBegin = line.find('<');
        const auto nameEnd = line.find(">:");
        if (nameBegin != std::string::npos && nameEnd != std::string::npos) {
            try {
                const unsigned long address = std::stoul(trim(line.substr(0, nameBegin)), nullptr, 16);
                const auto found = functionIndexByAddress_.find(address);
                currentFunction = found == functionIndexByAddress_.end() ? nullptr : &functions_[found->second];
            } catch (...) {
                currentFunction = nullptr;
            }
            continue;
        }
        if (currentFunction == nullptr) continue;

        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string address = trim(line.substr(0, colon));
        const std::string text = trim(line.substr(colon + 1));
        if (address.empty() || !std::isxdigit(static_cast<unsigned char>(address.front())) || text.empty()) continue;
        currentFunction->instructions.push_back({address, "", text});
        if (startsWith(text, "call")) {
            const std::string name = callTarget(text);
            if (!name.empty()) currentFunction->calls.push_back(name);
        }
    }
}

std::string ElfAnalyzer::pseudocode(const Function& function) const {
    std::ostringstream output;
    output << "// Rekonstruksi heuristik - ELF x86-64 System V AMD64 ABI\n";
    output << "// Address: 0x" << std::hex << function.address << ", size: " << std::dec << function.size << " bytes\n";
    output << "long " << safeCppName(function.name) << "() {\n";
    bool hasReturn = false;
    for (const Instruction& instruction : function.instructions) {
        const std::string& text = instruction.text;
        if (startsWith(text, "call")) {
            const std::string callee = callTarget(text);
            output << "    " << (callee.empty() ? "/* indirect call */" : safeCppName(callee) + "()") << ";\n";
        } else if (startsWith(text, "je ") || startsWith(text, "jz ")) {
            output << "    if (condition == 0) goto label_" << branchTarget(text) << ";\n";
        } else if (startsWith(text, "jne ") || startsWith(text, "jnz ")) {
            output << "    if (condition != 0) goto label_" << branchTarget(text) << ";\n";
        } else if (startsWith(text, "jg ") || startsWith(text, "ja ")) {
            output << "    if (condition > 0) goto label_" << branchTarget(text) << ";\n";
        } else if (startsWith(text, "jl ") || startsWith(text, "jb ")) {
            output << "    if (condition < 0) goto label_" << branchTarget(text) << ";\n";
        } else if (startsWith(text, "jmp ")) {
            output << "    goto label_" << branchTarget(text) << ";\n";
        } else if (text == "ret" || text == "retq") {
            output << "    return result;\n";
            hasReturn = true;
        }
    }
    if (!hasReturn) output << "    return result;\n";
    output << "}\n\n// Detail operasi tersedia pada panel Assembly.";
    return output.str();
}

std::string ElfAnalyzer::safeCppName(std::string name) {
    if (name.empty()) return "unknown_function";
    for (char& character : name) {
        if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_') character = '_';
    }
    if (std::isdigit(static_cast<unsigned char>(name.front()))) name.insert(name.begin(), '_');
    return name;
}

std::string ElfAnalyzer::branchTarget(const std::string& instruction) {
    const auto begin = instruction.find("0x");
    if (begin == std::string::npos) return "unknown";
    const auto end = instruction.find_first_of(" <", begin);
    return instruction.substr(begin + 2, end == std::string::npos ? std::string::npos : end - begin - 2);
}

std::string ElfAnalyzer::callTarget(const std::string& instruction) {
    const auto begin = instruction.find('<');
    const auto end = instruction.find('>', begin);
    if (begin == std::string::npos || end == std::string::npos) return "";
    return instruction.substr(begin + 1, end - begin - 1);
}
