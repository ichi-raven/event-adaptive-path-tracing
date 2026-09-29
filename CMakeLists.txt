cmake_minimum_required(VERSION 3.18 FATAL_ERROR)
project(eapt LANGUAGES C CXX)

list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(FetchContent)
include(Dependencies)

# Options
if (UNIX AND NOT APPLE)
  set(LINUX TRUE)
else()
  set(LINUX FALSE)
endif()

option(USE_OIDN "Use Open Image Denoise library" OFF)
option(USE_OPENMP "Use OpenMP" ON)

# Warning for macOS
if (APPLE)
  message(WARNING "macOS is not officially supported. Some features may not work as expected.")
endif()

# Common targets
set(VK2S_LIBRARY "vk2s")
set(MAIN_EXE "eapt")

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Macro
add_definitions("-DTINYEXR_USE_MINIZ")
add_definitions("-D_CRT_SECURE_NO_WARNINGS")

if(USE_OIDN)
  add_definitions("-DWITH_OIDN")
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  add_compile_options(-Wno-unused-parameter -Wno-unused-variable)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  add_compile_options(/MP /source-charset:utf-8)
  add_compile_options(/wd4100 /wd4099)

  # replace /MDd to /MTd in all projects
  string(REPLACE "/MDd" "/MTd" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
  string(REPLACE "/MDd" "/MTd" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

  # replace /MD to /MT in all projects
  string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
  string(REPLACE "/MD" "/MT" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
  string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL}")
  string(REPLACE "/MD" "/MT" CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL}")
  string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
  string(REPLACE "/MD" "/MT" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
endif()

# OpenMP
if (USE_OPENMP)
  find_package(OpenMP REQUIRED COMPONENTS CXX)
endif()

# Vulkan
find_package(Vulkan 1.3 REQUIRED)

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(EAPT_SHADER_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")

foreach(CONFIG_TYPE Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER ${CONFIG_TYPE} CONFIG)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG} ${CMAKE_BINARY_DIR}/lib)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG} ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG} ${CMAKE_BINARY_DIR}/bin)
endforeach()

set(CMAKE_DEBUG_POSTFIX "-debug")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Resolve all dependencies
set(FETCHCONTENT_BASE_DIR ${CMAKE_BINARY_DIR}/_deps)
resolve_dependencies()
if (APPLE)
  add_definitions(-DGL_SILENCE_DEPRECATION)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -framework Cocoa -framework IOKit -framework CoreVideo")
endif()

# Header-only dependencies are still vendored to preserve their exact contents.
set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(CXXOPTS_DIR "${THIRD_PARTY_DIR}/cxxopts")
set(NLOHMANNJSON_DIR "${THIRD_PARTY_DIR}/nlohmann-json")

# Build ImGui from its FetchContent source directory.
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_BACKEND_DIR}/imgui_impl_vulkan.cpp
  ${IMGUI_BACKEND_DIR}/imgui_impl_glfw.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_BACKEND_DIR})
target_link_libraries(imgui PUBLIC Vulkan::Vulkan glfw::glfw)

# Open Image Denoise
if(USE_OIDN)
  set(OIDN_DIR "C:/oidn-2.3.3.x64.windows" CACHE PATH "Open Image Denoise installation directory")
  set(OIDN_LIB_NAME OpenImageDenoise)
endif()

# Slang
get_filename_component(VULKAN_LIBRARY_DIR "${Vulkan_LIBRARY}" DIRECTORY)

message(STATUS "Using bundled Slang with Vulkan backend")
find_program(SLANGC_EXECUTABLE NAMES slangc REQUIRED)
find_path(SLANG_INCLUDE_DIR NAMES slang.h PATHS ${Vulkan_INCLUDE_DIR}/slang REQUIRED)
find_library(SLANG_LIBRARY NAMES slang HINTS ${Vulkan_LIBRARY_DIR} $ENV{VULKAN_SDK}/lib $ENV{VULKAN_SDK}/Lib REQUIRED)
message(STATUS "SLANG_INCLUDE_DIR: ${SLANG_INCLUDE_DIR}")

add_subdirectory(src)
add_subdirectory(vk2s)
add_subdirectory(shaders)
