# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles\\PortProbeQt_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\PortProbeQt_autogen.dir\\ParseCache.txt"
  "PortProbeQt_autogen"
  )
endif()
