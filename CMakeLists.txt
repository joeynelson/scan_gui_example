cmake_minimum_required (VERSION 3.1)
project(scan_gui_example)

include(FetchContent) 
FetchContent_Declare( mahi-gui 
                      GIT_REPOSITORY https://github.com/joeynelson/mahi-gui.git
                      GIT_TAG origin/update_implot) 
FetchContent_MakeAvailable(mahi-gui)

FetchContent_Declare( pinchot 
                      GIT_REPOSITORY https://github.com/joeynelson/pinchot-c-api.git
                      GIT_TAG origin/improving_cmake ) 

FetchContent_MakeAvailable(pinchot)

add_executable(scan_gui_example
  ${CMAKE_CURRENT_SOURCE_DIR}/src/scan_gui_example.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/jsCircleHough.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/jsCircleHough.h
  )

  
target_link_libraries(scan_gui_example mahi::gui pinchot)
