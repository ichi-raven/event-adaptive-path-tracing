include(Slangc)

file(GLOB_RECURSE SHADER_DEPS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Slang/*.slang")

compile_slangc(
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/Slang/Integrators/EventIntegrator.slang
  TARGET spirv
  ENTRYPOINTS
    rayGenShader
    missShader
    shadowMissShader
    closestHitShader
  DEPENDENCIES
    ${SHADER_DEPS}
  OUTPUT_DIRECTORY
    ${EAPT_SHADER_OUTPUT_DIRECTORY}
)

compile_slangc(
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/Slang/Integrators/EfficientEventIntegrator.slang
  TARGET spirv
  ENTRYPOINTS
    rayGenShader
    missShader
    shadowMissShader
    closestHitShader
  DEPENDENCIES
    ${SHADER_DEPS}
  OUTPUT_DIRECTORY
    ${EAPT_SHADER_OUTPUT_DIRECTORY}
)

compile_slangc(
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/Slang/Integrators/EfficientEventIntegratorCompute.slang
  TARGET spirv
  ENTRYPOINTS
    prefixSumPass1
    scanBlockSums
    prefixSumPass2
    compaction
  DEPENDENCIES
    ${SHADER_DEPS}
  OUTPUT_DIRECTORY
    ${EAPT_SHADER_OUTPUT_DIRECTORY}
)

compile_slangc(
  FILE ${CMAKE_CURRENT_SOURCE_DIR}/Slang/Integrators/MotionPathIntegrator.slang
  TARGET spirv
  ENTRYPOINTS
    rayGenShader
    missShader
    shadowMissShader
    closestHitShader
  DEPENDENCIES
    ${SHADER_DEPS}
  OUTPUT_DIRECTORY
    ${EAPT_SHADER_OUTPUT_DIRECTORY}
)

add_dependencies(${MAIN_EXE}
  slangc_EventIntegrator
  slangc_EfficientEventIntegrator
  slangc_EfficientEventIntegratorCompute
  slangc_MotionPathIntegrator
)

include(Slangc)
