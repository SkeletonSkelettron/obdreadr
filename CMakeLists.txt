cmake_minimum_required(VERSION 3.21)

project(obdreadr VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Android version code; CI passes the run number so each build can update the last.
set(OBD_VERSION_CODE 1 CACHE STRING "Android versionCode")

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2)
find_package(Qt6 6.8 QUIET OPTIONAL_COMPONENTS Bluetooth)

if(ANDROID AND NOT TARGET Qt6::Bluetooth)
    message(FATAL_ERROR "Qt Bluetooth (qtconnectivity) is required for Android builds")
endif()

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(obdreadr
    src/main.cpp
    src/obd/pids.h src/obd/pids.cpp
    src/obd/elmparser.h src/obd/elmparser.cpp
    src/obd/simecu.h src/obd/simecu.cpp
    src/obd/dtc.h src/obd/dtc.cpp
    src/transport/transport.h
    src/transport/tcptransport.h src/transport/tcptransport.cpp
    src/transport/simtransport.h src/transport/simtransport.cpp
    src/elm327.h src/elm327.cpp
    src/sensorseries.h src/sensorseries.cpp
    src/sensormodel.h src/sensormodel.cpp
    src/obdsession.h src/obdsession.cpp
    src/chartbase.h src/chartbase.cpp
    src/linechart.h src/linechart.cpp
    src/overlaychart.h src/overlaychart.cpp
)

qt_add_qml_module(obdreadr
    URI ObdReadr
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/ConnectPage.qml
        qml/DashboardPage.qml
        qml/SensorCard.qml
        qml/ChartsView.qml
        qml/ValuesView.qml
        qml/OverlayView.qml
        qml/BigValuesView.qml
        qml/SensorPicker.qml
        qml/CodesPage.qml
        qml/LogPage.qml
)

target_include_directories(obdreadr PRIVATE src)

target_link_libraries(obdreadr PRIVATE
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2)

if(TARGET Qt6::Bluetooth)
    target_sources(obdreadr PRIVATE
        src/transport/btclassictransport.h src/transport/btclassictransport.cpp
        src/transport/bletransport.h src/transport/bletransport.cpp)
    target_link_libraries(obdreadr PRIVATE Qt6::Bluetooth)
    target_compile_definitions(obdreadr PRIVATE OBD_HAVE_BLUETOOTH)
else()
    message(STATUS "Qt Bluetooth not found: building with Wi-Fi and demo adapters only")
endif()

set_target_properties(obdreadr PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    QT_ANDROID_PACKAGE_NAME "io.github.skeletonskelettron.obdreadr"
    QT_ANDROID_APP_NAME "OBD Reader"
    QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
    QT_ANDROID_VERSION_CODE "${OBD_VERSION_CODE}"
)

if(NOT ANDROID) # on Android the app is a library packaged by the apk target
    include(GNUInstallDirs)
    install(TARGETS obdreadr
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# Unit tests for the protocol code (desktop only).
if(NOT ANDROID)
    find_package(Qt6 QUIET COMPONENTS Test)
    if(TARGET Qt6::Test)
        enable_testing()
        qt_add_executable(tst_obd
            tests/tst_obd.cpp
            src/obd/pids.cpp
            src/obd/elmparser.cpp
            src/obd/simecu.cpp
            src/obd/dtc.cpp)
        target_include_directories(tst_obd PRIVATE src)
        target_link_libraries(tst_obd PRIVATE Qt6::Core Qt6::Test)
        add_test(NAME tst_obd COMMAND tst_obd)
    endif()
endif()
