cmake_minimum_required(VERSION 3.16)

project(FiveLinkFaultSetting VERSION 0.1 LANGUAGES CXX)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/output)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)

find_package(Qt6 REQUIRED COMPONENTS Quick Xml SerialPort Network)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(appFiveLinkFaultSetting
    main.cpp

    appconfig.h
    appconfig.cpp

    backend.h
    backend.cpp

    serialmanager.h
    serialmanager.cpp

    wifimanager.h
    wifimanager.cpp

    transporthub.h
    transporthub.cpp
)

qt_add_qml_module(appFiveLinkFaultSetting
    URI FiveLinkFaultSetting
    VERSION 1.0
    QML_FILES
        Main.qml
        FaultPage.qml
        LogPage.qml
        SwitchTile.qml
        ResultPage.qml
)

qt_add_resources(appFiveLinkFaultSetting "controls_conf"
    PREFIX "/"
    FILES qtquickcontrols2.conf
)

qt_add_resources(appFiveLinkFaultSetting "app_resources"
    PREFIX "/"
    FILES
        config/AppConfig.xml
        images/logo.ico
)

if (WIN32 AND NOT ANDROID)
    target_sources(appFiveLinkFaultSetting PRIVATE ${CMAKE_SOURCE_DIR}/logo.rc)
endif()

set_target_properties(appFiveLinkFaultSetting PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)
target_link_libraries(appFiveLinkFaultSetting
    PRIVATE
        Qt6::Quick
        Qt6::Xml
        Qt6::SerialPort
        Qt6::Network
)

if(WIN32)
    add_custom_command(TARGET appFiveLinkFaultSetting POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/config
            $<TARGET_FILE_DIR:appFiveLinkFaultSetting>/config
    )
endif()

include(GNUInstallDirs)

install(TARGETS appFiveLinkFaultSetting
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Android 保持不动
set_target_properties(appFiveLinkFaultSetting PROPERTIES
    QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/android"
    QT_ANDROID_APP_NAME "五联故障配置"
    ANDROID_VERSION_CODE 10001
    ANDROID_VERSION_NAME "1.0.1"
)
