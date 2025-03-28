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

# Source files
set(SOURCE_FILES
  src/application.cpp
  src/camera.cpp
  src/environment.cpp
  src/environment_preprocessor.cpp
  src/float16.cpp
  src/main.cpp
  src/mipmap_generator.cpp
  src/mikktspace.c
  src/mesh_utils.cpp
  src/model.cpp
  src/orbit_controls.cpp
  src/panorama_to_cubemap_converter.cpp
  src/renderer.cpp
)

# Header files
set(HEADER_FILES
  src/application.h
  src/camera.h
  src/environment.h
  src/environment_preprocessor.h
  src/float16.h
  src/mipmap_generator.h
  src/mikktspace.h
  src/mesh_utils.h
  src/model.h
  src/orbit_controls.h
  src/panorama_to_cubemap_converter.h
  src/renderer.h
)

# Add executable
add_executable(app ${SOURCE_FILES} ${HEADER_FILES})

# Organize files in IDEs (e.g., Visual Studio, Xcode)
source_group("Source Files" FILES ${SOURCE_FILES})
source_group("Header Files" FILES ${HEADER_FILES})

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
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
  target_link_options(app PRIVATE 
    -sUSE_WEBGPU=1 
    -sUSE_GLFW=3 
    -sALLOW_MEMORY_GROWTH=1 
    -O3
    --preload-file "${CMAKE_SOURCE_DIR}/assets@/assets"
    "-sEXPORTED_FUNCTIONS=[\"_wasm_OnDropFile\", \"_main\", \"_malloc\", \"_free\"]"
    "-sEXPORTED_RUNTIME_METHODS=[\"ccall\", \"cwrap\", \"stringToUTF8\", \"lengthBytesUTF8\"]"
  )
else()
  # Non-Emscripten settings
  set(DAWN_FETCH_DEPENDENCIES ON)
  add_subdirectory("third_party/dawn" EXCLUDE_FROM_ALL)
  target_link_libraries(app PRIVATE dawn::webgpu_dawn glfw webgpu_glfw)
endif()
