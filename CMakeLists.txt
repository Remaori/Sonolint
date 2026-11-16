cmake_minimum_required(VERSION 3.25)   # workflow presets, and SYSTEM in FetchContent_Declare

# 0.0.x while the first version is in development; 0.1.0 is the first release.
project(Sonolint VERSION 0.0.1 LANGUAGES C CXX)

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
add_library(sonolint-core STATIC
    src/core/report/param_list.cpp
)

target_include_directories(sonolint-core PUBLIC src)

target_link_libraries(sonolint-core
    PRIVATE
        nlohmann_json::nlohmann_json
        yaml-cpp::yaml-cpp
        pocketfft
        dr_libs
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
add_executable(sonolint-tests
    tests/unit/param_list_test.cpp
)

target_link_libraries(sonolint-tests
    PRIVATE
        sonolint-core
        nlohmann_json::nlohmann_json
        Catch2::Catch2WithMain
)

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
add_library(sonolint-host STATIC
    src/host/plugin_loader.cpp
)

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

target_sources(sonolint-cli PRIVATE src/cli/main.cpp)

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
