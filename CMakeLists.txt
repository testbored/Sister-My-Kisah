cmake_minimum_required(VERSION 3.16)
project(SREStudio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(X11 REQUIRED)
add_executable(sre_studio
    main.cpp
    ElfAnalyzer.cpp
    StudioWindow.cpp
)
target_link_libraries(sre_studio PRIVATE X11::X11)
target_compile_options(sre_studio PRIVATE -Wall -Wextra -Wpedantic)
