#pragma once

#include <string>
#include <vector>

struct Instruction {
    std::string address;
    std::string bytes;
    std::string text;
};

struct Function {
    unsigned long address = 0;
    unsigned long size = 0;
    std::string name;
    std::vector<Instruction> instructions;
    std::vector<std::string> calls;
};
