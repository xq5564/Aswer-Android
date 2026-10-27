cmake_minimum_required(VERSION 3.22)
project(aswer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-fno-rtti -fvisibility=hidden -O2)
add_link_options(-s)

find_library(liblog log)
find_library(libandroid android)
find_library(libEGL EGL)
find_library(libGLESv3 GLESv3)

# ---- Dear ImGui ----
file(GLOB IMGUI_CORE third_party/imgui_core/*.cpp)
set(IMGUI_BACKENDS third_party/imgui_backends/imgui_impl_opengl3.cpp)

# ---- mbedTLS (HTTPS 支持) ----
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/mbedtls)

add_executable(aswer
    src/main.cpp
    ${IMGUI_CORE}
    ${IMGUI_BACKENDS}
)

target_include_directories(aswer PRIVATE
    src
    third_party
    third_party/imgui_core
    third_party/imgui_backends
    third_party/mbedtls/include
)

target_compile_definitions(aswer PRIVATE
    CPPHTTPLIB_MBEDTLS_SUPPORT
    CPPHTTPLIB_ALLOW_BROKEN_SERVER_CERTIFICATES=OFF
    IMGUI_IMPL_OPENGL_ES3
)

target_link_libraries(aswer
    ${liblog} ${libandroid} ${libEGL} ${libGLESv3}
    mbedtls mbedx509 mbedcrypto
)
