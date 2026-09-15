cmake_minimum_required(VERSION 3.25)

project(FastClicker VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)
include(GNUInstallDirs)

option(AUTOCLICKER_BUILD_GUI "Build the Dear ImGui client" ON)
option(AUTOCLICKER_BUILD_DAEMON "Build the Linux evdev/uinput backend" ON)
option(AUTOCLICKER_BUILD_TESTS "Build unit tests" ON)
option(AUTOCLICKER_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(AUTOCLICKER_USE_SYSTEM_DEPS "Prefer installed GLFW and nlohmann_json packages" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
find_package(X11 REQUIRED)

if(AUTOCLICKER_USE_SYSTEM_DEPS)
    find_package(nlohmann_json CONFIG REQUIRED)
else()
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

function(autoclicker_target_defaults Target)
    target_compile_features(${Target} PUBLIC cxx_std_23)
    target_compile_options(${Target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow;-Wno-missing-field-initializers;-Werror=return-type>
    )
    if(AUTOCLICKER_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${Target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${Target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

add_library(autoclicker_common STATIC
    src/Core/Configuration.cpp
    src/Ipc/Protocol.cpp
    src/Ipc/Socket.cpp
    src/Utils/Error.cpp
    src/Utils/KeyNames.cpp
    src/Utils/Logger.cpp
    src/Utils/Paths.cpp
    src/Utils/UniqueFd.cpp
)
target_include_directories(autoclicker_common PUBLIC src)
target_link_libraries(autoclicker_common PUBLIC nlohmann_json::nlohmann_json Threads::Threads)
autoclicker_target_defaults(autoclicker_common)

# Application discovery is pure POSIX file reading, so it stays out of the X11 library and the
# tests can exercise its parsers without a display server.
add_library(autoclicker_desktop STATIC
    src/Input/ApplicationInventory.cpp
)
target_include_directories(autoclicker_desktop PUBLIC src)
target_link_libraries(autoclicker_desktop PUBLIC autoclicker_common)
autoclicker_target_defaults(autoclicker_desktop)

add_library(autoclicker_focus STATIC
    src/Input/WindowFocusTracker.cpp
)
target_include_directories(autoclicker_focus PUBLIC src)
target_link_libraries(autoclicker_focus PUBLIC autoclicker_common autoclicker_desktop X11::X11)
autoclicker_target_defaults(autoclicker_focus)

add_library(autoclicker_scheduler STATIC
    src/Core/ClickScheduler.cpp
    src/Input/HotkeyManager.cpp
)
target_include_directories(autoclicker_scheduler PUBLIC src)
target_link_libraries(autoclicker_scheduler PUBLIC autoclicker_common)
autoclicker_target_defaults(autoclicker_scheduler)

if(AUTOCLICKER_BUILD_DAEMON)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)
pkg_check_modules(LIBUDEV REQUIRED IMPORTED_TARGET libudev)

add_library(autoclicker_engine STATIC
    src/Input/DeviceGrab.cpp
    src/Input/InputListener.cpp
    src/Input/VirtualMouse.cpp
)
target_include_directories(autoclicker_engine PUBLIC src)
target_link_libraries(autoclicker_engine
    PUBLIC autoclicker_scheduler PkgConfig::LIBEVDEV PkgConfig::LIBUDEV
)
target_link_directories(autoclicker_engine PUBLIC ${LIBEVDEV_LIBRARY_DIRS} ${LIBUDEV_LIBRARY_DIRS})
autoclicker_target_defaults(autoclicker_engine)

add_executable(autoclickerd
    src/Apps/Daemon/Main.cpp
    src/Core/Daemon.cpp
    src/Core/ShutdownCoordinator.cpp
)
target_link_libraries(autoclickerd PRIVATE autoclicker_engine autoclicker_focus)
autoclicker_target_defaults(autoclickerd)
endif()

if(AUTOCLICKER_BUILD_GUI)
    find_package(OpenGL REQUIRED)
    if(AUTOCLICKER_USE_SYSTEM_DEPS)
        find_package(glfw3 CONFIG REQUIRED)
    else()
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
            glfw
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG 3.4
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(glfw)
    endif()

    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(imgui)

    add_library(imgui_backend STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui_backend PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(imgui_backend PUBLIC glfw OpenGL::GL)
    target_compile_features(imgui_backend PUBLIC cxx_std_11)

    add_executable(autoclicker-gui
        src/Apps/Gui/Main.cpp
        src/Gui/Application.cpp
        src/Gui/DaemonClient.cpp
    )
    target_link_libraries(autoclicker-gui PRIVATE autoclicker_common autoclicker_desktop autoclicker_focus imgui_backend)
    autoclicker_target_defaults(autoclicker-gui)
endif()

if(AUTOCLICKER_BUILD_TESTS)
    enable_testing()
    add_executable(autoclicker_tests
        src/Tests/TestMain.cpp
        src/Tests/ApplicationInventoryTests.cpp
        src/Tests/ConfigurationTests.cpp
        src/Tests/HotkeyTests.cpp
        src/Tests/KeyNameTests.cpp
        src/Tests/SchedulerTests.cpp
        src/Tests/ProtocolTests.cpp
    )
    target_link_libraries(autoclicker_tests PRIVATE autoclicker_scheduler autoclicker_desktop)
    autoclicker_target_defaults(autoclicker_tests)
    add_test(NAME autoclicker_tests COMMAND autoclicker_tests)
endif()

if(TARGET autoclickerd)
    install(TARGETS autoclickerd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(TARGET autoclicker-gui)
    install(TARGETS autoclicker-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(FILES packaging/70-autoclicker-uinput.rules
    DESTINATION ${CMAKE_INSTALL_DATADIR}/fastclicker
)
