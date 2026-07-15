cmake_minimum_required(VERSION 3.25)

project(trees_of_insanity VERSION 0.1.0 LANGUAGES CXX)

option(TOI_BUILD_TESTS "Build C++ tests" ON)
option(TOI_BUILD_DESKTOP "Build the Qt desktop application" OFF)
option(TOI_ENABLE_OVRTX "Build the ovrtx CUDA/Vulkan preview renderer" OFF)

if(TOI_BUILD_DESKTOP AND NOT TOI_ENABLE_OVRTX)
    message(FATAL_ERROR "The desktop requires TOI_ENABLE_OVRTX; no alternate viewport architecture is supported")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)
include(CTest)

find_package(nlohmann_json CONFIG REQUIRED)

add_subdirectory(packages/growth)

add_library(toi_import
    desktop/src/model/import/src/obj_importer.cpp
)
target_include_directories(toi_import PUBLIC
    desktop/src/model/import/include
)
target_link_libraries(toi_import PUBLIC toi_growth)
target_compile_features(toi_import PUBLIC cxx_std_23)

add_library(toi_render
    desktop/src/graphics/render/src/orbit_view.cpp
    desktop/src/graphics/render/src/render_projection.cpp
    desktop/src/graphics/render/src/viewport_guides.cpp
)
target_include_directories(toi_render PUBLIC
    desktop/src/graphics/render/include
)
target_link_libraries(toi_render PUBLIC toi_growth)
target_compile_features(toi_render PUBLIC cxx_std_23)

add_library(toi_project
    desktop/src/model/project/src/project.cpp
)
target_include_directories(toi_project PUBLIC
    desktop/src/model/project/include
)
target_link_libraries(toi_project PUBLIC toi_growth nlohmann_json::nlohmann_json)
target_compile_features(toi_project PUBLIC cxx_std_23)

add_library(toi_desktop_model
    desktop/src/model/session/src/desktop_session.cpp
)
target_include_directories(toi_desktop_model PUBLIC
    desktop/src/model/session/include
)
target_link_libraries(toi_desktop_model PUBLIC
    toi_growth
    toi_import
    toi_project
    nlohmann_json::nlohmann_json
)
target_compile_features(toi_desktop_model PUBLIC cxx_std_23)
target_compile_definitions(toi_desktop_model PUBLIC
    TOI_DEFAULT_ASSET_ROOT_PATH="${CMAKE_SOURCE_DIR}/desktop/assets"
    TOI_DEFAULT_PROTOTYPE_ASSET_PATH="${CMAKE_SOURCE_DIR}/desktop/assets/prototypes/TOI_Module_Prototypes.obj"
)

if(TOI_ENABLE_OVRTX)
    find_package(ovrtx CONFIG REQUIRED)
    find_package(CUDAToolkit REQUIRED)

    # ovrtx loads its RTX/USD plugins from the runtime library directory at
    # renderer-create time; expose it so tests and the app can find them.
    get_target_property(TOI_OVRTX_SHARED_LIB ovrtx::ovrtx IMPORTED_LOCATION)
    get_filename_component(TOI_OVRTX_RUNTIME_DIR "${TOI_OVRTX_SHARED_LIB}" DIRECTORY)

    add_library(toi_ovrtx
        desktop/src/graphics/ovrtx/src/error.cpp
        desktop/src/graphics/ovrtx/src/environment.cpp
        desktop/src/graphics/ovrtx/src/handles.cpp
        desktop/src/graphics/ovrtx/src/cuda_resources.cpp
        desktop/src/graphics/ovrtx/src/output_mapping.cpp
        desktop/src/graphics/ovrtx/src/renderer_session.cpp
    )
    target_include_directories(toi_ovrtx PUBLIC
        desktop/src/graphics/ovrtx/include
    )
    target_link_libraries(toi_ovrtx PUBLIC
        toi_render
        ovrtx::ovrtx
        CUDA::cudart
    )
    target_compile_features(toi_ovrtx PUBLIC cxx_std_23)

    find_package(Vulkan REQUIRED)
    find_program(GLSLC_EXECUTABLE glslc REQUIRED)
    set(TOI_SHADER_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/toi/viewport/shaders)
    file(MAKE_DIRECTORY ${TOI_SHADER_GEN_DIR})
    function(toi_compile_shader out_var source symbol)
        get_filename_component(name ${source} NAME)
        set(spv ${TOI_SHADER_GEN_DIR}/${name}.spv)
        set(cpp ${TOI_SHADER_GEN_DIR}/${name}.spv.cpp)
        add_custom_command(
            OUTPUT ${cpp}
            COMMAND ${GLSLC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/${source} -o ${spv}
            COMMAND ${CMAKE_COMMAND} -DINPUT=${spv} -DOUTPUT=${cpp} -DSYMBOL=${symbol}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_binary.cmake
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${source} cmake/embed_binary.cmake
            VERBATIM
        )
        set_source_files_properties(${cpp} PROPERTIES GENERATED TRUE)
        set(${out_var} ${cpp} PARENT_SCOPE)
    endfunction()
    toi_compile_shader(TOI_OVERLAY_VERT_SPV_CPP desktop/src/graphics/viewport/shaders/overlay_lines.vert overlay_lines_vert_spv)
    toi_compile_shader(TOI_OVERLAY_FRAG_SPV_CPP desktop/src/graphics/viewport/shaders/overlay_lines.frag overlay_lines_frag_spv)
    toi_compile_shader(TOI_OVERLAY_SPHERES_VERT_SPV_CPP desktop/src/graphics/viewport/shaders/overlay_spheres.vert overlay_spheres_vert_spv)
    toi_compile_shader(TOI_OVERLAY_SPHERES_FRAG_SPV_CPP desktop/src/graphics/viewport/shaders/overlay_spheres.frag overlay_spheres_frag_spv)

    add_library(toi_desktop_graphics
        desktop/src/graphics/viewport/src/error.cpp
        desktop/src/graphics/viewport/src/vulkan_context.cpp
        desktop/src/graphics/viewport/src/cuda_vulkan_interop.cpp
        desktop/src/graphics/viewport/src/viewport_overlay.cpp
        desktop/src/graphics/viewport/src/preview_renderer.cpp
        ${TOI_OVERLAY_VERT_SPV_CPP}
        ${TOI_OVERLAY_FRAG_SPV_CPP}
        ${TOI_OVERLAY_SPHERES_VERT_SPV_CPP}
        ${TOI_OVERLAY_SPHERES_FRAG_SPV_CPP}
    )
    target_include_directories(toi_desktop_graphics PUBLIC desktop/src/graphics/viewport/include)
    target_link_libraries(toi_desktop_graphics PUBLIC Vulkan::Vulkan toi_render toi_ovrtx CUDA::cudart)
    target_compile_features(toi_desktop_graphics PUBLIC cxx_std_23)
    if(WIN32)
        target_compile_definitions(toi_desktop_graphics PUBLIC VK_USE_PLATFORM_WIN32_KHR)
    endif()
endif()

if(TOI_BUILD_DESKTOP)
    find_package(CUDAToolkit REQUIRED)
    find_package(Vulkan REQUIRED)
    find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Core Gui GuiPrivate Qml Quick WebChannel WebEngineQuick)
    qt_standard_project_setup()

    qt_add_executable(trees_of_insanity
        desktop/src/shell/main.cpp
        desktop/src/shell/desktop_bridge.cpp
        desktop/src/shell/desktop_bridge.hpp
        desktop/src/shell/desktop_actions.cpp
        desktop/src/shell/desktop_actions.hpp
        desktop/src/shell/vulkan_device.cpp
        desktop/src/shell/vulkan_device.hpp
        desktop/src/shell/viewport_texture_item.cpp
        desktop/src/shell/viewport_texture_item.hpp
    )
    qt_add_resources(trees_of_insanity "shell_resources"
        PREFIX "/shell"
        BASE "desktop/src/shell"
        FILES desktop/src/shell/main.qml
    )
    target_link_libraries(trees_of_insanity PRIVATE
        toi_desktop_model
        nlohmann_json::nlohmann_json
        CUDA::cudart
        Vulkan::Vulkan
        Qt6::Core
        Qt6::Gui
        Qt6::GuiPrivate
        Qt6::Qml
        Qt6::Quick
        Qt6::WebChannel
        Qt6::WebEngineQuick
    )
    if(TOI_ENABLE_OVRTX)
        target_link_libraries(trees_of_insanity PRIVATE toi_desktop_graphics)
        target_compile_definitions(trees_of_insanity PRIVATE TOI_ENABLE_OVRTX)
        set_target_properties(trees_of_insanity PROPERTIES BUILD_RPATH "${TOI_OVRTX_RUNTIME_DIR};/opt/cuda/targets/x86_64-linux/lib")
    endif()
    target_compile_definitions(trees_of_insanity PRIVATE
        TOI_UI_SOURCE_INDEX_PATH="${CMAKE_SOURCE_DIR}/desktop/dist/index.html"
        TOI_QT_WEBENGINE_PROCESS_PATH="${CMAKE_SOURCE_DIR}/.cache/qt-root/usr/lib/qt6/QtWebEngineProcess"
        TOI_QT_WEBENGINE_RESOURCES_PATH="${CMAKE_SOURCE_DIR}/.cache/qt-root/usr/share/qt6/resources"
        TOI_QT_WEBENGINE_LOCALES_PATH="${CMAKE_SOURCE_DIR}/.cache/qt-root/usr/share/qt6/translations/qtwebengine_locales"
        TOI_QML_IMPORT_PATH="${CMAKE_SOURCE_DIR}/.cache/qt-root/usr/lib/qt6/qml"
    )

    install(TARGETS trees_of_insanity RUNTIME DESTINATION bin)
    install(DIRECTORY desktop/dist/ DESTINATION share/trees-of-insanity/ui)
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
        desktop/tests/model/model_smoke_tests.cpp
    )
    target_link_libraries(toi_core_tests PRIVATE
        toi_desktop_model
        toi_import
        toi_render
        Catch2::Catch2WithMain
    )
    target_compile_definitions(toi_core_tests PRIVATE
        TOI_TEST_PROTOTYPE_ASSET_PATH="${CMAKE_SOURCE_DIR}/desktop/assets/prototypes/TOI_Module_Prototypes.obj"
    )
    include(Catch)
    catch_discover_tests(toi_core_tests)

    if(TOI_ENABLE_OVRTX)
        add_executable(toi_ovrtx_tests
            desktop/tests/graphics/ovrtx_smoke_tests.cpp
        )
        target_link_libraries(toi_ovrtx_tests PRIVATE
            toi_desktop_model
            toi_render
            toi_ovrtx
            Catch2::Catch2WithMain
        )
        target_compile_definitions(toi_ovrtx_tests PRIVATE
            TOI_TEST_PROTOTYPE_ASSET_PATH="${CMAKE_SOURCE_DIR}/desktop/assets/prototypes/TOI_Module_Prototypes.obj"
            TOI_TEST_ASSET_ROOT_PATH="${CMAKE_SOURCE_DIR}/desktop/assets"
        )
        set_target_properties(toi_ovrtx_tests PROPERTIES BUILD_RPATH "${TOI_OVRTX_RUNTIME_DIR}")
        # First ovrtx frame compiles shaders/assets and can take a few minutes.
        catch_discover_tests(toi_ovrtx_tests PROPERTIES TIMEOUT 600)
    endif()
endif()
