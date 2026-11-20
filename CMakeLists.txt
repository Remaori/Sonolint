cmake_minimum_required(VERSION 3.25)   # workflow presets, and SYSTEM in FetchContent_Declare

project(Sonolint VERSION 0.1.0 LANGUAGES C CXX)

# ON builds only sonolint-core and its unit tests, and never downloads JUCE. The
# core-linux CI job uses it: a JUCE header included from src/core/ fails there.
option(SONOLINT_CORE_ONLY "Build only sonolint-core and its unit tests (no JUCE)" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Link the C++ runtime statically: sonolint.exe does not depend on the Visual C++ DLLs.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options(/MP)   # compile the files of a target in parallel
endif()

enable_testing()

# --- Dependencies, downloaded automatically and pinned -------------------------
# SYSTEM keeps their headers out of our warnings. The versions are also listed in
# THIRD_PARTY_NOTICES.txt: update both together.
include(FetchContent)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        yaml-cpp-0.9.0
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.1
    GIT_SHALLOW    TRUE
    SYSTEM
)
# pocketfft and dr_libs are single headers: SOURCE_SUBDIR points at nothing, so they
# are downloaded but their own build files never run.
FetchContent_Declare(pocketfft
    GIT_REPOSITORY https://github.com/mreineck/pocketfft.git
    GIT_TAG        4d865cba24359b05b37da4e75aee24498c3b78e7   # branch "cpp"; the project has no version tags
    SOURCE_SUBDIR  none
)
FetchContent_Declare(dr_libs
    GIT_REPOSITORY https://github.com/mackron/dr_libs.git
    GIT_TAG        wav-0.14.6
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  none
)

set(YAML_CPP_BUILD_TOOLS   OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TESTS   OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(nlohmann_json yaml-cpp Catch2 pocketfft dr_libs)

if(MSVC)
    # yaml-cpp 0.9.0 warns about size_t conversions in its own sources.
    target_compile_options(yaml-cpp PRIVATE /wd4267)
endif()

add_library(pocketfft INTERFACE)
target_include_directories(pocketfft SYSTEM INTERFACE "${pocketfft_SOURCE_DIR}")

add_library(dr_libs INTERFACE)
target_include_directories(dr_libs SYSTEM INTERFACE "${dr_libs_SOURCE_DIR}")

# --- sonolint-core: signals, metrics, comparison and reports. No JUCE. ---------
# Every .cpp under src/core/ belongs to the library; a new file needs no change here.
file(GLOB_RECURSE SONOLINT_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp")

add_library(sonolint-core STATIC ${SONOLINT_CORE_SOURCES})

target_include_directories(sonolint-core PUBLIC src)

find_package(Threads REQUIRED)   # the watchdog that stops a scenario that hangs

target_link_libraries(sonolint-core
    PUBLIC
        nlohmann_json::nlohmann_json   # results are built as JSON, also in the headers
    PRIVATE
        yaml-cpp::yaml-cpp
        pocketfft
        dr_libs
        Threads::Threads
)

if(MSVC)
    target_compile_options(sonolint-core PRIVATE /W4 /permissive-)
else()
    # Clang and GCC may fuse a * b + c into one rounding step (FMA); MSVC never does.
    # Turning that off gives the same numbers on every compiler. /fp:precise is MSVC's
    # default. Never enable fast-math here.
    target_compile_options(sonolint-core PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
endif()

# --- Unit tests of the core -----------------------------------------------------
file(GLOB SONOLINT_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/*.cpp")

add_executable(sonolint-tests ${SONOLINT_TEST_SOURCES})

target_link_libraries(sonolint-tests
    PRIVATE
        sonolint-core
        Catch2::Catch2WithMain
)

# The expected files that some tests compare their output with, byte for byte.
target_compile_definitions(sonolint-tests PRIVATE SONOLINT_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")

add_test(NAME unit COMMAND sonolint-tests)

# Our own code builds without warnings, and CI keeps it that way.
set_target_properties(sonolint-core sonolint-tests PROPERTIES COMPILE_WARNING_AS_ERROR ON)

if(SONOLINT_CORE_ONLY)
    return()
endif()

# --- JUCE and the command-line parser ---------------------------------------------
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.4
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_Declare(CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.7.2
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(JUCE CLI11)

# --- sonolint-host: loads the VST3 and renders audio through it, with JUCE -----
file(GLOB SONOLINT_HOST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/host/*.cpp")

add_library(sonolint-host STATIC ${SONOLINT_HOST_SOURCES})

# The JUCE modules are compiled into this library once. Whoever links it gets its
# definitions and include paths, so JUCE headers mean the same thing everywhere.
target_link_libraries(sonolint-host
    PUBLIC
        sonolint-core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    PRIVATE
        juce::juce_audio_processors_headless   # hosting without JUCE's GUI modules (docs/adr/0016)
)

target_compile_definitions(sonolint-host
    PUBLIC
        JUCE_PLUGINHOST_VST3=1
    INTERFACE
        $<TARGET_PROPERTY:sonolint-host,COMPILE_DEFINITIONS>
)

target_include_directories(sonolint-host
    INTERFACE
        $<TARGET_PROPERTY:sonolint-host,INCLUDE_DIRECTORIES>
)

# --- sonolint-cli: the `sonolint` executable -------------------------------------
juce_add_console_app(sonolint-cli PRODUCT_NAME "sonolint")
set_target_properties(sonolint-cli PROPERTIES OUTPUT_NAME sonolint)   # PRODUCT_NAME doesn't rename a console app

target_sources(sonolint-cli PRIVATE src/cli/main.cpp src/cli/terminal.cpp)

target_link_libraries(sonolint-cli
    PRIVATE
        sonolint-host
        CLI11::CLI11
)

# Shown by `sonolint version`. Both are read when CMake configures: exact in CI, and
# they can lag behind in a working tree until the next configure.
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE SONOLINT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT SONOLINT_COMMIT)
    set(SONOLINT_COMMIT "unknown")
endif()
string(TIMESTAMP SONOLINT_BUILD_DATE "%Y-%m-%d" UTC)

target_compile_definitions(sonolint-cli
    PRIVATE
        SONOLINT_VERSION="${PROJECT_VERSION}"
        SONOLINT_COMMIT="${SONOLINT_COMMIT}"
        SONOLINT_BUILD_DATE="${SONOLINT_BUILD_DATE}"
)

set_target_properties(sonolint-host sonolint-cli PROPERTIES COMPILE_WARNING_AS_ERROR ON)

add_test(NAME cli-version COMMAND sonolint-cli version)

# --- Test plugins and the tests that run the real binary against them -----------
add_subdirectory(tests/fixtures)

file(GLOB SONOLINT_INTEGRATION_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/*.cpp")

add_executable(sonolint-integration-tests ${SONOLINT_INTEGRATION_SOURCES})

target_link_libraries(sonolint-integration-tests
    PRIVATE
        sonolint-core
        Catch2::Catch2WithMain
)

# Where the binary, its configs and the test plugins are, and where it may write.
target_compile_definitions(sonolint-integration-tests
    PRIVATE
        SONOLINT_EXE="$<TARGET_FILE:sonolint-cli>"
        SONOLINT_CONFIGS="${CMAKE_CURRENT_SOURCE_DIR}/tests/integration"
        SONOLINT_FIXTURES="${CMAKE_CURRENT_BINARY_DIR}/tests/fixtures"
        SONOLINT_BUILD_CONFIG="$<CONFIG>"
        SONOLINT_OUT="${CMAKE_CURRENT_BINARY_DIR}/integration-out"
)

get_property(SONOLINT_FIXTURE_NAMES GLOBAL PROPERTY SONOLINT_FIXTURES)

foreach(fixture ${SONOLINT_FIXTURE_NAMES})
    add_dependencies(sonolint-integration-tests ${fixture}_VST3)
endforeach()

add_dependencies(sonolint-integration-tests sonolint-cli)
set_target_properties(sonolint-integration-tests PROPERTIES COMPILE_WARNING_AS_ERROR ON)

add_test(NAME integration COMMAND sonolint-integration-tests)
