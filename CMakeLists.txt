cmake_minimum_required(VERSION 3.13) # CMake version check
project(app)                         # Create project "app"
set(CMAKE_CXX_STANDARD 20)           # Enable C++20 standard

# Add GLM using FetchContent
include(FetchContent)

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 0.9.9.8 # Replace with desired version
)

# Fetch GLM
FetchContent_MakeAvailable(glm)

# Add executable and source files
add_executable(app
  src/application.cpp
  src/camera.cpp
  src/environment.cpp
  src/float16.cpp
  src/main.cpp
  src/model.cpp
  src/orbit_controls.cpp
  src/renderer.cpp
)

# Include directories for third-party libraries and project headers
target_include_directories(app PRIVATE
  third_party/stb_image
  third_party/tiny_gltf
  third_party/glm          # Include GLM
  src                      # Include src for project headers (e.g., camera.h)
)

# Link GLM to the target
target_link_libraries(app PRIVATE glm)

# Emscripten-specific settings
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
  # Non-Emscripten settings
  set(DAWN_FETCH_DEPENDENCIES ON)
  add_subdirectory("third_party/dawn" EXCLUDE_FROM_ALL)
  target_link_libraries(app PRIVATE dawn::webgpu_dawn glfw webgpu_glfw)
endif()
