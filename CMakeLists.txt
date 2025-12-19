cmake_minimum_required(VERSION 3.13)

project(git LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(git
    src/main.cpp
)

# --- REQUIRED SYSTEM LIBRARIES ---
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

# libcurl is NOT always exported as CURL::libcurl in Codecrafters,
# so we link it explicitly.
find_library(CURL_LIB curl REQUIRED)

target_link_libraries(git
    ${CURL_LIB}
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
)
