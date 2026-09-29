# Compile shader
function(compile_slangc)
  # Parse arguments
  set(options OPTIONAL)
  set(oneValueArgs FILE TARGET OUTPUT_DIRECTORY)
  set(multiValueArgs ENTRYPOINTS DEPENDENCIES)
  cmake_parse_arguments(compile_slangc "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if (NOT compile_slangc_FILE)
    message(FATAL_ERROR "No shader file specified for slangc compilation!")
  else()
    set(shader_file ${compile_slangc_FILE})
  endif()

  if (NOT compile_slangc_TARGET)
    set(shader_target "spirv")
  else()
    set(shader_target ${compile_slangc_TARGET})
  endif()

  if (NOT compile_slangc_OUTPUT_DIRECTORY)
    message(FATAL_ERROR "No output directory specified for slangc compilation!")
  else()
    set(out_dir ${compile_slangc_OUTPUT_DIRECTORY})
  endif()

  list(LENGTH compile_slangc_ENTRYPOINTS entry_count)
  if (entry_count EQUAL 0)
    set(entrypoints "main")
  else()
    set(entrypoints ${compile_slangc_ENTRYPOINTS})
  endif()

  get_filename_component(shader_name ${shader_file} NAME_WE)

  file(MAKE_DIRECTORY ${out_dir})
  message(STATUS "Compiling shader: ${shader_file}")

  set(shader_outputs)
  foreach(entry IN LISTS entrypoints)
    set(shader_out ${out_dir}/${shader_name}_${entry}.spv)
    set(target_name "slangc_${shader_name}_${entry}")

    add_custom_command(
      OUTPUT ${shader_out}
      COMMAND
        ${CMAKE_COMMAND} -E make_directory ${out_dir}
      COMMAND
        ${SLANGC_EXECUTABLE} ${shader_file}
        -o ${shader_out}
        -entry ${entry}
        -target ${shader_target}
        -profile "spirv_1_6"
        -emit-spirv-directly
        -matrix-layout-column-major
        -fvk-use-entrypoint-name
        -fvk-invert-y
        -capability "spvImageQuery"
        -capability "spvSparseResidency"
        -capability "spvRayTracingMotionBlurNV"
        -capability "SPV_GOOGLE_user_type"
        -capability "spvMinLod"
        -capability "spvDerivativeControl"
        -capability "spvImageGatherExtended"
        -capability "spvFragmentFullyCoveredEXT"
      DEPENDS
        ${shader_file}
        ${compile_slangc_DEPENDENCIES}
        "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Compiling shader ${shader_name}:${entry}"
      VERBATIM
    )

    list(APPEND shader_outputs ${shader_out})
  endforeach()

  add_custom_target("slangc_${shader_name}" ALL DEPENDS ${shader_outputs})
  set_target_properties("slangc_${shader_name}" PROPERTIES FOLDER "Shaders")
endfunction()
