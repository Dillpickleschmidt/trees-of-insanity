cmake_minimum_required(VERSION 3.25)

project(trees_of_insanity VERSION 0.1.0 LANGUAGES CXX)

option(TOI_BUILD_TESTS "Build native core tests" ON)

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

add_library(toi_native_core SHARED
    native/src/native_core.cpp
)
target_include_directories(toi_native_core PUBLIC
    native/include
)
target_link_libraries(toi_native_core PRIVATE toi_app nlohmann_json::nlohmann_json)
target_compile_features(toi_native_core PUBLIC cxx_std_23)
set_target_properties(toi_native_core PROPERTIES OUTPUT_NAME toi_native_core)

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
    )
    target_link_libraries(toi_core_tests PRIVATE
        toi_app
        toi_native_core
        Catch2::Catch2WithMain
    )
    target_compile_definitions(toi_core_tests PRIVATE
        TOI_TEST_PROTOTYPE_ASSET_PATH="${CMAKE_SOURCE_DIR}/assets/prototypes/TOI_Module_Prototypes.obj"
    )
    include(Catch)
    catch_discover_tests(toi_core_tests)
endif()
