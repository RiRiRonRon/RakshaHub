cmake_minimum_required(VERSION 3.16)
project(Raksha_Hub VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt modules

find_package(Qt6 REQUIRED COMPONENTS Quick Qml QuickDialogs2 Network Sql OpenGL Widgets)
qt_standard_project_setup()

qt_add_executable(appRaksha_Hub
    src/main.cpp
    src/app.rc

)

qt_add_qml_module(appRaksha_Hub
    URI Raksha_Hub
    VERSION 1.0
    QML_FILES
        src/qml/Main.qml
        src/qml/ShowCard.qml
        src/qml/ShowPage.qml
        src/qml/MenuButton.qml
        src/qml/SideMenuItem.qml
        src/qml/AddButton.qml
        src/qml/AddMenu.qml
        src/qml/Toast.qml
        src/qml/PlayerWindow.qml
        src/qml/BookCard.qml
        src/qml/BookReaderPage.qml
    SOURCES
        src/library_manager.h
        src/library_manager.cpp
        src/thumbnail_generator.h
        src/thumbnail_generator.cpp

        src/mpv_object.h
        src/mpv_object.cpp
        src/mpv_renderer.h
        src/mpv_renderer.cpp
        src/webview_item.h
        src/webview_item.cpp
        src/book_manager.h
        src/book_manager.cpp
)



target_include_directories(appRaksha_Hub PRIVATE
    src
    ${CMAKE_SOURCE_DIR}/libs/mpv/include
    ${CMAKE_SOURCE_DIR}/libs/webview2/include
)

target_link_libraries(appRaksha_Hub
    PRIVATE
        Qt6::Quick
        Qt6::Qml
        Qt6::QuickDialogs2
        Qt6::Network
        Qt6::Sql
        Qt6::OpenGL
        Qt6::Widgets

        ${CMAKE_SOURCE_DIR}/libs/mpv/libmpv.dll.a
        ${CMAKE_SOURCE_DIR}/libs/webview2/x64/WebView2Loader.dll.lib
)

set_target_properties(appRaksha_Hub PROPERTIES
    WIN32_EXECUTABLE TRUE
)

# libmpv-2.dll
add_custom_command(TARGET appRaksha_Hub POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/libs/mpv/libmpv-2.dll"
        "$<TARGET_FILE_DIR:appRaksha_Hub>/libmpv-2.dll"
    COMMENT "Copying libmpv-2.dll to output directory"
)


# WebView2Loader.dll
add_custom_command(TARGET appRaksha_Hub POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/libs/webview2/x64/WebView2Loader.dll"
        "$<TARGET_FILE_DIR:appRaksha_Hub>/WebView2Loader.dll"
    COMMENT "Copying WebView2Loader.dll"
)

# Copy pdfjs folder next to the exe
add_custom_command(TARGET appRaksha_Hub POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/pdfjs"
        "$<TARGET_FILE_DIR:appRaksha_Hub>/pdfjs"
    COMMENT "Copying PDF.js"
)

# Copy epubjs folder next to the exe
# Place epub.min.js at <project root>/epubjs/epub.min.js

add_custom_command(TARGET appRaksha_Hub POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/epubjs"
        "$<TARGET_FILE_DIR:appRaksha_Hub>/epubjs"
    COMMENT "Copying epub.js"
)
# Copy fonts folder next to the exe
add_custom_command(TARGET appRaksha_Hub POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/fonts"
    "$<TARGET_FILE_DIR:appRaksha_Hub>/fonts"
    COMMENT "Copying fonts folder"
)