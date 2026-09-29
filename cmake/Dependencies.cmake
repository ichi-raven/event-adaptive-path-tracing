MACRO(resolve_dependencies)
  # glfw
  FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.9
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
  )
  set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "")
  set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "")
  set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "")

  # glm
  FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.2
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
  )

  # Assimp
  FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v6.0.2
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
  )
  set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE INTERNAL "")
  set(ASSIMP_BUILD_TESTS OFF CACHE INTERNAL "")
  set(ASSIMP_BUILD_SAMPLES OFF CACHE INTERNAL "")
  set(ASSIMP_BUILD_DOCS OFF CACHE INTERNAL "")

  # ng-log
  FetchContent_Declare(
    ng-log
    GIT_REPOSITORY https://github.com/ng-log/ng-log.git
    GIT_TAG v0.8.4
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
  )

  # Source-only dependencies. Preserve the existing submodule revisions and
  # our own ImGui / TinyEXR build rules instead of adding upstream targets.
  FetchContent_Declare(
    tinyexr
    GIT_REPOSITORY https://github.com/syoyo/tinyexr.git
    GIT_TAG 8cf0642ca63802a5a07ca6d6cdc0e731c1f5cd6e
    GIT_SUBMODULES ""
    SOURCE_SUBDIR eventadaptivept-source-only
    GIT_PROGRESS TRUE
  )
  FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
    GIT_SUBMODULES ""
    SOURCE_SUBDIR eventadaptivept-source-only
    GIT_PROGRESS TRUE
  )
  FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG 46d39d56febc2a00bdd2270dc88c8a13f2a0441a
    GIT_SUBMODULES ""
    SOURCE_SUBDIR eventadaptivept-source-only
    GIT_PROGRESS TRUE
  )

  # Fetch all dependencies
  FetchContent_MakeAvailable(glfw glm assimp ng-log tinyexr stb imgui)

  # Used by the main project's own source and include lists.
  set(TINYEXR_DIR "${tinyexr_SOURCE_DIR}")
  set(STB_DIR "${stb_SOURCE_DIR}")
  set(IMGUI_DIR "${imgui_SOURCE_DIR}")
  set(IMGUI_BACKEND_DIR "${IMGUI_DIR}/backends")

  # GLFW alias
  if (NOT TARGET glfw::glfw)
    add_library(glfw::glfw ALIAS glfw)
  endif()
ENDMACRO(resolve_dependencies)
