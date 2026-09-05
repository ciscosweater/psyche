cmake_minimum_required(VERSION 3.21)
project(Psyche VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 Concurrent Network)
find_package(LibArchive REQUIRED)
find_package(yaml-cpp REQUIRED)

qt_standard_project_setup(REQUIRES 6.5)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
    qt_policy(SET QTP0004 NEW)
endif()

add_library(psyche_core
    src/library.cpp
    src/package.cpp
    src/catalog.cpp
    src/settings.cpp
    src/settings.h
)
target_include_directories(psyche_core PUBLIC src)
target_compile_definitions(psyche_core PUBLIC "PSYCHE_VERSION=\"${PROJECT_VERSION}\"")
target_link_libraries(psyche_core PUBLIC
    Qt6::Core
    Qt6::Network
    LibArchive::LibArchive
    yaml-cpp::yaml-cpp
)

add_library(psyche_artwork src/artwork.cpp src/artwork.h)
target_include_directories(psyche_artwork PUBLIC src)
target_link_libraries(psyche_artwork PUBLIC Qt6::Gui Qt6::Network)

qt_add_executable(psyche
    src/main.cpp
    src/cli.cpp
    src/backend.cpp
    src/backend.h
)
target_link_libraries(psyche PRIVATE
    psyche_core
    psyche_artwork
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Concurrent
)
qt_add_qml_module(psyche
    URI Psyche
    VERSION 1.0
    QML_FILES qml/Main.qml qml/GameCover.qml
    RESOURCES qml/icons/psyche.svg qml/fonts/PixelifySans.ttf qml/fonts/OFL.txt
)

include(CTest)
if(BUILD_TESTING)
    add_executable(library_test tests/library_test.cpp)
    target_link_libraries(library_test PRIVATE psyche_core)
    add_test(NAME library_management COMMAND library_test)

    add_executable(package_test tests/package_test.cpp)
    target_link_libraries(package_test PRIVATE psyche_core)
    add_test(NAME isolated_package COMMAND package_test)

    add_executable(regression_test tests/regression_test.cpp)
    target_link_libraries(regression_test PRIVATE psyche_core)
    add_test(NAME regression_package COMMAND regression_test)

    add_executable(catalog_test tests/catalog_test.cpp)
    target_link_libraries(catalog_test PRIVATE psyche_core)
    add_test(NAME catalog_local_http COMMAND catalog_test)

    add_executable(cli_test tests/cli_test.cpp)
    target_link_libraries(cli_test PRIVATE psyche_core)
    add_test(NAME cli_headless COMMAND cli_test $<TARGET_FILE:psyche>)

    add_executable(artwork_test tests/artwork_test.cpp)
    target_link_libraries(artwork_test PRIVATE psyche_core psyche_artwork)
    add_test(NAME artwork_local_http COMMAND artwork_test)

    add_executable(settings_test tests/settings_test.cpp)
    target_link_libraries(settings_test PRIVATE psyche_core)
    add_test(NAME settings_persistence COMMAND settings_test)

    add_executable(ui_test tests/ui_test.cpp src/backend.cpp src/backend.h)
    target_link_libraries(ui_test PRIVATE
        psyche_core
        psyche_artwork
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Concurrent
    )
    add_test(NAME ui_offscreen COMMAND ui_test ${CMAKE_CURRENT_SOURCE_DIR}/qml/Main.qml)
    set_tests_properties(ui_offscreen PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
        TIMEOUT 20
    )
    set_tests_properties(
        isolated_package
        regression_package
        catalog_local_http
        cli_headless
        settings_persistence
        artwork_local_http
        PROPERTIES TIMEOUT 30
    )
endif()

install(TARGETS psyche RUNTIME DESTINATION bin)
