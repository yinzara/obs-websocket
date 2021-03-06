cmake_minimum_required(VERSION 3.2)
project(obs-websocket)

set(CMAKE_PREFIX_PATH "${QTDIR}")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

if (NOT DEFINED OBS_OUTPUT_DIR)
	include(external/FindLibObs.cmake)
	SET(WAMP_INSTALL_PATH "${CMAKE_BINARY_DIR}/$<CONFIG>")
	SET(CORE_INSTALL_PATH "${CMAKE_BINARY_DIR}/$<CONFIG>")
	SET(WEBSOCKETS_INSTALL_PATH "${CMAKE_BINARY_DIR}/$<CONFIG>")
else()
	SET(WAMP_INSTALL_PATH "${OBS_LIBRARY_DESTINATION}")
	SET(CORE_INSTALL_PATH "${OBS_LIBRARY_DESTINATION}")
	SET(WEBSOCKETS_INSTALL_PATH "${OBS_LIBRARY_DESTINATION}")
endif()

if(DEFINED QTDIR${_lib_suffix})
	list(APPEND CMAKE_PREFIX_PATH "${QTDIR${_lib_suffix}}")
elseif(DEFINED QTDIR)
	list(APPEND CMAKE_PREFIX_PATH "${QTDIR}")
elseif(DEFINED ENV{QTDIR${_lib_suffix}})
	list(APPEND CMAKE_PREFIX_PATH "$ENV{QTDIR${_lib_suffix}}")
elseif(DEFINED ENV{QTDIR})
	list(APPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
endif()

find_package(LibObs REQUIRED)
find_package(Qt5Core REQUIRED)
find_package(Qt5WebSockets REQUIRED)
find_package(Qt5Widgets REQUIRED)


add_subdirectory(deps/mbedtls EXCLUDE_FROM_ALL)
add_subdirectory(deps/wamp)
set(ENABLE_PROGRAMS false)

set(obs-websocket_SOURCES
	src/obs-websocket.cpp
	src/WSServer.cpp
	src/WSRequestHandler.cpp
	src/WSRequestHandler_General.cpp
	src/WSRequestHandler_Profiles.cpp
	src/WSRequestHandler_Recording.cpp
	src/WSRequestHandler_ReplayBuffer.cpp
	src/WSRequestHandler_SceneCollections.cpp
	src/WSRequestHandler_Scenes.cpp
	src/WSRequestHandler_SceneItems.cpp
	src/WSRequestHandler_Sources.cpp
	src/WSRequestHandler_Streaming.cpp
	src/WSRequestHandler_StudioMode.cpp
	src/WSRequestHandler_Transitions.cpp
	src/WSRequestHandler_WebSockets.cpp
    src/WSRequestHandler_Wamp.cpp
	src/WSEvents.cpp
	src/Config.cpp
	src/Utils.cpp
	src/forms/settings-dialog.cpp)

set(obs-websocket_HEADERS
	src/obs-websocket.h
	src/WSServer.h
	src/WSRequestHandler.h
	src/WSEvents.h
	src/Config.h
	src/Utils.h
	src/forms/settings-dialog.h)

# --- Platform-independent build settings ---
add_library(obs-websocket MODULE
	${obs-websocket_SOURCES}
	${obs-websocket_HEADERS})

add_dependencies(obs-websocket mbedcrypto
	wamp core websockets)

include_directories(
	"${LIBOBS_INCLUDE_DIR}/../UI/obs-frontend-api"
	"${CMAKE_SOURCE_DIR}/UI/obs-frontend-api"
	${Qt5Core_INCLUDES}
	${Qt5WebSockets_INCLUDES}
	${Qt5Widgets_INCLUDES}
	${mbedcrypto_INCLUDES}
	"${CMAKE_SOURCE_DIR}/deps/mbedtls/include"
	"${CMAKE_SOURCE_DIR}/plugins/obs-websocket/deps/mbedtls/include"
	"${CMAKE_SOURCE_DIR}/deps/wamp/src"
	"${CMAKE_SOURCE_DIR}/plugins/obs-websocket/deps/wamp/src"
	"${CMAKE_SOURCE_DIR}/deps/wamp/src/client"
	"${CMAKE_SOURCE_DIR}/plugins/obs-websocket/deps/wamp/src/client"
	"${CMAKE_SOURCE_DIR}/deps/wamp/src/router"
	"${CMAKE_SOURCE_DIR}/plugins/obs-websocket/deps/wamp/src/router")

target_link_libraries(obs-websocket
	libobs
	Qt5::Core
	Qt5::WebSockets
	Qt5::Widgets
	mbedcrypto
	wamp
	core
	websockets)

# --- End of section ---

# --- Windows-specific build settings and tasks ---

if(WIN32)

	IF (DEFINED OBS_OUTPUT_DIR)
		target_link_libraries(obs-websocket obs-frontend-api)
	else()
		if(NOT DEFINED OBS_FRONTEND_LIB)
			set(OBS_FRONTEND_LIB "OBS_FRONTEND_LIB-NOTFOUND" CACHE FILEPATH "OBS frontend library")
			message(FATAL_ERROR "Could not find OBS Frontend API\'s library !")
		endif()


		include_directories(
			"${LIBOBS_INCLUDE_DIR}/../${OBS_BUILDDIR_ARCH}/UI"
		)

		target_link_libraries(obs-websocket
			"${OBS_FRONTEND_LIB}")


		if(CMAKE_SIZEOF_VOID_P EQUAL 8)
			set(ARCH_NAME "64bit")
			set(OBS_BUILDDIR_ARCH "build64")
		else()
			set(ARCH_NAME "32bit")
			set(OBS_BUILDDIR_ARCH "build32")
		endif()

		add_custom_command(TARGET obs-websocket POST_BUILD
			COMMAND if $<CONFIG:Release>==1 ("${CMAKE_COMMAND}" -E copy
				"${QTDIR}/bin/Qt5WebSockets.dll"
				"${QTDIR}/bin/Qt5Network.dll"
				"${CMAKE_BINARY_DIR}/$<CONFIG>")

			COMMAND if $<CONFIG:Debug>==1 ("${CMAKE_COMMAND}" -E copy
				"${QTDIR}/bin/Qt5WebSocketsd.dll"
				"${QTDIR}/bin/Qt5Networkd.dll"
				"${QTDIR}/bin/Qt5Qmld.dll"
				"${CMAKE_BINARY_DIR}/$<CONFIG>")
		)

		# --- Release package helper ---
		# The "release" folder has a structure similar OBS' one on Windows
		set(RELEASE_DIR "${PROJECT_SOURCE_DIR}/release")

		add_custom_command(TARGET obs-websocket POST_BUILD
			COMMAND if $<CONFIG:Release>==1 (
				"${CMAKE_COMMAND}" -E make_directory
				"${RELEASE_DIR}/data/obs-plugins/obs-websocket"
				"${RELEASE_DIR}/obs-plugins/${ARCH_NAME}")

			COMMAND if $<CONFIG:Release>==1 ("${CMAKE_COMMAND}" -E copy_directory
				"${PROJECT_SOURCE_DIR}/data"
				"${RELEASE_DIR}/data/obs-plugins/obs-websocket")

			COMMAND if $<CONFIG:Release>==1 ("${CMAKE_COMMAND}" -E copy
				"$<TARGET_FILE:obs-websocket>"
				"${QTDIR}/bin/Qt5WebSockets.dll"
				"${QTDIR}/bin/Qt5Network.dll"
				"${QTDIR}/bin/Qt5Qml.dll"
				"${RELEASE_DIR}/obs-plugins/${ARCH_NAME}")

			# Copy to obs-studio dev environment for immediate testing
			COMMAND if $<CONFIG:Debug>==1 (
				"${CMAKE_COMMAND}" -E copy
					"$<TARGET_FILE:obs-websocket>"
					"${QTDIR}/bin/Qt5WebSocketsd.dll"
					"${QTDIR}/bin/Qt5Networkd.dll"
					"${QTDIR}/bin/Qt5Qmld.dll"
					"${LIBOBS_INCLUDE_DIR}/../${OBS_BUILDDIR_ARCH}/rundir/$<CONFIG>/obs-plugins/${ARCH_NAME}")

			COMMAND if $<CONFIG:Debug>==1 (
				"${CMAKE_COMMAND}" -E make_directory
					"${LIBOBS_INCLUDE_DIR}/../${OBS_BUILDDIR_ARCH}/rundir/$<CONFIG>/data/obs-plugins/obs-websocket")

			COMMAND if $<CONFIG:Debug>==1 (
				"${CMAKE_COMMAND}" -E copy_directory
					"${PROJECT_SOURCE_DIR}/data"
					"${LIBOBS_INCLUDE_DIR}/../${OBS_BUILDDIR_ARCH}/rundir/$<CONFIG>/data/obs-plugins/obs-websocket")
		)
		# --- End of sub-section ---

    endif()

endif()
# --- End of section ---

# --- Linux-specific build settings and tasks ---
if(UNIX AND NOT APPLE)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

	target_compile_options(mbedcrypto PRIVATE -fPIC)
	set_target_properties(obs-websocket PROPERTIES PREFIX "")
	target_link_libraries(obs-websocket
		obs-frontend-api)

	file(GLOB locale_files data/locale/*.ini)

	install(TARGETS obs-websocket
		LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/obs-plugins")
	install(FILES ${locale_files}
		DESTINATION "${CMAKE_INSTALL_PREFIX}/share/obs/obs-plugins/obs-websocket/locale")
endif()
# --- End of section ---

# -- OS X specific build settings and tasks --
if(APPLE)

	set_target_properties(obs-websocket PROPERTIES PREFIX "")
	IF (DEFINED OBS_OUTPUT_DIR)
		target_link_libraries(obs-websocket obs-frontend-api)
	else()
		target_link_libraries(obs-websocket "${OBS_FRONTEND_LIB}")
	endif()
endif()

IF (DEFINED OBS_OUTPUT_DIR)
	install_obs_plugin_with_data(obs-websocket data)
endif()
# -- End of section --
