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

struct CallGraphNode {
    std::size_t functionIndex = 0;
    int x = 0;
    int y = 0;
    int depth = 0;
};
