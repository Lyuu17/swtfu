# SWTFU-Helper: one static x86 exe, no Python or runtime needed to run it.
#   cmake -S helper -B helper/build -A Win32
#   cmake --build helper/build --config Release
# The exe is written to the repository root, next to the .cmd launchers.
cmake_minimum_required(VERSION 3.20)
project(SWTFUHelper CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Configure with -A Win32: the game is 32-bit")
endif()

file(GLOB SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/*.cpp)
add_executable(SWTFU-Helper ${SOURCES} ${CMAKE_SOURCE_DIR}/patches.h)
target_compile_features(SWTFU-Helper PRIVATE cxx_std_17)
target_compile_definitions(SWTFU-Helper PRIVATE UNICODE _UNICODE)
target_compile_options(SWTFU-Helper PRIVATE /W4 /WX /utf-8 /EHsc)
set_property(TARGET SWTFU-Helper PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")  # static CRT
target_link_libraries(SWTFU-Helper PRIVATE user32 shell32 bcrypt dbghelp)
set_target_properties(SWTFU-Helper PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/..
  RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/Debug)
