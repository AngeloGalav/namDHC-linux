cmake_minimum_required(VERSION 3.20)
project(namdhc-linux VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

include(GNUInstallDirs)
find_package(Threads REQUIRED)

option(NAMDHC_BUILD_CHDMAN "Build the bundled chdman" ON)
option(NAMDHC_BUILD_GUI "Build the Qt6 GUI (namdhc-gui)" ON)
set(NAMDHC_GITHUB_REPO "AngeloGalav/namDHC" CACHE STRING "GitHub owner/repo linked from the About dialog")

if(NAMDHC_BUILD_CHDMAN)
	add_subdirectory(lib/mame)
endif()

# ---------------------------------------------------------------- core (Qt6::Core only)
if(NAMDHC_BUILD_GUI)
	find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets)
else()
	find_package(Qt6 6.2 REQUIRED COMPONENTS Core)
endif()
set(CMAKE_AUTOMOC ON)

configure_file(src/core/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)

add_library(namdhc_core STATIC
	src/core/options.cpp src/core/options.h
	src/core/utils.cpp src/core/utils.h
	src/core/job.cpp src/core/job.h
	src/core/jobrunner.cpp src/core/jobrunner.h
	src/core/jobqueue.cpp src/core/jobqueue.h
	src/core/chdinfo.cpp src/core/chdinfo.h
	src/core/settings.h)
target_include_directories(namdhc_core PUBLIC src/core ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(namdhc_core PUBLIC Qt6::Core)

# ---------------------------------------------------------------- CLI
add_executable(namdhc src/cli/main.cpp)
target_link_libraries(namdhc PRIVATE namdhc_core)
install(TARGETS namdhc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ---------------------------------------------------------------- GUI
if(NAMDHC_BUILD_GUI)
	add_executable(namdhc-gui
		src/gui/main.cpp
		src/gui/mainwindow.cpp src/gui/mainwindow.h
		src/gui/dialogs.cpp src/gui/dialogs.h
		resources/namdhc.qrc)
	set_target_properties(namdhc-gui PROPERTIES AUTORCC ON)
	target_link_libraries(namdhc-gui PRIVATE namdhc_core Qt6::Widgets)
	install(TARGETS namdhc-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(FILES resources/io.github.namdhc.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
	install(FILES resources/namdhc.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
