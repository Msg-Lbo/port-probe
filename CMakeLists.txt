cmake_minimum_required(VERSION 3.16)

project(PortProbeQt VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(MSVC)
  add_compile_options(/Zc:__cplusplus)
  add_compile_options(/utf-8)
endif()

# Win7 release target: require Qt5 explicitly.
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Network)

set(APP_ICON_SOURCE app_icon.rc)

if(MINGW)
  get_filename_component(_cxx_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
  if(EXISTS "${_cxx_dir}/windres.exe")
    set(CMAKE_RC_COMPILER "${_cxx_dir}/windres.exe" CACHE FILEPATH "RC compiler" FORCE)
  endif()
endif()

add_executable(PortProbeQt WIN32
  ${APP_ICON_SOURCE}
  app/resources.qrc
  app/main.cpp
  app/MainWindow.h
  app/MainWindow.cpp
  app/ConfigDialog.h
  app/ConfigDialog.cpp
  app/QrCodeDialog.h
  app/QrCodeDialog.cpp
  app/DeviceTableModel.h
  app/DeviceTableModel.cpp
  core/Models.h
  core/Hex.h
  core/Hex.cpp
  core/SettingsStore.h
  core/SettingsStore.cpp
  core/NicService.h
  core/NicService.cpp
  core/UdpScanner.h
  core/UdpScanner.cpp
  core/DeviceParser.h
  core/DeviceParser.cpp
  core/Admin.h
  core/Admin.cpp
)

target_include_directories(PortProbeQt PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(PortProbeQt PRIVATE
  Qt5::Core
  Qt5::Gui
  Qt5::Widgets
  Qt5::Network
  shell32
  ws2_32
  iphlpapi
)

