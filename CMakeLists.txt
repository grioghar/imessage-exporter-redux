cmake_minimum_required(VERSION 3.16)
project(imessage_exporter LANGUAGES CXX VERSION 0.6.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# So the static core/db libraries link cleanly into the (PIE) Qt GUI executable.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Datetime sub-version (YYYYMMDDHHMM, 24h) appended to the displayed version as
# IMSG_VERSION.<stamp>. Resolved in priority order so a release built from a
# tagged commit reads EXACTLY the tag:
#   1. an explicit -DIMSG_BUILD_STAMP=... ;
#   2. the trailing datetime of the current tag (vX.Y.Z.<YYYYMMDDHHMM>), taken
#      from $GITHUB_REF_NAME in CI or `git describe` locally;
#   3. otherwise the current build time.
if(NOT DEFINED IMSG_BUILD_STAMP OR IMSG_BUILD_STAMP STREQUAL "")
  set(_imsg_ref "$ENV{GITHUB_REF_NAME}")
  if(NOT _imsg_ref)
    execute_process(COMMAND git describe --tags --exact-match
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE _imsg_ref OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
  endif()
  if(_imsg_ref MATCHES "\\.([0-9]+)$")  # trailing ".<digits>" of the tag
    string(LENGTH "${CMAKE_MATCH_1}" _imsg_len)
    if(_imsg_len GREATER_EQUAL 8)  # looks like a YYYYMMDDHHMM datetime
      set(IMSG_BUILD_STAMP "${CMAKE_MATCH_1}")
    endif()
  endif()
endif()
if(NOT DEFINED IMSG_BUILD_STAMP OR IMSG_BUILD_STAMP STREQUAL "")
  string(TIMESTAMP IMSG_BUILD_STAMP "%Y%m%d%H%M")  # fallback: build time
endif()
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_stamp.hpp.in
               ${CMAKE_BINARY_DIR}/generated/imsg/build_stamp.hpp @ONLY)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
  add_compile_options(/W3)
else()
  add_compile_options(-Wall -Wextra)
endif()

# --- Core library: zero external dependencies, fully unit-testable ----------
add_library(imsg_core STATIC
  src/log.cpp
  src/models.cpp
  src/time_util.cpp
  src/attributed_body.cpp
  src/exporters.cpp
  src/theme.cpp
  src/contact_book.cpp
  src/vcard.cpp
)
target_include_directories(imsg_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# --- Tests (no SQLite needed) ----------------------------------------------
enable_testing()
add_executable(imsg_tests tests/test_core.cpp)
target_link_libraries(imsg_tests PRIVATE imsg_core)
add_test(NAME core COMMAND imsg_tests)

# --- SQLite-backed pieces: need SQLite3 (ships with macOS/iOS; libsqlite3-dev
#     on Linux) ----------------------------------------------------------------
find_package(SQLite3)
if(SQLite3_FOUND)
  # Reader + streaming export engine, shared by the CLI and the embedding bridge.
  add_library(imsg_db STATIC
    src/database.cpp src/export_job.cpp src/contacts.cpp src/backup.cpp
    src/contact_store.cpp)
  target_link_libraries(imsg_db PUBLIC imsg_core SQLite::SQLite3)

  # CLI binary.
  add_executable(imessage-exporter src/main.cpp)
  target_link_libraries(imessage-exporter PRIVATE imsg_db)
  target_include_directories(imessage-exporter PRIVATE ${CMAKE_BINARY_DIR}/generated)
  install(TARGETS imessage-exporter RUNTIME DESTINATION bin)
  install(FILES man/imessage-exporter.1 DESTINATION share/man/man1)

  # --- Desktop GUI: needs Qt6 Widgets (one C++ app for macOS/Windows/Linux) ---
  find_package(Qt6 COMPONENTS Widgets Concurrent Network Svg QUIET)
  if(Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(GUI_SOURCES gui/main.cpp gui/main_window.cpp gui/icloud_contacts.cpp
        gui/updater.cpp gui/secret_store.cpp gui/google_contacts.cpp
        gui/google_auth.cpp gui/google_drive.cpp gui/link_preview.cpp
        gui/resources.qrc)
    # Embed the app icon in the Windows .exe when the .ico has been generated
    # (the package workflow renders assets/icon.ico from the SVG before building).
    if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
      list(APPEND GUI_SOURCES assets/app_icon.rc)
    endif()
    add_executable(imessage-exporter-gui ${GUI_SOURCES})
    target_link_libraries(imessage-exporter-gui
      PRIVATE imsg_db Qt6::Widgets Qt6::Concurrent Qt6::Network Qt6::Svg)
    target_include_directories(imessage-exporter-gui
      PRIVATE ${CMAKE_BINARY_DIR}/generated)
    if(APPLE)
      # Produces "iMessage Exporter.app" for double-click launch.
      set_target_properties(imessage-exporter-gui PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "iMessage Exporter")
    endif()
    if(WIN32)
      # Windowed subsystem: no console window pops up behind the GUI.
      set_target_properties(imessage-exporter-gui PROPERTIES WIN32_EXECUTABLE TRUE)
      # Credential Manager (secret_store) lives in advapi32.
      target_link_libraries(imessage-exporter-gui PRIVATE advapi32)
    endif()
    install(TARGETS imessage-exporter-gui
      RUNTIME DESTINATION bin BUNDLE DESTINATION .)
    if(UNIX AND NOT APPLE)
      # Desktop-environment integration for .deb / .rpm / Snap / AppImage.
      install(FILES packaging/linux/imessage-exporter.desktop
        DESTINATION share/applications)
      install(FILES assets/icon.svg
        DESTINATION share/icons/hicolor/scalable/apps
        RENAME imessage-exporter.svg)
    endif()
  else()
    message(STATUS
      "Qt6 not found: skipping the desktop GUI. Install Qt 6 (e.g. "
      "'qt6-base-dev' on Linux) to build 'imessage-exporter-gui'.")
  endif()

  # Embeddable static library exposing the C ABI (imsg_bridge.h). This is what an
  # iOS app links against — see docs/IOS.md. Build for iOS with an iOS CMake
  # toolchain, or consume the sources via the SwiftPM package (Package.swift).
  add_library(imsg_mobile STATIC src/imsg_bridge.cpp)
  target_link_libraries(imsg_mobile PUBLIC imsg_db)
  install(TARGETS imsg_mobile ARCHIVE DESTINATION lib)
  install(DIRECTORY include/ DESTINATION include)
else()
  message(WARNING
    "SQLite3 not found: building the core library and tests only. "
    "Install libsqlite3-dev (Linux) to build the 'imessage-exporter' binary. "
    "On macOS SQLite3 ships with the system and is found automatically.")
endif()

# --- CPack: Linux distro packages (.deb / .rpm) ------------------------------
# Built in CI against the distro's own Qt so dependencies resolve via the system
# package manager. Run from the build dir: `cpack -G DEB` or `cpack -G RPM`.
set(CPACK_PACKAGE_NAME "imessage-exporter")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}.${IMSG_BUILD_STAMP}")
set(CPACK_PACKAGE_CONTACT "imessage-exporter maintainers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
  "Export macOS iMessage/SMS history to TXT, JSON, or HTML")
set(CPACK_PACKAGE_HOMEPAGE_URL
  "https://github.com/grioghar/imessage-exporter-redux")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
# DEB: let dpkg-shlibdeps derive the Qt/SQLite runtime dependencies.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
# RPM: dependency discovery is automatic.
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Productivity")
set(CPACK_RPM_PACKAGE_AUTOREQ "yes")
include(CPack)
