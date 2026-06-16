cmake_minimum_required(VERSION 3.10)

project(nexus_sync_hmi VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 COMPONENTS Core Quick Qml Widgets REQUIRED)

add_executable(nexus-sync-hmi
    src/data_provider.h
    src/exciter_interface.h
    src/telemetry_model.h
    src/main.cpp
    src/telemetry_model.cpp
    src/demo_data_provider.h
    src/demo_data_provider.cpp
    src/axi_lite_provider.h
    src/axi_lite_provider.cpp
    src/backend_facade.h
    src/backend_facade.cpp
    qml/qml.qrc
)

target_include_directories(nexus-sync-hmi PRIVATE src)
target_link_libraries(nexus-sync-hmi PRIVATE Qt5::Core Qt5::Quick Qt5::Qml)

add_executable(nexus-sync-hmi-lite
    src/data_provider.h
    src/exciter_interface.h
    src/telemetry_model.h
    src/embedded_lite_main.cpp
    src/telemetry_model.cpp
    src/demo_data_provider.h
    src/demo_data_provider.cpp
    src/axi_lite_provider.h
    src/axi_lite_provider.cpp
    src/backend_facade.h
    src/backend_facade.cpp
)

target_include_directories(nexus-sync-hmi-lite PRIVATE src)
target_link_libraries(nexus-sync-hmi-lite PRIVATE Qt5::Core Qt5::Widgets)

install(TARGETS nexus-sync-hmi RUNTIME DESTINATION bin)
install(TARGETS nexus-sync-hmi-lite RUNTIME DESTINATION bin)
