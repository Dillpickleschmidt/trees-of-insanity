cmake_minimum_required(VERSION 3.25)

project(trees_of_insanity VERSION 0.1.0 LANGUAGES CXX)

option(TOI_BUILD_TESTS "Build native core tests" ON)
option(TOI_ENABLE_VIEWPORT "Build the Vulkan/X11 native viewport into the core" OFF)
option(TOI_ENABLE_OVRTX "Build the ovrtx CUDA growth-preview renderer" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
include(CTest)

find_package(nlohmann_json CONFIG REQUIRED)

add_library(toi_growth
    src/toi/growth/src/growth.cpp
)
target_include_directories(toi_growth PUBLIC
    src/toi/growth/include
)
target_compile_features(toi_growth PUBLIC cxx_std_23)

add_library(toi_import
    src/toi/import/src/obj_importer.cpp
)
target_include_directories(toi_import PUBLIC
    src/toi/import/include
)
target_link_libraries(toi_import PUBLIC toi_growth)
target_compile_features(toi_import PUBLIC cxx_std_23)

add_library(toi_plant
    src/toi/plant/src/plant.cpp
)
target_include_directories(toi_plant PUBLIC
    src/toi/plant/include
)
target_link_libraries(toi_plant PUBLIC toi_growth toi_import)
target_compile_features(toi_plant PUBLIC cxx_std_23)

add_library(toi_render
    src/toi/render/src/orbit_view.cpp
    src/toi/render/src/render_projection.cpp
    src/toi/render/src/viewport_guides.cpp
)
target_include_directories(toi_render PUBLIC
    src/toi/render/include
)
target_link_libraries(toi_render PUBLIC toi_growth)
target_compile_features(toi_render PUBLIC cxx_std_23)

add_library(toi_project
    src/toi/project/src/project.cpp
)
target_include_directories(toi_project PUBLIC
    src/toi/project/include
)
target_link_libraries(toi_project PUBLIC toi_growth nlohmann_json::nlohmann_json)
target_compile_features(toi_project PUBLIC cxx_std_23)

add_library(toi_app
    src/toi/app/src/application_controller.cpp
    src/toi/app/src/application_commands.cpp
)
target_include_directories(toi_app PUBLIC
    src/toi/app/include
)
target_link_libraries(toi_app PUBLIC
    toi_growth
    toi_import
    toi_project
    toi_render
    nlohmann_json::nlohmann_json
)
target_compile_features(toi_app PUBLIC cxx_std_23)
target_compile_definitions(toi_app PUBLIC
    TOI_DEFAULT_ASSET_ROOT_PATH="${CMAKE_SOURCE_DIR}/assets"
    TOI_DEFAULT_PROTOTYPE_ASSET_PATH="${CMAKE_SOURCE_DIR}/assets/prototypes/TOI_Module_Prototypes.obj"
)

if(TOI_ENABLE_VIEWPORT)
    find_package(Vulkan REQUIRED)
    find_package(X11 REQUIRED)
    find_program(GLSLC_EXECUTABLE glslc REQUIRED)
    find_program(XXD_EXECUTABLE xxd REQUIRED)

    set(TOI_SHADER_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/toi/viewport/shaders)
    file(MAKE_DIRECTORY ${TOI_SHADER_GEN_DIR})
    function(toi_compile_shader out_var source symbol)
        get_filename_component(name ${source} NAME)
        set(spv ${TOI_SHADER_GEN_DIR}/${name}.spv)
        set(cpp ${TOI_SHADER_GEN_DIR}/${name}.spv.cpp)
        add_custom_command(
            OUTPUT ${cpp}
            COMMAND ${GLSLC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${spv}
            COMMAND ${XXD_EXECUTABLE} -i -n ${symbol} ${spv} ${cpp}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${source}
            VERBATIM
        )
        set_source_files_properties(${cpp} PROPERTIES GENERATED TRUE)
        set(${out_var} ${cpp} PARENT_SCOPE)
    endfunction()
    toi_compile_shader(TOI_OVERLAY_VERT_SPV_CPP src/toi/viewport/shaders/overlay_lines.vert overlay_lines_vert_spv)
    toi_compile_shader(TOI_OVERLAY_FRAG_SPV_CPP src/toi/viewport/shaders/overlay_lines.frag overlay_lines_frag_spv)

    add_library(toi_viewport
        src/toi/viewport/src/error.cpp
        src/toi/viewport/src/x11_vulkan_surface.cpp
        src/toi/viewport/src/vulkan_context.cpp
        src/toi/viewport/src/vulkan_swapchain.cpp
        src/toi/viewport/src/viewport_overlay.cpp
        src/toi/viewport/src/viewport_session.cpp
        ${TOI_OVERLAY_VERT_SPV_CPP}
        ${TOI_OVERLAY_FRAG_SPV_CPP}
    )
    target_include_directories(toi_viewport PUBLIC
        src/toi/viewport/include
    )
    target_link_libraries(toi_viewport PUBLIC Vulkan::Vulkan X11::X11 toi_render)
    target_compile_features(toi_viewport PUBLIC cxx_std_23)
    target_compile_definitions(toi_viewport PUBLIC VK_USE_PLATFORM_XLIB_KHR)
endif()

if(TOI_ENABLE_OVRTX)
    find_package(ovrtx CONFIG REQUIRED)
    find_package(CUDAToolkit REQUIRED)

    # ovrtx loads its RTX/USD plugins from the runtime library directory at
    # renderer-create time; expose it so tests and the app can find them.
    get_target_property(TOI_OVRTX_SHARED_LIB ovrtx::ovrtx IMPORTED_LOCATION)
    get_filename_component(TOI_OVRTX_RUNTIME_DIR "${TOI_OVRTX_SHARED_LIB}" DIRECTORY)

    add_library(toi_ovrtx
        src/toi/ovrtx/src/error.cpp
        src/toi/ovrtx/src/environment.cpp
        src/toi/ovrtx/src/handles.cpp
        src/toi/ovrtx/src/cuda_resources.cpp
        src/toi/ovrtx/src/output_mapping.cpp
        src/toi/ovrtx/src/renderer_session.cpp
    )
    target_include_directories(toi_ovrtx PUBLIC
        src/toi/ovrtx/include
    )
    target_link_libraries(toi_ovrtx PUBLIC
        toi_render
        ovrtx::ovrtx
        CUDA::cudart
    )
    target_compile_features(toi_ovrtx PUBLIC cxx_std_23)

    # Present the ovrtx CUDA frame through the native viewport: add the
    # CUDA/Vulkan interop to toi_viewport and switch on its ovrtx path.
    if(TOI_ENABLE_VIEWPORT)
        target_sources(toi_viewport PRIVATE src/toi/viewport/src/cuda_vulkan_interop.cpp)
        target_link_libraries(toi_viewport PUBLIC toi_ovrtx CUDA::cudart)
        target_compile_definitions(toi_viewport PUBLIC TOI_ENABLE_OVRTX)
    endif()
endif()

add_library(toi_native_core SHARED
    native/src/native_core.cpp
)
target_include_directories(toi_native_core PUBLIC
    native/include
)
target_link_libraries(toi_native_core PRIVATE toi_app nlohmann_json::nlohmann_json)
target_compile_features(toi_native_core PUBLIC cxx_std_23)
set_target_properties(toi_native_core PROPERTIES OUTPUT_NAME toi_native_core)

if(TOI_ENABLE_VIEWPORT)
    target_link_libraries(toi_native_core PRIVATE toi_viewport)
    target_compile_definitions(toi_native_core PRIVATE TOI_ENABLE_VIEWPORT)
endif()

if(TOI_ENABLE_OVRTX)
    # Bake the ovrtx runtime directory into the RPATH so the shell loads it (and
    # its RTX/USD plugins) without a manual LD_LIBRARY_PATH.
    set_target_properties(toi_native_core PROPERTIES BUILD_RPATH "${TOI_OVRTX_RUNTIME_DIR}")
endif()

if(TOI_BUILD_TESTS)
    find_package(Catch2 3 CONFIG QUIET)
    if(NOT Catch2_FOUND)
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.8.1
        )
        FetchContent_MakeAvailable(Catch2)
    endif()

    add_executable(toi_core_tests
        tests/core_smoke_tests.cpp
        tests/plant_tests.cpp
    )
    target_link_libraries(toi_core_tests PRIVATE
        toi_app
        toi_plant
        toi_import
        toi_native_core
        Catch2::Catch2WithMain
    )
    target_compile_definitions(toi_core_tests PRIVATE
        TOI_TEST_PROTOTYPE_ASSET_PATH="${CMAKE_SOURCE_DIR}/assets/prototypes/TOI_Module_Prototypes.obj"
    )
    include(Catch)
    catch_discover_tests(toi_core_tests)

    if(TOI_ENABLE_OVRTX)
        add_executable(toi_ovrtx_tests
            tests/ovrtx_smoke_tests.cpp
        )
        target_link_libraries(toi_ovrtx_tests PRIVATE
            toi_app
            toi_ovrtx
            Catch2::Catch2WithMain
        )
        target_compile_definitions(toi_ovrtx_tests PRIVATE
            TOI_TEST_PROTOTYPE_ASSET_PATH="${CMAKE_SOURCE_DIR}/assets/prototypes/TOI_Module_Prototypes.obj"
            TOI_TEST_ASSET_ROOT_PATH="${CMAKE_SOURCE_DIR}/assets"
        )
        set_target_properties(toi_ovrtx_tests PROPERTIES BUILD_RPATH "${TOI_OVRTX_RUNTIME_DIR}")
        # First ovrtx frame compiles shaders/assets and can take a few minutes.
        catch_discover_tests(toi_ovrtx_tests PROPERTIES TIMEOUT 600)
    endif()
endif()
