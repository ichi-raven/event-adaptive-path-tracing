
set(VK2S_SOURCE_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/AccelerationStructure.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Buffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/BindGroup.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/BindLayout.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Camera.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Command.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Compiler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Device.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/DynamicBuffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Fence.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Image.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Pipeline.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/RenderPass.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Sampler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Semaphore.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Shader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ShaderBindingTable.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Window.cpp
)

set(VK2S_HEADER_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/AccelerationStructure.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/Buffer.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/BindGroup.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/BindLayout.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/Camera.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/Command.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/Compiler.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/Device.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/Exception.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/DynamicBuffer.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/Fence.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/Image.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/Pipeline.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/RenderPass.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/Sampler.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/Semaphore.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/Shader.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/SlotMap.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/ShaderBindingTable.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/vk2s/Window.hpp
)

# Create library
add_library(${VK2S_LIBRARY} STATIC ${VK2S_SOURCE_FILES} ${VK2S_HEADER_FILES})

# Link libraries
target_include_directories(
  ${VK2S_LIBRARY}
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${SLANG_INCLUDE_DIR}
    ${STB_DIR}
)

target_link_libraries(${VK2S_LIBRARY}
  PUBLIC
    Vulkan::Vulkan
    imgui
    glm::glm
    glfw::glfw
  PRIVATE
    ${SLANG_LIBRARY}
)

if(MSVC)
  target_link_libraries(${VK2S_LIBRARY}
    PUBLIC
      zlibstatic
  )
  target_compile_options(${VK2S_LIBRARY} PRIVATE /W4)
  set_property(TARGET ${VK2S_LIBRARY} APPEND PROPERTY LINK_FLAGS "/DEBUG /PROFILE /INCREMENTAL:NO")
else()
  target_compile_options(${VK2S_LIBRARY} PRIVATE -Wall)
endif()
