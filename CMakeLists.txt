cmake_minimum_required(VERSION 3.15)
project(PotatoPatch VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# DirectX 12 is included with the Windows SDK, no find_package needed

# Source files
set(SOURCES
        src/main.cpp
        src/Application.cpp
        src/Core/D3D12Context.cpp
        src/Core/CommandQueue.cpp
        src/Core/DescriptorHeap.cpp
        src/Capture/CaptureEngine.cpp
        src/Capture/DesktopDuplication.cpp
        src/Processing/Upscaler.cpp
        src/Processing/FrameGenerator.cpp
        src/Processing/GPUProcessor.cpp
        src/Display/DisplayManager.cpp
        src/UI/ImGuiLayer.cpp
        src/Utils/Logger.cpp
        src/Utils/Timer.cpp
)

set(HEADERS
        src/Application.h
        src/Core/D3D12Context.h
        src/Core/CommandQueue.h
        src/Core/DescriptorHeap.h
        src/Capture/CaptureEngine.h
        src/Capture/DesktopDuplication.h
        src/Processing/Upscaler.h
        src/Processing/FrameGenerator.h
        src/Processing/GPUProcessor.h
        src/Display/DisplayManager.h
        src/UI/ImGuiLayer.h
        src/Utils/Logger.h
        src/Utils/Timer.h
)

# ImGui sources
set(IMGUI_SOURCES
        external/imgui/imgui.cpp
        external/imgui/imgui_draw.cpp
        external/imgui/imgui_tables.cpp
        external/imgui/imgui_widgets.cpp
        external/imgui/backends/imgui_impl_win32.cpp
        external/imgui/backends/imgui_impl_dx12.cpp
        external/d3dx12.h
)

# Create executable (Console app for debugging)
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${IMGUI_SOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/external
        ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
        d3d12.lib
        d3d11.lib
        dxgi.lib
        d3dcompiler.lib
        dxguid.lib
)

# Compile shaders
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(COMPILED_SHADER_DIR ${CMAKE_BINARY_DIR}/shaders)

file(MAKE_DIRECTORY ${COMPILED_SHADER_DIR})

# Shader compilation function
function(compile_shader SHADER_FILE SHADER_TYPE ENTRY_POINT)
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
    set(OUTPUT_FILE ${COMPILED_SHADER_DIR}/${SHADER_NAME}.cso)

    add_custom_command(
            OUTPUT ${OUTPUT_FILE}
            COMMAND fxc /T ${SHADER_TYPE} /E ${ENTRY_POINT} /Fo ${OUTPUT_FILE} ${SHADER_DIR}/${SHADER_FILE}
            DEPENDS ${SHADER_DIR}/${SHADER_FILE}
            COMMENT "Compiling shader ${SHADER_FILE}"
    )

    target_sources(${PROJECT_NAME} PRIVATE ${OUTPUT_FILE})
endfunction()

# Compile all shaders
compile_shader(BilinearUpscale.hlsl cs_5_1 CSMain)
compile_shader(FSRUpscale.hlsl cs_5_1 CSMain)
compile_shader(Copy.hlsl cs_5_1 CSMain)

# Set working directory for debugging
set_property(TARGET ${PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

# Copy runtime files
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders ${CMAKE_BINARY_DIR}/shaders
)