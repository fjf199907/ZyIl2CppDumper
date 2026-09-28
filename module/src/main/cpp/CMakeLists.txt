cmake_minimum_required(VERSION 3.18.1)

if (NOT DEFINED MODULE_NAME)
    message(FATAL_ERROR "MODULE_NAME is not set")
else ()
    project(${MODULE_NAME})
endif ()

message("Build type: ${CMAKE_BUILD_TYPE}")

set(CMAKE_CXX_STANDARD 20)

set(LINKER_FLAGS "-ffixed-x18 -Wl,--hash-style=both")
set(C_FLAGS "-Werror=format -fdata-sections -ffunction-sections")
set(CXX_FLAGS "${CXX_FLAGS} -fno-exceptions -fno-rtti")

if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(C_FLAGS "${C_FLAGS} -O2 -fvisibility=hidden -fvisibility-inlines-hidden")
    set(LINKER_FLAGS "${LINKER_FLAGS} -Wl,-exclude-libs,ALL -Wl,--gc-sections -Wl,--strip-all")
else ()
    set(C_FLAGS "${C_FLAGS} -O0")
endif ()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${C_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${C_FLAGS} ${CXX_FLAGS}")

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${LINKER_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${LINKER_FLAGS}")

include_directories(
        xdl/include
)

aux_source_directory(xdl xdl-src)

add_library(${MODULE_NAME} SHARED
        main.cpp
        hack.cpp
        il2cpp_dump.cpp
        ${xdl-src})
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(DOBBY_DEBUG OFF CACHE BOOL "" FORCE)
set(Plugin.SymbolResolver OFF CACHE BOOL "" FORCE)
set(dobby_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Dobby")
if (NOT EXISTS "${dobby_SOURCE_DIR}/include/dobby.h" OR NOT EXISTS "${dobby_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Incomplete healthhook sources: upload module/src/main/cpp/third_party/Dobby in full, together with module/build.gradle and module/src/main/cpp/CMakeLists.txt.")
endif()
message(STATUS "MAJO healthhook: Dobby=${dobby_SOURCE_DIR}; ABI=${ANDROID_ABI}")
enable_language(ASM)
add_subdirectory("${dobby_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/dobby" EXCLUDE_FROM_ALL)
# Generate ELF assembly from the pinned upstream Mach-O-style ARM64 sources.
if (ANDROID AND ANDROID_ABI STREQUAL "arm64-v8a")
    get_target_property(dobby_sources dobby_static SOURCES)
    list(FILTER dobby_sources EXCLUDE REGEX "[.]asm$")
    set_property(TARGET dobby_static PROPERTY SOURCES "${dobby_sources}")
    foreach(bridge closure_bridge_arm64 closure_trampoline_arm64)
        file(READ "${dobby_SOURCE_DIR}/source/TrampolineBridge/ClosureTrampolineBridge/arm64/${bridge}.asm" asm_source)
        string(REPLACE "cdecl(common_closure_bridge_handler)@PAGEOFF" ":lo12:common_closure_bridge_handler" asm_source "${asm_source}")
        string(REPLACE "cdecl(common_closure_bridge_handler)@PAGE" "common_closure_bridge_handler" asm_source "${asm_source}")
        string(REPLACE "#closure_tramp_entry_addr" "closure_tramp_entry_addr" asm_source "${asm_source}")
        string(REPLACE "#closure_bridge_addr" "closure_bridge_addr" asm_source "${asm_source}")
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${bridge}.S" "${asm_source}")
        target_sources(dobby_static PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${bridge}.S")
    endforeach()
endif()
target_include_directories(${MODULE_NAME} PRIVATE ${dobby_SOURCE_DIR}/include)
target_link_libraries(${MODULE_NAME} log dobby_static)

if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_custom_command(TARGET ${MODULE_NAME} POST_BUILD
            COMMAND ${CMAKE_STRIP} --strip-all --remove-section=.comment "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/lib${MODULE_NAME}.so")
endif ()
