cmake_minimum_required(VERSION 3.2)
project(obs-websocket)

set(CMAKE_PREFIX_PATH "${QTDIR}")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

if (NOT DEFINED OBS_OUTPUT_DIR) 
	include(external/FindLibObs.cmake)
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
set(ENABLE_PROGRAMS false)

set(obs-websocket_SOURCES 
	obs-websocket.cpp
	WSServer.cpp
	WSRequestHandler.cpp
	WSEvents.cpp
	Config.cpp
	Utils.cpp
	forms/settings-dialog.cpp)

set(obs-websocket_HEADERS
	obs-websocket.h
	WSServer.h
	WSRequestHandler.h
	WSEvents.h
	Config.h
	Utils.h
	forms/settings-dialog.h)

# --- Platform-independent build settings ---
add_library(obs-websocket MODULE 
	${obs-websocket_SOURCES}
	${obs-websocket_HEADERS})

add_dependencies(obs-websocket mbedcrypto)

include_directories( 
	"${LIBOBS_INCLUDE_DIR}/../UI/obs-frontend-api"
	"${CMAKE_SOURCE_DIR}/UI/obs-frontend-api"
	${Qt5Core_INCLUDES}
	${Qt5WebSockets_INCLUDES}
	${Qt5Widgets_INCLUDES}
	${mbedcrypto_INCLUDES}
	"${CMAKE_SOURCE_DIR}/deps/mbedtls/include"
	"${CMAKE_SOURCE_DIR}/plugins/obs-websocket/deps/mbedtls/include" )

target_link_libraries(obs-websocket 
	libobs
	Qt5::Core
	Qt5::WebSockets
	Qt5::Widgets
	mbedcrypto)

# --- End of section ---

# --- Windows-specific build settings and tasks ---
IF (DEFINED OBS_OUTPUT_DIR)	
	target_link_libraries(obs-websocket obs-frontend-api)
else()
	if(WIN32)

		# --- Release package helper ---
		# The "release" folder has a structure similar OBS' one on Windows
		set(RELEASE_DIR "${PROJECT_SOURCE_DIR}/release")

	

		#add_custom_command(TARGET obs-websocket POST_BUILD
		#	COMMAND if $<CONFIG:Release>==1 (
		#		"${CMAKE_COMMAND}" -E make_directory
		#		"${RELEASE_DIR}/data/obs-plugins/obs-websocket"
		#		"${RELEASE_DIR}/obs-plugins/${ARCH_NAME}")
		#		
		#	COMMAND if $<CONFIG:Release>==1 ("${CMAKE_COMMAND}" -E copy_directory
		#		"${PROJECT_SOURCE_DIR}/data"
		#		"${RELEASE_DIR}/data/obs-plugins/obs-websocket")
		#
		#	COMMAND if $<CONFIG:Release>==1 ("${CMAKE_COMMAND}" -E copy 
		#		"$<TARGET_FILE:obs-websocket>"
		#		"${QTDIR}/bin/Qt5WebSockets.dll" 
		#		"${QTDIR}/bin/Qt5Network.dll" 
		#		"${RELEASE_DIR}/obs-plugins/${ARCH_NAME}")
		#)
		# --- End of sub-section ---

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
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -fvisibility=default")

		set_target_properties(obs-websocket PROPERTIES PREFIX "")
		target_link_libraries(obs-websocket "${OBS_FRONTEND_LIB}")
	endif()

endif()

install_obs_plugin_with_data(obs-websocket data)
# -- End of section --
