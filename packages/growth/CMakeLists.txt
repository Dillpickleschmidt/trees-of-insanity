cmake_minimum_required(VERSION 3.25)

project(toi_growth VERSION 0.1.0 LANGUAGES CXX)

add_library(toi_growth
    src/growth.cpp
    src/plant.cpp
)
add_library(toi::growth ALIAS toi_growth)
target_include_directories(toi_growth
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(toi_growth PUBLIC cxx_std_23)

include(GNUInstallDirs)
install(TARGETS toi_growth EXPORT toi-growth-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT toi-growth-targets
    FILE toi-growth-targets.cmake
    NAMESPACE toi::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/toi-growth
)

if(PROJECT_IS_TOP_LEVEL)
    include(FetchContent)
    include(CTest)

    find_package(Catch2 3 CONFIG QUIET)
    if(NOT Catch2_FOUND)
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.8.1
        )
        FetchContent_MakeAvailable(Catch2)
    endif()

    add_executable(toi_growth_tests tests/growth_tests.cpp)
    target_link_libraries(toi_growth_tests PRIVATE toi::growth Catch2::Catch2WithMain)

    include(Catch)
    catch_discover_tests(toi_growth_tests)
endif()
