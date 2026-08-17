#include "StudioWindow.hpp"

#include <X11/keysym.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

constexpr unsigned long kBackground = 0xf7f8fc;
constexpr unsigned long kHeader = 0x18233c;
constexpr unsigned long kSidebar = 0xe8edf7;
constexpr unsigned long kText = 0x202538;
constexpr unsigned long kMutedText = 0x33466b;

}  // namespace

StudioWindow::StudioWindow(std::string initialPath) : initialPath_(std::move(initialPath)) {}

StudioWindow::~StudioWindow() {
    if (display_ != nullptr) XCloseDisplay(display_);
}

int StudioWindow::run() {
    display_ = XOpenDisplay(nullptr);
    if (display_ == nullptr) {
        std::cerr << "Tidak dapat membuka X display. Jalankan dari desktop Linux/X11.\n";
        return 1;
    }
    initializeWindow();
    pathText_ = initialPath_;
    if (!initialPath_.empty()) analyzeCurrentPath();

    while (true) {
        XEvent event;
        XNextEvent(display_, &event);
        if (event.type == Expose || event.type == ConfigureNotify) draw();
        if (event.type == KeyPress) handleKey(event.xkey);
        if (event.type == ButtonPress) handleClick(event.xbutton);
    }
}

void StudioWindow::initializeWindow() {
    screen_ = DefaultScreen(display_);
    window_ = XCreateSimpleWindow(display_, RootWindow(display_, screen_), 40, 40, 1280, 760, 1,
                                  BlackPixel(display_, screen_), kBackground);
    XStoreName(display_, window_, "SRE Studio - ELF C++ Decompiler");
    XSelectInput(display_, window_, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);
    XMapWindow(display_, window_);
    graphics_ = XCreateGC(display_, window_, 0, nullptr);
    font_ = XLoadQueryFont(display_, "fixed");
    if (font_ != nullptr) XSetFont(display_, graphics_, font_->fid);
}

void StudioWindow::analyzeCurrentPath() {
    std::string message;
    if (analyzer_.open(pathText_, message)) {
        status_ = message;
        selectedFunction_ = 0;
        searchText_.clear();
        editingPath_ = false;
    } else {
        status_ = "Error: " + message;
    }
    draw();
}

void StudioWindow::drawText(int x, int y, const std::string& value, unsigned long color) {
    XSetForeground(display_, graphics_, color);
    XDrawString(display_, window_, graphics_, x, y, value.c_str(), static_cast<int>(value.size()));
}

void StudioWindow::drawBox(int x, int y, int width, int height, unsigned long color) {
    XSetForeground(display_, graphics_, color);
    XFillRectangle(display_, window_, graphics_, x, y, width, height);
}

void StudioWindow::drawMultilineText(int x, int y, int maximumLines, const std::string& content, unsigned long color) {
    std::istringstream lines(content);
    std::string line;
    for (int number = 1; number <= maximumLines && std::getline(lines, line); ++number) {
        drawText(x, y + number * kLineHeight, line.substr(0, 110), color);
    }
}

void StudioWindow::draw() {
    if (display_ == nullptr) return;
    XWindowAttributes attributes;
    XGetWindowAttributes(display_, window_, &attributes);
    const int width = attributes.width;
    const int height = attributes.height;

    drawBox(0, 0, width, height, kBackground);
    drawBox(0, 0, width, kHeaderHeight, kHeader);
    drawBox(0, kHeaderHeight, kSidebarWidth, height - kHeaderHeight, kSidebar);
    drawText(18, 28, "SRE STUDIO", 0xffffff);
    drawText(18, 50, "ELF x86-64 C++ Decompiler", 0xaecbff);
    drawBox(300, 16, width - 470, 30, 0xffffff);
    drawText(308, 37, pathText_.empty() ? "Path binary ELF..." : pathText_);
    drawBox(width - 156, 16, 132, 30, 0x3478d4);
    drawText(width - 130, 37, "ANALYZE", 0xffffff);
    drawText(18, 78, status_, 0xd2e1ff);

    drawFunctionList(height);
    drawSelectedFunction(width, height);
}

void StudioWindow::drawFunctionList(int height) {
    const std::string title = searchText_.empty() ? "FUNCTIONS (quick search: type)"
                                                   : "FUNCTIONS / filter: " + searchText_;
    drawText(14, 118, title, kMutedText);
    const auto& functions = analyzer_.functions();
    int displayed = 0;
    for (std::size_t index = 0; index < functions.size() && displayed < (height - 140) / kLineHeight; ++index) {
        const Function& function = functions[index];
        if (!searchText_.empty() && function.name.find(searchText_) == std::string::npos) continue;
        const int y = 140 + displayed++ * kLineHeight;
        if (static_cast<int>(index) == selectedFunction_) drawBox(5, y - 14, kSidebarWidth - 10, 17, 0xc9dcfa);
        std::ostringstream label;
        label << std::hex << "0x" << function.address << "  " << function.name;
        drawText(12, y, label.str().substr(0, 43));
    }
}

void StudioWindow::drawSelectedFunction(int width, int height) {
    const auto& functions = analyzer_.functions();
    if (selectedFunction_ < 0 || selectedFunction_ >= static_cast<int>(functions.size())) {
        drawText(kSidebarWidth + 22, 130, "Pilih fungsi untuk melihat rekonstruksi dan assembly.");
        return;
    }
    const Function& function = functions[selectedFunction_];
    const int split = kSidebarWidth + (width - kSidebarWidth) / 2;
    drawText(kSidebarWidth + 22, 118, "RECONSTRUCTED C++", kMutedText);
    drawText(split + 18, 118, "ASSEMBLY / OPCODES", kMutedText);
    drawMultilineText(kSidebarWidth + 22, 140, (height - 160) / kLineHeight, analyzer_.pseudocode(function), kText);

    std::ostringstream assembly;
    int lineNumber = 1;
    for (const Instruction& instruction : function.instructions) {
        assembly << std::setw(3) << lineNumber++ << "  " << instruction.address << "  " << instruction.text << '\n';
    }
    drawMultilineText(split + 18, 140, (height - 160) / kLineHeight, assembly.str(), 0x283653);
    drawCallGraph(function, width, height);
}

void StudioWindow::drawCallGraph(const Function& function, int width, int height) {
    const int graphY = height - 38;
    int x = kSidebarWidth + 28;
    const int spacing = 120 * graphZoom_;
    drawText(kSidebarWidth + 22, graphY - 12, "CALL GRAPH (wheel zoom; click a node)", kMutedText);
    for (const std::string& call : function.calls) {
        drawBox(x, graphY, 100, 22, 0x77a7e7);
        drawText(x + 5, graphY + 16, call.substr(0, 13), 0xffffff);
        XDrawLine(display_, window_, graphics_, x - 15, graphY + 11, x, graphY + 11);
        x += spacing;
        if (x > width - 105) break;
    }
}

void StudioWindow::handleKey(const XKeyEvent& event) {
    char characters[32]{};
    KeySym key;
    const int count = XLookupString(const_cast<XKeyEvent*>(&event), characters, sizeof(characters), &key, nullptr);
    if (key == XK_Escape) std::exit(0);
    if (key == XK_Return && editingPath_) {
        analyzeCurrentPath();
        return;
    }

    std::string& input = editingPath_ ? pathText_ : searchText_;
    if (key == XK_BackSpace && !input.empty()) input.pop_back();
    if (count > 0 && std::isprint(static_cast<unsigned char>(characters[0]))) input.append(characters, count);
    draw();
}

void StudioWindow::handleClick(const XButtonEvent& event) {
    XWindowAttributes attributes;
    XGetWindowAttributes(display_, window_, &attributes);
    if (event.y < kHeaderHeight && event.x >= 300 && event.x < attributes.width - 165) {
        editingPath_ = true;
    } else if (event.y < kHeaderHeight && event.x >= attributes.width - 165) {
        analyzeCurrentPath();
        return;
    } else if (event.x < kSidebarWidth && event.y >= 125) {
        editingPath_ = false;
        selectFunctionFromList(event.y);
    } else if (event.y > attributes.height - 70) {
        selectCallGraphTarget(event.x);
    }
    if (event.button == Button4) graphZoom_ = std::min(3, graphZoom_ + 1);
    if (event.button == Button5) graphZoom_ = std::max(1, graphZoom_ - 1);
    draw();
}

void StudioWindow::selectFunctionFromList(int mouseY) {
    const int wantedRow = (mouseY - 140 + 14) / kLineHeight;
    int visibleRow = 0;
    const auto& functions = analyzer_.functions();
    for (std::size_t index = 0; index < functions.size(); ++index) {
        if (!searchText_.empty() && functions[index].name.find(searchText_) == std::string::npos) continue;
        if (visibleRow++ == wantedRow) {
            selectedFunction_ = static_cast<int>(index);
            return;
        }
    }
}

void StudioWindow::selectCallGraphTarget(int mouseX) {
    if (selectedFunction_ < 0) return;
    const int callIndex = (mouseX - kSidebarWidth - 28) / (120 * graphZoom_);
    const auto& calls = analyzer_.functions()[selectedFunction_].calls;
    if (callIndex < 0 || callIndex >= static_cast<int>(calls.size())) return;
    const auto& functions = analyzer_.functions();
    for (std::size_t index = 0; index < functions.size(); ++index) {
        if (functions[index].name == calls[callIndex]) {
            selectedFunction_ = static_cast<int>(index);
            return;
        }
    }
}
