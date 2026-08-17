#include "StudioWindow.hpp"

int main(int argc, char** argv) {
    const std::string binaryPath = argc > 1 ? argv[1] : "";
    StudioWindow application(binaryPath);
    return application.run();
}
