cmake_minimum_required(VERSION 3.13) # CMake version check
project(app)                         # Create project "app"
set(CMAKE_CXX_STANDARD 20)           # Enable C++20 standard

add_executable(app "main.cpp")

# Include directories for third-party libraries
target_include_directories(app PRIVATE
  third_party/stb_image
  third_party/tiny_gltf
)

if(EMSCRIPTEN)
  set_target_properties(app PROPERTIES SUFFIX ".html")
  target_link_options(app PRIVATE 
    -sUSE_WEBGPU=1 
    -sUSE_GLFW=3 
    -sALLOW_MEMORY_GROWTH=1 
    -O3
    --preload-file ${CMAKE_SOURCE_DIR}/assets@/assets
  )
else()
  set(DAWN_FETCH_DEPENDENCIES ON)
  add_subdirectory("third_party/dawn" EXCLUDE_FROM_ALL)
  target_link_libraries(app PRIVATE dawn::webgpu_dawn glfw webgpu_glfw)
endif()