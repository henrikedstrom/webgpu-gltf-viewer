cmake_minimum_required(VERSION 3.22) # CMake version check
project(app)                         # Create project "app"
set(CMAKE_CXX_STANDARD 20)           # Enable C++20 standard

# Add GLM using FetchContent
include(FetchContent)

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.2
)

# Fetch GLM
FetchContent_MakeAvailable(glm)

# Source files
set(SOURCE_FILES
  src/application.cpp
  src/camera.cpp
  src/environment.cpp
  src/environment_preprocessor.cpp
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

# Compiler warnings
if(MSVC)
  target_compile_options(app PRIVATE /W4 /WX)
else()
  target_compile_options(app PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# Per-file warning overrides (third-party)
if(MSVC)
  set_source_files_properties(src/mikktspace.c PROPERTIES COMPILE_OPTIONS "/W0")
else()
  set_source_files_properties(src/mikktspace.c PROPERTIES COMPILE_OPTIONS "-w")
endif()

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

# Treat third-party headers as system headers to silence warnings from them
target_include_directories(app SYSTEM PRIVATE
  third_party/stb_image
  third_party/tiny_gltf
  third_party/glm
)

# Link GLM to the target
target_link_libraries(app PRIVATE glm)

# Dawn settings
set(DAWN_FETCH_DEPENDENCIES ON CACHE BOOL "" FORCE)
if (WIN32)
  # Allow Dawn to fall back to loading system components from System32 on Windows.
  # This avoids runtime failures when SDK redistributable DLLs are not present beside the app.
  set(DAWN_FORCE_SYSTEM_COMPONENT_LOAD ON CACHE BOOL "" FORCE)
endif()

# Temporary MSVC toolset workaround (VS2026 / MSVC 19.50+):
# SPIRV-Tools enables /WX on MSVC and MSVC 19.50 introduced new diagnostics in SPIRV-Tools headers.
# Until this is fixed upstream, suppress the specific warnings that cause the build to fail.
if (MSVC AND MSVC_VERSION GREATER_EQUAL 1950)
  add_compile_options(/wd5232 /wd4717)
endif()

add_subdirectory("third_party/dawn" EXCLUDE_FROM_ALL)

# Emscripten-specific settings
if(EMSCRIPTEN)
  set_target_properties(app PROPERTIES SUFFIX ".html")
  target_link_libraries(app PRIVATE emdawnwebgpu_cpp webgpu_glfw)
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
  target_link_options(app PRIVATE 
    -sASYNCIFY=1 
    -sUSE_GLFW=3 
    -sALLOW_MEMORY_GROWTH=1 
    -O3
    --preload-file "${CMAKE_SOURCE_DIR}/assets@/assets"
    "-sEXPORTED_FUNCTIONS=[\"_wasm_OnDropFile\", \"_main\", \"_malloc\", \"_free\"]"
    "-sEXPORTED_RUNTIME_METHODS=[\"ccall\", \"cwrap\", \"stringToUTF8\", \"lengthBytesUTF8\"]"
  )
else()
  # Non-Emscripten settings
  target_link_libraries(app PRIVATE webgpu_dawn webgpu_glfw glfw)
endif()
