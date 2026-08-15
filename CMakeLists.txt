cmake_minimum_required(VERSION 3.21)
project(argus VERSION 1.4.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native")
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets)
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc video videoio imgcodecs dnn face objdetect)
find_package(Threads REQUIRED)

qt_add_executable(argus
  src/main.cpp
  src/resources.qrc
  src/core/camera.cpp
  src/core/v4l2_ctl.cpp
  src/core/detector.cpp
  src/core/object_detector.cpp
  src/core/face_recognizer.cpp
  src/core/recorder.cpp
  src/core/detection_worker.cpp
  src/core/settings.cpp
  src/ui/camera_widget.cpp
  src/ui/add_camera_dialog.cpp
  src/ui/detached_camera_window.cpp
  src/ui/theme.cpp
  src/ui/about_dialog.cpp
  src/ui/media_player_dialog.cpp
  src/ui/recordings_dialog.cpp
  src/ui/face_manager_dialog.cpp
  src/ui/main_window.cpp
)

target_link_libraries(argus PRIVATE
  Qt6::Widgets
  Qt6::Multimedia
  Qt6::MultimediaWidgets
  ${OpenCV_LIBS}
  Threads::Threads
)

target_include_directories(argus PRIVATE src)

include(GNUInstallDirs)

# Where the bundled models live when installed (./share/argus/models).
# At runtime core/paths.h falls back to appdir-relative / cwd locations if
# this path does not exist (e.g. a dev build that is not installed).
target_compile_definitions(argus PRIVATE
  ARGUS_VERSION="${PROJECT_VERSION}"
  ARGUS_ASSETS_DIR="${CMAKE_INSTALL_FULL_DATADIR}/argus/models"
)

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------

install(TARGETS argus RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY assets/models
        DESTINATION ${CMAKE_INSTALL_DATADIR}/argus
        FILES_MATCHING PATTERN "*.weights" PATTERN "*.cfg" PATTERN "*.names" PATTERN "*.xml")

install(FILES src/argus_icon.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
        RENAME argus.png)

install(FILES packaging/argus.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

# ---------------------------------------------------------------------------
# Packaging (.deb)
# ---------------------------------------------------------------------------

set(CPACK_PACKAGE_NAME argus)
set(CPACK_PACKAGE_VENDOR "Argus")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Argus Tactical Vision Suite")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/packaging/description.txt")
set(CPACK_PACKAGE_CONTACT "Argus maintainer <argus@localhost>")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Argus maintainer <argus@localhost>")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_GENERATOR DEB)
include(CPack)
