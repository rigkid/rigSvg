file(READ "${CMAKE_CURRENT_LIST_DIR}/pack.json" PACK_JSON)
string(JSON PACK_NAME GET "${PACK_JSON}" name)

if(NOT TARGET rigPlotComponent)
	if(DEFINED RIGKIT_ROOT AND NOT RIGKIT_ROOT STREQUAL "")
		set(_dep "${RIGKIT_ROOT}/packs/rigPlotComponent")
	else()
		set(_dep "${CMAKE_SOURCE_DIR}/packs/rigPlotComponent")
	endif()
	if(EXISTS "${_dep}/CMakeLists.txt")
		add_subdirectory("${_dep}" "${CMAKE_BINARY_DIR}/packs/rigPlotComponent")
	endif()
endif()

if(NOT TARGET rigTrueType)
	if(DEFINED RIGKIT_ROOT AND NOT RIGKIT_ROOT STREQUAL "")
		set(_tt "${RIGKIT_ROOT}/packs/rigTrueType")
	else()
		set(_tt "${CMAKE_SOURCE_DIR}/packs/rigTrueType")
	endif()
	if(EXISTS "${_tt}/CMakeLists.txt")
		add_subdirectory("${_tt}" "${CMAKE_BINARY_DIR}/packs/rigTrueType")
	endif()
endif()

if(NOT TARGET rigTrueType)
	if(DEFINED RIGKIT_ROOT AND NOT RIGKIT_ROOT STREQUAL "")
		set(_tt "${RIGKIT_ROOT}/packs/rigTrueType")
	else()
		set(_tt "${CMAKE_SOURCE_DIR}/packs/rigTrueType")
	endif()
	if(EXISTS "${_tt}/CMakeLists.txt")
		add_subdirectory("${_tt}" "${CMAKE_BINARY_DIR}/packs/rigTrueType")
	endif()
endif()

if(NOT TARGET rigTrueType)
	if(DEFINED RIGKIT_ROOT AND NOT RIGKIT_ROOT STREQUAL "")
		set(_tt "${RIGKIT_ROOT}/packs/rigTrueType")
	else()
		set(_tt "${CMAKE_SOURCE_DIR}/packs/rigTrueType")
	endif()
	if(EXISTS "${_tt}/CMakeLists.txt")
		add_subdirectory("${_tt}" "${CMAKE_BINARY_DIR}/packs/rigTrueType")
	endif()
endif()

add_library(${PACK_NAME} STATIC
	src/rigSvg.cpp
	src/rigSvg.h
	src/SvgLoader.cpp
	src/SvgLoader.h
	src/SvgWriter.cpp
	src/SvgWriter.h
	src/SvgDocument.cpp
	src/SvgDocument.h
	src/SvgPathParser.cpp
	src/SvgPathParser.h
	src/SvgThumbnail.cpp
	src/SvgThumbnail.h
)
target_link_libraries(${PACK_NAME} PUBLIC rigkit)
if(TARGET rigPlotComponent)
	target_link_libraries(${PACK_NAME} PUBLIC rigPlotComponent)
endif()
if(TARGET rigTrueType)
	target_link_libraries(${PACK_NAME} PUBLIC rigTrueType)
endif()
target_include_directories(${PACK_NAME} PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/src>
)
if(TARGET blend2d)
	target_compile_definitions(${PACK_NAME} PRIVATE RIGSVG_HAS_BLEND2D)
	target_link_libraries(${PACK_NAME} PRIVATE blend2d)
endif()

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/tools/svg_smoke.cpp")
	add_executable(svg_smoke "${CMAKE_CURRENT_LIST_DIR}/tools/svg_smoke.cpp")
	target_link_libraries(svg_smoke PRIVATE ${PACK_NAME})
endif()

message(STATUS "Configured pack: ${PACK_NAME} (SVG import/export)")
