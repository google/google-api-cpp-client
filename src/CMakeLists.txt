add_subdirectory(googleapis)

if(EXISTS "./gmock")
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/gmock/include
  ${CMAKE_CURRENT_SOURCE_DIR}/gmock/gtest/include)
add_subdirectory(gmock)
else()
include_directories(${GMOCK_INCLUDES})
endif()
