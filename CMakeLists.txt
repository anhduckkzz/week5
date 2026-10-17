# Haptic Pottery: build CHAI3D 3.3.0 từ source cùng lúc với project này.
#
#   cmake -S . -B build -DCHAI3D_DIR=<thư mục CHAI3D 3.3.0> -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#
# Đã build và chạy thử với CHAI3D 3.3.0 gốc: MinGW-w64 GCC 14 (MSYS2, Windows 11),
# GCC 11 và Clang 14 (Ubuntu 22.04). Nhánh MSVC dùng đúng các cờ CHAI3D đặt cho MSVC
# nhưng chưa build thử được.
# Chỉ build thư viện chai3d + glfw, không build các example của CHAI3D.
cmake_minimum_required (VERSION 3.13)
project (HapticPottery C CXX)

set (CHAI3D_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../chai3d" CACHE PATH "Thu muc source CHAI3D 3.3.0")
get_filename_component (CHAI3D_DIR "${CHAI3D_DIR}" ABSOLUTE)
if (NOT EXISTS "${CHAI3D_DIR}/src/chai3d.h")
    message (FATAL_ERROR "Khong tim thay CHAI3D o '${CHAI3D_DIR}'. Truyen -DCHAI3D_DIR=<thu muc chai3d-3.3.0>.")
endif ()

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set (CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif ()

# CHAI3D khai báo cmake_minimum_required(3.0), CMake 4 sẽ từ chối nếu không có dòng này
set (CMAKE_POLICY_VERSION_MINIMUM 3.5)
# Linux: dùng libGL kiểu cũ như CHAI3D vẫn dùng, khỏi bị CMake cảnh báo CMP0072
set (OpenGL_GL_PREFERENCE LEGACY)

add_subdirectory ("${CHAI3D_DIR}" chai3d EXCLUDE_FROM_ALL)

# Bản CHAI3D 3.3.0 gốc build bằng MinGW với -std=c++11, khi đó MinGW giấu M_PI và
# build lỗi. Cờ này đứng sau cờ của CHAI3D nên được ưu tiên.
if (MINGW)
    target_compile_options (chai3d PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++14>)
endif ()

# CHAI3D tự thêm -march=native khi build bằng GCC. File exe khi đó chỉ chạy chắc
# chắn trên đúng loại CPU đã build, nên ép lại về x86-64 chung để gửi exe đi được.
option (HP_PORTABLE "Build CHAI3D voi -march=x86-64 thay vi -march=native" ON)
if (HP_PORTABLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64|amd64")
    target_compile_options (chai3d PRIVATE -march=x86-64 -mtune=generic)
    target_compile_options (glfw   PRIVATE -march=x86-64 -mtune=generic)
endif ()

find_package (OpenGL REQUIRED)

file (GLOB SOURCES "${PROJECT_SOURCE_DIR}/src/*.cpp")
add_executable (haptic-pottery ${SOURCES})
set_target_properties (haptic-pottery PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)

target_include_directories (haptic-pottery PRIVATE
    "${CHAI3D_DIR}/src"
    "${CHAI3D_DIR}/externals/Eigen"
    "${CHAI3D_DIR}/externals/glew/include"
    "${CHAI3D_DIR}/extras/GLFW/include")

target_link_libraries (haptic-pottery chai3d glfw ${OPENGL_LIBRARIES})

if (MSVC)
    target_compile_definitions (haptic-pottery PRIVATE WIN64 _CRT_SECURE_NO_DEPRECATE _USE_MATH_DEFINES=)
    target_compile_options (haptic-pottery PRIVATE /EHsc /utf-8)
    target_link_libraries (haptic-pottery winmm)
elseif (MINGW)
    target_compile_definitions (haptic-pottery PRIVATE WIN32 HAVE_GCC_DESTRUCTOR _USE_MATH_DEFINES=)
    target_link_libraries (haptic-pottery winmm)
    # link tĩnh libgcc/libstdc++/winpthread: exe chạy được trên máy không cài MinGW
    target_link_options (haptic-pottery PRIVATE -static -static-libgcc -static-libstdc++)
elseif (CMAKE_SYSTEM_NAME MATCHES Linux)
    target_compile_definitions (haptic-pottery PRIVATE LINUX)
    # thư viện Force Dimension (libdrd.a) đi kèm CHAI3D
    target_link_directories (haptic-pottery PRIVATE "${CHAI3D_DIR}/externals/DHD/lib/lin-${CMAKE_SYSTEM_PROCESSOR}")
    target_link_libraries (haptic-pottery drd usb-1.0 rt pthread dl asound X11 Xcursor Xrandr Xinerama)
elseif (APPLE)
    target_compile_definitions (haptic-pottery PRIVATE MACOSX)
    target_link_directories (haptic-pottery PRIVATE "${CHAI3D_DIR}/externals/DHD/lib/mac-${CMAKE_SYSTEM_PROCESSOR}")
    target_link_libraries (haptic-pottery drd
        "-framework CoreFoundation" "-framework IOKit" "-framework CoreAudio"
        "-framework AudioToolbox" "-framework CoreMIDI" "-framework Cocoa" "-framework CoreVideo")
endif ()
