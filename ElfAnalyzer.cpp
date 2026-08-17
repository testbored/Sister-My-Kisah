#include "ElfAnalyzer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

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

std::vector<std::string> operands(const std::string& instruction) {
    const auto separator = instruction.find_first_of(" \t");
    if (separator == std::string::npos) return {};
    const std::string values = trim(instruction.substr(separator));
    const auto comma = values.find(',');
    if (comma == std::string::npos) return {values};
    return {trim(values.substr(0, comma)), trim(values.substr(comma + 1))};
}

std::string canonicalRegister(std::string value) {
    static const std::map<std::string, std::string> aliases = {
        {"eax", "rax"}, {"ax", "rax"}, {"al", "rax"}, {"ebx", "rbx"}, {"bx", "rbx"},
        {"ecx", "rcx"}, {"cx", "rcx"}, {"edx", "rdx"}, {"dx", "rdx"}, {"edi", "rdi"},
        {"esi", "rsi"}, {"ebp", "rbp"}, {"esp", "rsp"}, {"r8d", "r8"}, {"r9d", "r9"},
    };
    const auto found = aliases.find(value);
    return found == aliases.end() ? value : found->second;
}

std::string stackVariable(const std::string& operand) {
    const auto base = operand.find("[rbp");
    if (base == std::string::npos) return "";
    std::string suffix = operand.substr(base + 4);
    suffix.erase(std::remove_if(suffix.begin(), suffix.end(), [](char c) { return c == ']' || c == ' '; }), suffix.end());
    if (suffix.empty()) return "saved_rbp";
    std::replace(suffix.begin(), suffix.end(), '-', '_');
    std::replace(suffix.begin(), suffix.end(), '+', '_');
    return "local" + suffix;
}

std::string conditionFor(const std::string& mnemonic, const std::string& left, const std::string& right) {
    const std::string lhs = left.empty() ? "condition" : left;
    const std::string rhs = right.empty() ? "0" : right;
    if (mnemonic == "je" || mnemonic == "jz") return lhs + " == " + rhs;
    if (mnemonic == "jne" || mnemonic == "jnz") return lhs + " != " + rhs;
    if (mnemonic == "jg" || mnemonic == "ja") return lhs + " > " + rhs;
    if (mnemonic == "jge" || mnemonic == "jae") return lhs + " >= " + rhs;
    if (mnemonic == "jl" || mnemonic == "jb") return lhs + " < " + rhs;
    if (mnemonic == "jle" || mnemonic == "jbe") return lhs + " <= " + rhs;
    return "condition";
}

std::string invertedConditionFor(const std::string& mnemonic, const std::string& left, const std::string& right) {
    const std::string lhs = left.empty() ? "condition" : left;
    const std::string rhs = right.empty() ? "0" : right;
    if (mnemonic == "je" || mnemonic == "jz") return lhs + " != " + rhs;
    if (mnemonic == "jne" || mnemonic == "jnz") return lhs + " == " + rhs;
    if (mnemonic == "jg" || mnemonic == "ja") return lhs + " <= " + rhs;
    if (mnemonic == "jge" || mnemonic == "jae") return lhs + " < " + rhs;
    if (mnemonic == "jl" || mnemonic == "jb") return lhs + " >= " + rhs;
    if (mnemonic == "jle" || mnemonic == "jbe") return lhs + " > " + rhs;
    return "true";
}

unsigned long addressNumber(const std::string& address) {
    try {
        return std::stoul(address, nullptr, 16);
    } catch (...) {
        return 0;
    }
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
    output << "long " << safeCppName(function.name) << "(long arg1, long arg2, long arg3, long arg4) {\n";
    output << "    long result = 0;\n";

    std::unordered_map<std::string, std::string> value = {
        {"rdi", "arg1"}, {"rsi", "arg2"}, {"rdx", "arg3"}, {"rcx", "arg4"},
        {"r8", "arg5"}, {"r9", "arg6"}, {"rax", "result"},
    };
    std::string comparedLeft;
    std::string comparedRight;
    const auto expressionFor = [&value](const std::string& operand) {
        const std::string local = stackVariable(operand);
        if (!local.empty()) return local;
        const std::string registerName = canonicalRegister(operand);
        const auto found = value.find(registerName);
        return found == value.end() ? operand : found->second;
    };

    // A conditional branch to an earlier address is the x86-64 signature of a loop.
    // Keep its bounds so the linear disassembly can be emitted as a do/while block.
    std::map<std::size_t, std::size_t> loopEndForStart;
    std::map<std::size_t, std::size_t> loopStartForEnd;
    std::map<std::size_t, std::size_t> guardedLoopEnd;
    std::string loopCounterRegister;
    for (std::size_t index = 0; index < function.instructions.size(); ++index) {
        const Instruction& instruction = function.instructions[index];
        const auto opcodeEnd = instruction.text.find_first_of(" \t");
        const std::string opcode = opcodeEnd == std::string::npos ? instruction.text : instruction.text.substr(0, opcodeEnd);
        if (opcode.empty() || opcode[0] != 'j' || opcode == "jmp") continue;

        const unsigned long target = addressNumber(branchTarget(instruction.text));
        const unsigned long current = addressNumber(instruction.address);
        if (target >= current || target == 0) continue;
        for (std::size_t start = 0; start < index; ++start) {
            if (addressNumber(function.instructions[start].address) != target) continue;
            loopEndForStart[start] = index;
            loopStartForEnd[index] = start;
            if (index > 0 && startsWith(function.instructions[index - 1].text, "cmp")) {
                const std::vector<std::string> comparison = operands(function.instructions[index - 1].text);
                if (!comparison.empty()) loopCounterRegister = canonicalRegister(comparison[0]);
            }
            for (std::size_t guard = 0; guard < start; ++guard) {
                const auto guardEnd = function.instructions[guard].text.find_first_of(" \t");
                const std::string guardOpcode = guardEnd == std::string::npos ? function.instructions[guard].text
                                                                               : function.instructions[guard].text.substr(0, guardEnd);
                const unsigned long guardTarget = addressNumber(branchTarget(function.instructions[guard].text));
                if (!guardOpcode.empty() && guardOpcode[0] == 'j' && guardOpcode != "jmp" && guardTarget > current) {
                    guardedLoopEnd[guard] = index;
                }
            }
            break;
        }
    }

    bool hasReturn = false;
    for (std::size_t index = 0; index < function.instructions.size(); ++index) {
        const Instruction& instruction = function.instructions[index];
        const std::string& text = instruction.text;
        const auto opcodeEnd = text.find_first_of(" \t");
        const std::string opcode = opcodeEnd == std::string::npos ? text : text.substr(0, opcodeEnd);
        const std::vector<std::string> args = operands(text);

        if (loopEndForStart.count(index) != 0) output << "    do {\n";

        if ((opcode == "mov" || opcode == "movzx" || opcode == "movsxd") && args.size() == 2) {
            const std::string destination = stackVariable(args[0]);
            const std::string source = expressionFor(args[1]);
            if (!destination.empty()) {
                output << "    long " << destination << " = " << source << ";\n";
            } else {
                const std::string destinationRegister = canonicalRegister(args[0]);
                if (destinationRegister == loopCounterRegister) {
                    output << "    long i = " << source << ";\n";
                    value[destinationRegister] = "i";
                } else {
                    value[destinationRegister] = source;
                }
            }
            continue;
        }
        if (opcode == "lea" && args.size() == 2) {
            value[canonicalRegister(args[0])] = "&" + args[1];
            continue;
        }
        if ((opcode == "add" || opcode == "sub" || opcode == "imul") && args.size() == 2) {
            const std::string destination = canonicalRegister(args[0]);
            const std::string operatorText = opcode == "add" ? " + " : opcode == "sub" ? " - " : " * ";
            const std::string currentValue = expressionFor(args[0]);
            const std::string rightValue = expressionFor(args[1]);
            if (!loopCounterRegister.empty() && destination == loopCounterRegister) {
                output << "    i " << (opcode == "add" ? "+=" : opcode == "sub" ? "-=" : "*=") << " " << rightValue << ";\n";
                value[destination] = "i";
            } else if (loopEndForStart.size() > 0 && value.count(destination) != 0) {
                output << "    " << currentValue << " " << (opcode == "add" ? "+=" : opcode == "sub" ? "-=" : "*=")
                       << " " << rightValue << ";\n";
                value[destination] = currentValue;
            } else {
                value[destination] = "(" + currentValue + operatorText + rightValue + ")";
            }
            continue;
        }
        if (opcode == "xor" && args.size() == 2 && canonicalRegister(args[0]) == canonicalRegister(args[1])) {
            value[canonicalRegister(args[0])] = "0";
            continue;
        }
        if ((opcode == "cmp" || opcode == "test") && args.size() == 2) {
            comparedLeft = expressionFor(args[0]);
            comparedRight = opcode == "test" && args[0] == args[1] ? "0" : expressionFor(args[1]);
            continue;
        }
        if (startsWith(text, "call")) {
            const std::string callee = callTarget(text);
            const std::string call = callee.empty() ? "/* indirect_call */" : safeCppName(callee);
            output << "    " << call << "(" << expressionFor("rdi") << ", " << expressionFor("rsi") << ");\n";
            value["rax"] = "result";
        } else if (guardedLoopEnd.count(index) != 0) {
            output << "    if (" << invertedConditionFor(opcode, comparedLeft, comparedRight) << ") {\n";
        } else if (loopStartForEnd.count(index) != 0) {
            output << "    } while (" << conditionFor(opcode, comparedLeft, comparedRight) << ");\n";
            for (const auto& guard : guardedLoopEnd) {
                if (guard.second == index) output << "    }\n";
            }
        } else if (!opcode.empty() && opcode[0] == 'j' && opcode != "jmp") {
            output << "    if (" << conditionFor(opcode, comparedLeft, comparedRight) << ") {\n";
            output << "        // branch to 0x" << branchTarget(text) << "\n";
            output << "    }\n";
        } else if (opcode == "jmp") {
            output << "    // jump to 0x" << branchTarget(text) << "\n";
        } else if (text == "ret" || text == "retq") {
            output << "    return " << expressionFor("rax") << ";\n";
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
    if (begin != std::string::npos) {
        const auto end = instruction.find_first_of(" <", begin);
        return instruction.substr(begin + 2, end == std::string::npos ? std::string::npos : end - begin - 2);
    }
    const std::vector<std::string> target = operands(instruction);
    if (target.empty()) return "unknown";
    const auto end = target[0].find_first_of(" <");
    return target[0].substr(0, end);
}

std::string ElfAnalyzer::callTarget(const std::string& instruction) {
    const auto begin = instruction.find('<');
    const auto end = instruction.find('>', begin);
    if (begin == std::string::npos || end == std::string::npos) return "";
    return instruction.substr(begin + 1, end - begin - 1);
}
