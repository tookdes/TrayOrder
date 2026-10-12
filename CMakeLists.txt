cmake_minimum_required(VERSION 3.24)
project(TrayOrder VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "TrayOrder only supports Windows")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "TrayOrder currently supports x64 Windows only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
set(MINHOOK_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MINHOOK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(minhook)

add_library(TrayOrderHook SHARED
    src/hook_entry.cpp
    src/tray_order_core.cpp
    src/compat_runtime.cpp
    src/symbols.cpp
)
target_include_directories(TrayOrderHook PRIVATE src)
target_compile_definitions(TrayOrderHook PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(TrayOrderHook PRIVATE minhook advapi32 dbghelp ole32 shell32 user32 winhttp)
set_target_properties(TrayOrderHook PROPERTIES OUTPUT_NAME "TrayOrderHook")

add_executable(TrayOrder WIN32
    src/controller.cpp
    src/resources.rc
)
target_include_directories(TrayOrder PRIVATE src)
target_compile_definitions(TrayOrder PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(TrayOrder PRIVATE advapi32 shell32 user32)

if(MSVC)
    foreach(target TrayOrder TrayOrderHook)
        target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive- /EHsc)
    endforeach()
    target_compile_options(TrayOrderHook PRIVATE /wd4100 /wd4189 /wd4244 /wd4267)
endif()

install(TARGETS TrayOrder TrayOrderHook RUNTIME DESTINATION . LIBRARY DESTINATION .)
