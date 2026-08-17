#pragma once

#include "ElfAnalyzer.hpp"

#include <X11/Xlib.h>

#include <string>

// Native X11 user interface. It owns only presentation and user interaction;
// all ELF parsing and reconstruction remain in ElfAnalyzer.
class StudioWindow {
public:
    explicit StudioWindow(std::string initialPath);
    ~StudioWindow();

    int run();

private:
    void initializeWindow();
    void analyzeCurrentPath();
    void draw();
    void drawFunctionList(int height);
    void drawSelectedFunction(int width, int height);
    void drawCallGraph(const Function& function, int width, int height);
    void handleKey(const XKeyEvent& event);
    void handleClick(const XButtonEvent& event);
    void selectFunctionFromList(int mouseY);
    void selectCallGraphTarget(int mouseX);

    void drawText(int x, int y, const std::string& value, unsigned long color = 0x202538);
    void drawBox(int x, int y, int width, int height, unsigned long color);
    void drawMultilineText(int x, int y, int maximumLines, const std::string& content, unsigned long color);

    Display* display_ = nullptr;
    Window window_ = 0;
    GC graphics_ = 0;
    XFontStruct* font_ = nullptr;
    int screen_ = 0;

    ElfAnalyzer analyzer_;
    std::string initialPath_;
    std::string pathText_;
    std::string searchText_;
    std::string status_ = "Masukkan path ELF, lalu tekan Analyze.";
    int selectedFunction_ = -1;
    int graphZoom_ = 1;
    bool editingPath_ = true;

    static constexpr int kSidebarWidth = 310;
    static constexpr int kHeaderHeight = 92;
    static constexpr int kLineHeight = 18;
};
