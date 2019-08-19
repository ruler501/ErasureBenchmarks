cmake_minimum_required(VERSION 3.14)
project(ErasureBenchmarks)

add_executable(ErasureBenchmarks main.cpp)

target_compile_features(ErasureBenchmarks PUBLIC cxx_std_17)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
        target_compile_options(ErasureBenchmarks PUBLIC /O2)
    else()
        target_compile_options(ErasureBenchmarks PUBLIC -march=native -Ofast -masm=intel -fverbose-asm)
    endif()
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
else()
    # Download automatically, you can also just copy the conan.cmake file
    if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
        message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
        file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/master/conan.cmake"
                "${CMAKE_BINARY_DIR}/conan.cmake")
    endif()

    include(${CMAKE_BINARY_DIR}/conan.cmake)

    conan_cmake_run(REQUIRES dyno/0.1@lawrencem/stable
            BASIC_SETUP CMAKE_TARGETS)

    target_link_libraries(ErasureBenchmarks PUBLIC CONAN_PKG::dyno)
endif()