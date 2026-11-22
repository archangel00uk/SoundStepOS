cmake_minimum_required(VERSION 3.16)
project(SoundStep LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
  message(WARNING "SoundStep targets Windows/WASAPI and is designed for MSVC.")
endif()

# Statically link the C/C++ runtime so the .exe is self-contained and can be
# shared without requiring the Visual C++ Redistributable on the target machine.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(SoundStep
  src/main.cpp
  src/audio/audio_pipeline.cpp
  src/audio/process_loopback_capture.cpp
  src/audio/process_monitor.cpp
  src/audio/wasapi_renderer.cpp
  src/gui/gui_controller.cpp
  src/resource.rc
)

target_include_directories(SoundStep PRIVATE src)

# Process loopback capture requires Windows 10 build 20348 / Windows 11 at
# runtime (the SDK header is gated on NTDDI_WIN10_FE).
target_compile_definitions(SoundStep PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  UNICODE
  _UNICODE
  _WIN32_WINNT=0x0A00
  NTDDI_VERSION=0x0A00000A
)

if(MSVC)
  target_compile_options(SoundStep PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

target_link_libraries(SoundStep PRIVATE
  ole32
  uuid
  avrt
  mmdevapi
  shell32
  gdiplus
  dwmapi
)
