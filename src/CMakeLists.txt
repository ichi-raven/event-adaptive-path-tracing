set(MAIN_SOURCE_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Scene.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/SceneBuilder.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Options.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Integrator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Integrators/MotionIntegrator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Integrators/MotionPathIntegrator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Integrators/EventIntegrator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Integrators/EfficientEventIntegrator.cpp
  ${TINYEXR_DIR}/deps/miniz/miniz.c
  ${TINYEXR_DIR}/tinyexr.cc
)

set(MAIN_HEADER_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/Integrator.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/Mesh.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/Material.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/Transform.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/Emitter.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/AnimatedTransform.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/Integrators/MotionIntegrator.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/Integrators/MotionPathIntegrator.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/Integrators/EventIntegrator.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/Integrators/EfficientEventIntegrator.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/SceneBuilder.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/Options.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/Progress.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/Console.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/ConsoleLogging.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/Thresholds.hpp
  ${TINYEXR_DIR}/deps/miniz/miniz.h
  ${TINYEXR_DIR}/tinyexr.h
)

add_executable(${MAIN_EXE})
target_sources(${MAIN_EXE} PRIVATE ${MAIN_SOURCE_FILES} ${MAIN_HEADER_FILES})
target_compile_definitions(${MAIN_EXE} PRIVATE
  "EAPT_SHADER_DIRECTORY=\"${EAPT_SHADER_OUTPUT_DIRECTORY}\"")

# include directories
target_include_directories(
  ${MAIN_EXE} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${Vulkan_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/vk2s/include
  ${STB_DIR}
  ${SLANG_INCLUDE_DIR}
  ${CXXOPTS_DIR}
  ${NLOHMANNJSON_DIR}/single_include
  ${TINYEXR_DIR}
  ${TINYEXR_DIR}/deps/miniz
)

if(USE_OIDN)
  target_include_directories(${MAIN_EXE} PRIVATE ${OIDN_DIR}/include)
endif()

# Link libraries
target_link_libraries(${MAIN_EXE}
  PRIVATE
  Vulkan::Vulkan
  imgui
  glm::glm
  glfw::glfw
  assimp::assimp
  ng-log::ng-log
  ${SLANG_LIBRARY}
  ${VK2S_LIBRARY}
)

if (USE_OPENMP)
  target_link_libraries(${MAIN_EXE} PRIVATE OpenMP::OpenMP_CXX)
endif()

if(MSVC)
  target_compile_options(${MAIN_EXE} PRIVATE /W4)
  target_link_libraries(${MAIN_EXE}
    PRIVATE
      zlibstatic
  )
  set_property(TARGET ${MAIN_EXE} APPEND PROPERTY LINK_FLAGS "/DEBUG /PROFILE /INCREMENTAL:NO")
else()
  target_compile_options(${MAIN_EXE} PRIVATE -Wall)
endif()

if(USE_OIDN)
  target_link_libraries(${MAIN_EXE} PRIVATE ${OIDN_LIB_NAME})
endif()
