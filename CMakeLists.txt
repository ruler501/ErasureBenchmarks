cmake_minimum_required(VERSION 3.14)

include(CMakeDependentOption)

project(ErasureBenchmarks)

add_executable(ErasureBenchmarks main.cpp mparkvariant.hpp)
option(SKIP_FINAL_VIRTUAL "Skip testing with a final version of the AImpl(AImplFinal) class." OFF)
option(SKIP_NONFINAL_VIRTUAL "Skip testing with a non-final version of the AImpl class." OFF)
option(SKIP_UNSAFE_TESTS "Skip the tests which more heavily delve into UB. They do not work with MSVC" ON)
option(SKIP_CREATE_INTERFACE_IMPL "Skip the implementation that creates a subclass of the interface." OFF)
option(SKIP_CAPTURING_LAMBDA "Skip the implementation that uses capturing lambdas." OFF)
option(SKIP_MEMBER_PTR "Skip the implementation that calls with member function pointers." OFF)
option(SKIP_STD_VARIANT "Skip the implementation that uses std::variant." OFF)
option(SKIP_MPARK_VARIANT "Skip the mpark variant implementation." OFF)

cmake_dependent_option(SKIP_UNSAFE_MEMBER_PTR_CAST "Skip the implementation that does unsafe member ptr casts." OFF
                       "NOT SKIP_UNSAFE_TESTS" ON)
cmake_dependent_option(SKIP_UNSAFE_FUNC_PTR_CAST "Skip the implementation that does unsafe function ptr casts." OFF
                       "NOT SKIP_UNSAFE_TESTS" ON)
cmake_dependent_option(SKIP_INTERFACE_REFERENCE "Skip the inheritance reference tests" OFF
                       "NOT SKIP_FINAL_VIRTUAL;NOT SKIP_NONFINAL_VIRTUAL" ON)
cmake_dependent_option(SKIP_POLYMORPHIC_VALUE "Skip the implementation that uses a custom polymorphic_value that always allocates on the stack." OFF
                       "NOT SKIP_FINAL_VIRTUAL;NOT SKIP_NONFINAL_VIRTUAL" ON)

set(OPTIMIZATION_LEVEL "Full" CACHE STRING "The default optimization level that the compiler will be allowed to do.")
set_property(CACHE OPTIMIZATION_LEVEL PROPERTY STRINGS None NoOptimizeMembers NoInlineMembers NoSelectors NoOptimizeRunTest Full)
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    if(NOT SKIP_UNSAFE_TESTS)
        message(WARNING "You cannot run the unsafe tests with MSVC, they will do bad things. Enabling SKIP_UNSAFE_TESTS.")
        set(SKIP_UNSAFE_TESTS ON CACHE INTERNAL "Cannot be used with MSVC")
    endif()
endif()

target_compile_features(ErasureBenchmarks PUBLIC cxx_std_20)

string(TOLOWER "${CMAKE_BUILD_TYPE}" cmake_build_type_lower)
if(cmake_build_type_lower STREQUAL "Release")
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
        target_compile_options(ErasureBenchmarks PUBLIC /O2)
        set_property(TARGET ErasureBenchmarks PROPERTY
                MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        set_property(TARGET ErasureBenchmarks )
    else()
        target_compile_options(ErasureBenchmarks PUBLIC -march=native -O3 -masm=intel -fverbose-asm)
    endif()
endif()

if(NOT SKIP_FINAL_VIRTUAL)
    target_compile_definitions(ErasureBenchmarks PUBLIC FINAL_VIRTUAL)
endif()
if(NOT SKIP_NONFINAL_VIRTUAL)
    target_compile_definitions(ErasureBenchmarks PUBLIC NONFINAL_VIRTUAL)
endif()
if(NOT SKIP_CREATE_INTERFACE_IMPL)
    target_compile_definitions(ErasureBenchmarks PUBLIC CREATE_INTERFACE_IMPL)
endif()
if(NOT SKIP_CAPTURING_LAMBDA)
    target_compile_definitions(ErasureBenchmarks PUBLIC CAPTURING_LAMBDA)
endif()
if(NOT SKIP_MEMBER_PTR)
    target_compile_definitions(ErasureBenchmarks PUBLIC MEMBER_PTR)
endif()
if(NOT SKIP_STD_VARIANT)
    target_compile_definitions(ErasureBenchmarks PUBLIC STD_VARIANT)
endif()
if(NOT SKIP_MPARK_VARIANT)
    target_compile_definitions(ErasureBenchmarks PUBLIC MPARK_VARIANT)
endif()
if(NOT SKIP_UNSAFE_MEMBER_PTR_CAST)
    target_compile_definitions(ErasureBenchmarks PUBLIC UNSAFE_MEMBER_PTR_CAST)
endif()
if(NOT SKIP_UNSAFE_FUNC_PTR_CAST)
    target_compile_definitions(ErasureBenchmarks PUBLIC UNSAFE_FUNC_PTR_CAST)
endif()
if(NOT SKIP_INTERFACE_REFERENCE)
    target_compile_definitions(ErasureBenchmarks PUBLIC INTERFACE_REFERENCE)
endif()
if(NOT SKIP_INTERFACE_REFERENCE_BUFFER)
    target_compile_definitions(ErasureBenchmarks PUBLIC INTERFACE_REFERENCE_BUFFER)
endif()
if(NOT SKIP_POLYMORPHIC_VALUE)
    target_compile_definitions(ErasureBenchmarks PUBLIC POLYMORPHIC_VALUE)
endif()
if(NOT SKIP_TIMING)
    target_compile_definitions(ErasureBenchmarks PUBLIC MANUAL_BENCH)
endif()
if("${OPTIMIZATION_LEVEL}" STREQUAL "None")
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
        target_compile_options(ErasureBenchmarks PUBLIC /Od)
    else()
        target_compile_options(ErasureBenchmarks PUBLIC -O0)
    endif()
    target_compile_definitions(ErasureBenchmarks PUBLIC NO_OPTIMIZE_MEMBERS NO_INLINE_MEMBERS
                                                        NO_OPTIMIZE_RUNTEST NO_INLINE_CALL_MEMBER
                                                        NO_SELECTOR)
endif()
if("${OPTIMIZATION_LEVEL}" STREQUAL "NoOptimizeMembers")
    target_compile_definitions(ErasureBenchmarks PUBLIC NO_OPTIMIZE_MEMBERS NO_INLINE_MEMBERS
                                                        NO_OPTIMIZE_RUNTEST NO_SELECTOR)
endif()
if("${OPTIMIZATION_LEVEL}" STREQUAL "NoInlineMembers")
    target_compile_definitions(ErasureBenchmarks PUBLIC NO_INLINE_MEMBERS NO_OPTIMIZE_RUNTEST
                                                        NO_SELECTOR)
endif()
if("${OPTIMIZATION_LEVEL}" STREQUAL "NoSelector")
    target_compile_definitions(ErasureBenchmarks PUBLIC NO_OPTIMIZE_RUNTEST NO_SELECTOR)
endif()
if("${OPTIMIZATION_LEVEL}" STREQUAL "NoOptimizeRunTest")
    target_compile_definitions(ErasureBenchmarks PUBLIC NO_OPTIMIZE_RUNTEST)
endif()

include(FetchContent)

FetchContent_Declare(
    nanobench
    GIT_REPOSITORY https://github.com/martinus/nanobench.git
    GIT_TAG v4.3.1
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(nanobench)

target_link_libraries(ErasureBenchmarks PRIVATE nanobench)
