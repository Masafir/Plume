cmake_minimum_required(VERSION 3.24)
project(PlumeWallpaper VERSION 0.2.0 LANGUAGES CXX RC)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Windows et MSVC sont requis")
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
configure_file(src/resources/plume.rc.in generated/plume.rc @ONLY)
configure_file(src/resources/version.h.in generated/version.h @ONLY)
set(PLUME_SOURCES
  src/main.cpp
  src/application.cpp
  src/config.cpp
  src/desktop.cpp
  src/diagnostics.cpp
  src/lifecycle.cpp
  src/video/gpu_device.cpp
  src/video/playback.cpp
  src/ui/settings.cpp
  src/ui/screen_cards.cpp
  src/ui/gpu_import.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/generated/plume.rc"
  app.manifest
)

# Both executables share the same modules and native build settings.
function(add_plume_executable name)
  add_executable(${name} WIN32 ${PLUME_SOURCES})
  target_compile_features(${name} PRIVATE cxx_std_20)
  target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(${name} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  target_compile_options(${name} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/EHsc;/utf-8> $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/O1;/GL>)
  target_link_options(${name} PRIVATE $<$<CONFIG:Release>:/LTCG> /MANIFEST:EMBED /MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/app.manifest)
  target_link_libraries(${name} PRIVATE d3d11 dxgi mfplat mfreadwrite mfuuid wmcodecdspuuid ole32 shell32 user32 dwmapi wtsapi32 advapi32 comdlg32 comctl32 psapi)
endfunction()
add_plume_executable(PlumeWallpaper)
add_plume_executable(PlumeV0)
target_compile_definitions(PlumeV0 PRIVATE PLUME_V0)

include(CTest)
add_executable(GpuCaps EXCLUDE_FROM_ALL tools/gpu_caps.cpp)
target_compile_features(GpuCaps PRIVATE cxx_std_20)
target_compile_definitions(GpuCaps PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(GpuCaps PRIVATE /W4 /utf-8)
target_link_libraries(GpuCaps PRIVATE d3d11 dxgi dxguid)
if(BUILD_TESTING)
  add_executable(BoundedMp4Tests tests/bounded_mp4_tests.cpp)
  target_compile_features(BoundedMp4Tests PRIVATE cxx_std_20)
  target_compile_definitions(BoundedMp4Tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(BoundedMp4Tests PRIVATE /W4 /EHsc /utf-8)
  target_link_libraries(BoundedMp4Tests PRIVATE mfplat mfuuid ole32)
  if(EXISTS "${CMAKE_SOURCE_DIR}/bojji-4k-h264-lowmemory.mp4" AND EXISTS "${CMAKE_SOURCE_DIR}/bojji-4k-h264-gpu.mp4")
    add_test(NAME BoundedMp4Safety COMMAND BoundedMp4Tests "${CMAKE_SOURCE_DIR}/bojji-4k-h264-lowmemory.mp4" "${CMAKE_SOURCE_DIR}/bojji-4k-h264-gpu.mp4")
  endif()
endif()
