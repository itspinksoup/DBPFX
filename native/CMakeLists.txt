cmake_minimum_required(VERSION 4.0)
# macOS 14 is the oldest supported system: binaries refuse to load on anything older.
# (must be set before project())
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version" FORCE)
project(DBPFX VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------- requirements
# DBPFX is only built with, and only runs on, current software, so every report comes from a recent,
# supported setup. The run-time limits live in include/dbpfx/requirements.hpp; keep both in step.
#   Build:  CMake 4.0, MSVC 19.50 (Visual Studio 2026), GCC 12, Clang 15, AppleClang 15 (Xcode 15),
#           Windows SDK 10.0.26100
#   Run:    minimum Windows 10 22H2 (build 19045), Linux with glibc 2.35, macOS 14;
#           recommended Windows 11, glibc 2.39, macOS 15
# A Linux library works on the glibc it was built with and newer, so release builds for Linux are
# made on the minimum (Ubuntu 22.04).
set(DBPFX_MIN_MSVC 19.50)
set(DBPFX_MIN_GCC 12)
set(DBPFX_MIN_CLANG 15)
set(DBPFX_MIN_APPLECLANG 15)
set(DBPFX_MIN_WINDOWS_SDK 10.0.26100)
set(DBPFX_MIN_GLIBC 2.35)

foreach(_lang C CXX)
    set(_id "${CMAKE_${_lang}_COMPILER_ID}")
    set(_ver "${CMAKE_${_lang}_COMPILER_VERSION}")
    if(_id STREQUAL "MSVC")
        set(_min ${DBPFX_MIN_MSVC})
        set(_what "MSVC ${DBPFX_MIN_MSVC} (Visual Studio 2026)")
    elseif(_id STREQUAL "GNU")
        set(_min ${DBPFX_MIN_GCC})
        set(_what "GCC ${DBPFX_MIN_GCC}")
    elseif(_id STREQUAL "AppleClang")
        set(_min ${DBPFX_MIN_APPLECLANG})
        set(_what "AppleClang ${DBPFX_MIN_APPLECLANG} (Xcode 15)")
    elseif(_id STREQUAL "Clang")
        set(_min ${DBPFX_MIN_CLANG})
        set(_what "Clang ${DBPFX_MIN_CLANG}")
    else()
        message(FATAL_ERROR "Unsupported ${_lang} compiler '${_id}'. Use MSVC, GCC, Clang or AppleClang.")
    endif()
    if(_ver VERSION_LESS _min)
        message(FATAL_ERROR "${_lang} compiler ${_id} ${_ver} is too old: DBPFX requires ${_what} or newer.")
    endif()
endforeach()

if(WIN32)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "DBPFX is built for 64-bit Windows only.")
    endif()
    # Visual Studio generators report the SDK themselves; Ninja/NMake builds get it from vcvars
    set(_sdk "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
    if(NOT _sdk)
        string(REGEX REPLACE "[\\/]+$" "" _sdk "$ENV{WindowsSDKVersion}")
    endif()
    if(NOT _sdk)
        message(FATAL_ERROR "The Windows SDK version is unknown. Build from a Visual Studio developer environment (build.ps1 sets it up).")
    endif()
    if(_sdk VERSION_LESS DBPFX_MIN_WINDOWS_SDK)
        message(FATAL_ERROR "Windows SDK ${_sdk} is too old: DBPFX requires ${DBPFX_MIN_WINDOWS_SDK} or newer.")
    endif()
    message(STATUS "Windows SDK ${_sdk}")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # the native library needs at least the glibc of the machine that builds it
    execute_process(COMMAND getconf GNU_LIBC_VERSION OUTPUT_VARIABLE _libc OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE _libc_rc ERROR_QUIET)
    if(NOT _libc_rc EQUAL 0 OR NOT _libc MATCHES "^glibc ")
        message(FATAL_ERROR "DBPFX requires glibc ${DBPFX_MIN_GLIBC} or newer (musl and other C libraries are not supported).")
    endif()
    string(REPLACE "glibc " "" _libc "${_libc}")
    if(_libc VERSION_LESS DBPFX_MIN_GLIBC)
        message(FATAL_ERROR "glibc ${_libc} is too old: DBPFX requires ${DBPFX_MIN_GLIBC} or newer (Ubuntu 22.04, Debian 12, Fedora 36).")
    endif()
    message(STATUS "glibc ${_libc}")
elseif(NOT APPLE)
    message(FATAL_ERROR "DBPFX supports Windows, Linux and macOS only.")
endif()

# Pre-release tag appended to the numeric version (empty for a final release).
# Keep it in sync with <Version> in managed/Directory.Build.props.
set(DBPFX_VERSION_SUFFIX "canary")
if(DBPFX_VERSION_SUFFIX)
    set(DBPFX_VERSION_STRING "${PROJECT_VERSION}-${DBPFX_VERSION_SUFFIX}")
else()
    set(DBPFX_VERSION_STRING "${PROJECT_VERSION}")
endif()

# Release channel, read from the pre-release tag: canary -> CANARY, dev -> DEVELOPER, beta -> BETA,
# none -> STABLE. Every DBPFX executable gets that channel's icon from DBPFX.Identity.dll (built from
# assets/icons/<CHANNEL>_DBPFX.ico). Executables the build produces but does not compile from these
# sources (CMake's compiler probes) always get the STABLE icon; Microsoft's signed .NET hosts are
# never modified.
string(TOLOWER "${DBPFX_VERSION_SUFFIX}" _dbpfx_suffix)
if(_dbpfx_suffix STREQUAL "")
    set(DBPFX_CHANNEL STABLE)
elseif(_dbpfx_suffix MATCHES "^canary([.-].*)?$")
    set(DBPFX_CHANNEL CANARY)
elseif(_dbpfx_suffix MATCHES "^dev([.-].*)?$")
    set(DBPFX_CHANNEL DEVELOPER)
elseif(_dbpfx_suffix MATCHES "^beta([.-].*)?$")
    set(DBPFX_CHANNEL BETA)
else()
    message(FATAL_ERROR "Unknown pre-release tag '${DBPFX_VERSION_SUFFIX}': use canary, dev, beta or nothing (stable)")
endif()
set(DBPFX_ICON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../assets/icons")
get_filename_component(DBPFX_ICON_DIR "${DBPFX_ICON_DIR}" ABSOLUTE)
message(STATUS "DBPFX ${DBPFX_VERSION_STRING} (${DBPFX_CHANNEL} channel)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
if(MSVC)
    # sources and tests contain UTF-8 text; MSVC would otherwise read them in the system code page
    # (C and C++ only: the resource compiler does not accept it)
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>)
endif()

option(DBPFX_BUILD_TESTS "Build tests" ON)
option(DBPFX_BUILD_TOOLS "Build the command-line tool and benchmark" ON)
option(DBPFX_FETCH_ZLIB "Download and build zlib if it is not installed (typical on Windows)" ON)

# ---------------------------------------------------------------- zlib
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    if(NOT DBPFX_FETCH_ZLIB)
        message(FATAL_ERROR "zlib not found; install it or enable DBPFX_FETCH_ZLIB")
    endif()
    include(FetchContent)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
    # zlib 1.3.1 declares cmake_minimum_required(2.4.4), which CMake 4.x refuses without this floor
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    # a release archive, so git is not required
    FetchContent_Declare(zlib
        URL https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(zlib)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
    target_include_directories(zlibstatic INTERFACE ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
    # only the static library is used: keep zlib's DLL and sample programs out of the build
    foreach(_t zlib example minigzip example64 minigzip64)
        if(TARGET ${_t})
            set_target_properties(${_t} PROPERTIES EXCLUDE_FROM_ALL ON)
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------- core (static, used by everything)
add_library(dbpfx_core STATIC
    src/mapped_file.cpp
    src/compression.cpp
    src/package.cpp
    src/requirements.cpp)
target_include_directories(dbpfx_core PUBLIC include)
# single source of truth for the version reported by dbpfx_version() and dbpfx-cli
target_compile_definitions(dbpfx_core PUBLIC DBPFX_VERSION="${DBPFX_VERSION_STRING}")
target_link_libraries(dbpfx_core PUBLIC ZLIB::ZLIB)
if(MSVC)
    target_compile_options(dbpfx_core PRIVATE /W4 /permissive- /Zc:__cplusplus)
    target_compile_definitions(dbpfx_core PUBLIC _WIN32_WINNT=0x0A00)
else()
    target_compile_options(dbpfx_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------- shared library with the C ABI (P/Invoke target)
add_library(dbpfx SHARED src/c_api.cpp)
target_link_libraries(dbpfx PRIVATE dbpfx_core)
target_include_directories(dbpfx PUBLIC include)
set_target_properties(dbpfx PROPERTIES OUTPUT_NAME "DBPFX.Native")
if(WIN32)
    set_target_properties(dbpfx PROPERTIES PREFIX "")
endif()

# ---------------------------------------------------------------- tools
if(DBPFX_BUILD_TOOLS)
    add_executable(dbpfx-cli tools/cli.cpp)
    target_link_libraries(dbpfx-cli PRIVATE dbpfx_core)
    add_executable(dbpfx-bench tools/bench.cpp)
    target_link_libraries(dbpfx-bench PRIVATE dbpfx_core)
endif()

# ---------------------------------------------------------------- tests
if(DBPFX_BUILD_TESTS)
    enable_testing()
    add_executable(dbpfx-tests tests/tests.cpp)
    target_link_libraries(dbpfx-tests PRIVATE dbpfx_core)
    set(DBPFX_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures)
    add_test(NAME unit COMMAND dbpfx-tests ${DBPFX_FIXTURES})
    add_executable(dbpfx-capi-test tests/capi_test.c)
    target_link_libraries(dbpfx-capi-test PRIVATE dbpfx)
    add_test(NAME capi COMMAND dbpfx-capi-test ${DBPFX_FIXTURES}/ts4.package)
endif()

# ---------------------------------------------------------------- Windows icons and version resources
if(WIN32)
    enable_language(RC)
    include(cmake/resources.cmake)

    # DBPFX.Identity.dll - resource-only library with the STABLE, BETA, CANARY and DEVELOPER icons
    configure_file(cmake/identity.rc.in "${CMAKE_CURRENT_BINARY_DIR}/resources/identity.rc" @ONLY)
    add_library(dbpfx-identity MODULE "${CMAKE_CURRENT_BINARY_DIR}/resources/identity.rc")
    set_target_properties(dbpfx-identity PROPERTIES OUTPUT_NAME "DBPFX.Identity" PREFIX "" SUFFIX ".dll" LINKER_LANGUAGE C)
    if(MSVC)
        target_link_options(dbpfx-identity PRIVATE /NOENTRY /NODEFAULTLIB)
    endif()
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/resources/identity.rc" PROPERTIES OBJECT_DEPENDS
        "${DBPFX_ICON_DIR}/STABLE_DBPFX.ico;${DBPFX_ICON_DIR}/BETA_DBPFX.ico;${DBPFX_ICON_DIR}/CANARY_DBPFX.ico;${DBPFX_ICON_DIR}/DEVELOPER_DBPFX.ico")

    # dbpfx-iconstamp - copies an icon group from DBPFX.Identity.dll into an executable
    add_executable(dbpfx-iconstamp tools/iconstamp.cpp)
    dbpfx_add_version_resource(dbpfx-iconstamp EXE "DBPFX icon stamper")
    if(MSVC)
        target_compile_options(dbpfx-iconstamp PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive->")
    endif()
    add_dependencies(dbpfx-iconstamp dbpfx-identity)
    # a running program cannot rewrite its own resources, so a temporary copy stamps the original
    set(_stamp_copy "${CMAKE_CURRENT_BINARY_DIR}/dbpfx-iconstamp.tmp.exe")
    add_custom_command(TARGET dbpfx-iconstamp POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:dbpfx-iconstamp>" "${_stamp_copy}"
        COMMAND "${_stamp_copy}" "$<TARGET_FILE:dbpfx-identity>" ${DBPFX_CHANNEL} "$<TARGET_FILE:dbpfx-iconstamp>"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${_stamp_copy}"
        VERBATIM)

    dbpfx_add_version_resource(dbpfx DLL "DBPFX native engine")
    foreach(_exe dbpfx-cli dbpfx-bench dbpfx-tests dbpfx-capi-test)
        if(TARGET ${_exe})
            dbpfx_add_version_resource(${_exe} EXE "DBPFX ${_exe}")
            dbpfx_add_channel_icon(${_exe})
        endif()
    endforeach()

    # CMake's compiler identification programs (CMakeFiles/<version>/CompilerId*/CMake*CompilerId.exe)
    # always get the STABLE icon
    file(GLOB_RECURSE _probes LIST_DIRECTORIES false
        "${CMAKE_BINARY_DIR}/CMakeFiles/CMakeCCompilerId.exe"
        "${CMAKE_BINARY_DIR}/CMakeFiles/CMakeCXXCompilerId.exe")
    if(_probes)
        add_custom_command(
            OUTPUT "${CMAKE_BINARY_DIR}/compiler-probes.icon"
            COMMAND dbpfx-iconstamp "$<TARGET_FILE:dbpfx-identity>" STABLE ${_probes}
            COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/compiler-probes.icon"
            DEPENDS dbpfx-iconstamp dbpfx-identity
            COMMENT "Setting the STABLE icon on CMake's compiler probes"
            VERBATIM)
        add_custom_target(dbpfx-probe-icons ALL DEPENDS "${CMAKE_BINARY_DIR}/compiler-probes.icon")
    endif()
endif()

install(TARGETS dbpfx RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
install(FILES include/dbpfx/dbpfx.h DESTINATION include/dbpfx)
