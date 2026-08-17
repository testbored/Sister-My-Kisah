#include "StudioWindow.hpp"

#include <X11/keysym.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <queue>
#include <sstream>
#include <unordered_map>

namespace {

constexpr unsigned long kBackground = 0xf7f8fc;
constexpr unsigned long kHeader = 0x18233c;
constexpr unsigned long kSidebar = 0xe8edf7;
constexpr unsigned long kText = 0x202538;
constexpr unsigned long kMutedText = 0x33466b;
constexpr unsigned long kGraphBackground = 0x101722;
constexpr unsigned long kGraphGrid = 0x1c2838;
constexpr unsigned long kGraphEdge = 0x7398c3;
constexpr int kGraphNodeWidth = 154;
constexpr int kGraphNodeHeight = 44;

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
        if (event.type == MotionNotify) handleMotion(event.xmotion);
        if (event.type == ButtonRelease) draggingGraph_ = false;
    }
}

void StudioWindow::initializeWindow() {
    screen_ = DefaultScreen(display_);
    window_ = XCreateSimpleWindow(display_, RootWindow(display_, screen_), 40, 40, 1280, 760, 1,
                                  BlackPixel(display_, screen_), kBackground);
    XStoreName(display_, window_, "SRE Studio - ELF C++ Decompiler");
    XSelectInput(display_, window_, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask |
                                        PointerMotionMask | StructureNotifyMask);
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
    if (graphView_) {
        drawCallGraph(width, height);
    } else {
        drawSelectedFunction(width, height);
    }
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
    (void)function;
}

void StudioWindow::drawGraphNode(const CallGraphNode& node, int x, int y, bool selected) {
    const Function& function = analyzer_.functions()[node.functionIndex];
    const unsigned long fill = selected ? 0x386d9e : node.depth == 0 ? 0x885c36 : 0x263b52;
    drawBox(x, y, kGraphNodeWidth, kGraphNodeHeight, fill);
    XSetForeground(display_, graphics_, selected ? 0x93d7ff : 0x77a8cf);
    XDrawRectangle(display_, window_, graphics_, x, y, kGraphNodeWidth, kGraphNodeHeight);
    drawText(x + 8, y + 17, function.name.substr(0, 20), 0xffffff);
    std::ostringstream address;
    address << "0x" << std::hex << function.address;
    drawText(x + 8, y + 34, address.str(), 0xc9d8e8);
}

std::vector<CallGraphNode> StudioWindow::buildGraphLayout() const {
    const auto& functions = analyzer_.functions();
    std::unordered_map<std::string, int> indexByName;
    for (std::size_t index = 0; index < functions.size(); ++index) indexByName[functions[index].name] = static_cast<int>(index);

    const int root = selectedFunction_ >= 0 ? selectedFunction_ : 0;
    std::vector<int> depth(functions.size(), -1);
    std::queue<int> pending;
    depth[root] = 0;
    pending.push(root);
    while (!pending.empty()) {
        const int source = pending.front();
        pending.pop();
        for (const std::string& call : functions[source].calls) {
            const auto found = indexByName.find(call);
            if (found != indexByName.end() && depth[found->second] == -1) {
                depth[found->second] = depth[source] + 1;
                pending.push(found->second);
            }
        }
    }
    int lastDepth = 0;
    for (int& value : depth) {
        if (value == -1) value = ++lastDepth;
        else lastDepth = std::max(lastDepth, value);
    }
    std::vector<int> row(lastDepth + 1, 0);
    std::vector<CallGraphNode> nodes;
    for (std::size_t index = 0; index < functions.size() && index < 48; ++index) {
        nodes.push_back({index, depth[index] * 230, row[depth[index]]++ * 85, depth[index]});
    }
    return nodes;
}

void StudioWindow::drawArrow(int fromX, int fromY, int toX, int toY, unsigned long color) {
    XSetForeground(display_, graphics_, color);
    XDrawLine(display_, window_, graphics_, fromX, fromY, toX, toY);
    const int direction = toX >= fromX ? 1 : -1;
    XDrawLine(display_, window_, graphics_, toX, toY, toX - direction * 8, toY - 5);
    XDrawLine(display_, window_, graphics_, toX, toY, toX - direction * 8, toY + 5);
}

void StudioWindow::drawCallGraph(int width, int height) {
    drawBox(kSidebarWidth, kHeaderHeight, width - kSidebarWidth, height - kHeaderHeight, kGraphBackground);
    drawText(kSidebarWidth + 22, 120, "CALL GRAPH / FUNCTION RELATIONSHIPS", 0xc8dbef);
    drawText(kSidebarWidth + 22, 140, "Drag canvas: pan | scroll: zoom | click node: open function | G: decompiler", 0x91a8c2);
    const auto& functions = analyzer_.functions();
    if (functions.empty()) return;
    graphCanvasWidth_ = width;

    XSetForeground(display_, graphics_, kGraphGrid);
    for (int x = kSidebarWidth; x < width; x += 32) XDrawLine(display_, window_, graphics_, x, kHeaderHeight, x, height);
    for (int y = kHeaderHeight; y < height; y += 32) XDrawLine(display_, window_, graphics_, kSidebarWidth, y, width, y);

    const std::vector<CallGraphNode> nodes = buildGraphLayout();
    std::unordered_map<std::size_t, CallGraphNode> nodeByIndex;
    std::unordered_map<std::string, std::size_t> indexByName;
    for (const CallGraphNode& node : nodes) {
        nodeByIndex[node.functionIndex] = node;
        indexByName[functions[node.functionIndex].name] = node.functionIndex;
    }
    const int scale = graphZoom_;
    const int originX = kSidebarWidth + 55 + graphPanX_;
    const int originY = 180 + graphPanY_;

    for (const CallGraphNode& source : nodes) {
        for (const std::string& call : functions[source.functionIndex].calls) {
            const auto targetIndex = indexByName.find(call);
            if (targetIndex == indexByName.end()) continue;
            const CallGraphNode& target = nodeByIndex[targetIndex->second];
            const int sourceX = originX + source.x * scale + kGraphNodeWidth;
            const int sourceY = originY + source.y * scale + kGraphNodeHeight / 2;
            const int targetX = originX + target.x * scale;
            const int targetY = originY + target.y * scale + kGraphNodeHeight / 2;
            drawArrow(sourceX, sourceY, targetX, targetY, kGraphEdge);
        }
    }
    for (const CallGraphNode& node : nodes) {
        const int x = originX + node.x * scale;
        const int y = originY + node.y * scale;
        drawGraphNode(node, x, y, static_cast<int>(node.functionIndex) == selectedFunction_);
    }
}

void StudioWindow::handleKey(const XKeyEvent& event) {
    char characters[32]{};
    KeySym key;
    const int count = XLookupString(const_cast<XKeyEvent*>(&event), characters, sizeof(characters), &key, nullptr);
    if (key == XK_Escape) std::exit(0);
    if (key == XK_g || key == XK_G) {
        graphView_ = !graphView_;
        editingPath_ = false;
        draw();
        return;
    }
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
    } else if (graphView_ && event.button == Button1) {
        selectGraphNode(event.x, event.y);
        if (graphView_) {
            draggingGraph_ = true;
            lastPointerX_ = event.x;
            lastPointerY_ = event.y;
        }
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

void StudioWindow::selectGraphNode(int mouseX, int mouseY) {
    const int originX = kSidebarWidth + 55 + graphPanX_;
    const int originY = 180 + graphPanY_;
    for (const CallGraphNode& node : buildGraphLayout()) {
        const int x = originX + node.x * graphZoom_;
        const int y = originY + node.y * graphZoom_;
        if (mouseX >= x && mouseX <= x + kGraphNodeWidth && mouseY >= y && mouseY <= y + kGraphNodeHeight) {
            selectedFunction_ = static_cast<int>(node.functionIndex);
            graphView_ = false;
            return;
        }
    }
}

void StudioWindow::handleMotion(const XMotionEvent& event) {
    if (!graphView_ || !draggingGraph_) return;
    graphPanX_ += event.x - lastPointerX_;
    graphPanY_ += event.y - lastPointerY_;
    lastPointerX_ = event.x;
    lastPointerY_ = event.y;
    draw();
}
