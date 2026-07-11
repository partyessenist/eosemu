cmake_minimum_required(VERSION 3.20)

project(EOSEmu
	VERSION 0.1.0
	DESCRIPTION "LAN-local re-implementation of the Epic Online Services SDK 1.19.1.2"
	LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Epic's headers are the specification. We compile against them directly and
# never copy or modify them. Epic's SDK is NOT bundled with this repo (its
# license is Epic's); download it yourself and drop it in third_party/EOSSDK/
# (see third_party/EOSSDK/place_sdk_here.txt), or point EOSEMU_SDK_INCLUDE_DIR
# at an SDK/Include elsewhere.
# ---------------------------------------------------------------------------
set(EOSEMU_SDK_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/EOSSDK/SDK/Include"
	CACHE PATH "Path to the EOS SDK Include directory")

if(NOT EXISTS "${EOSEMU_SDK_INCLUDE_DIR}/eos_sdk.h")
	message(FATAL_ERROR
		"EOS SDK headers not found at '${EOSEMU_SDK_INCLUDE_DIR}'.\n"
		"Download the EOS C SDK 1.19.1.2 from the Epic Dev Portal and unzip it into\n"
		"third_party/EOSSDK/ (so third_party/EOSSDK/SDK/Include/eos_sdk.h exists), or pass\n"
		"-DEOSEMU_SDK_INCLUDE_DIR=<path to SDK/Include>. See README, \"Building from source\".")
endif()

option(EOSEMU_VERIFY_EXPORTS "Diff the built export table against the SDK headers" ON)

# The Dear ImGui social overlay is Windows-only (it stands up its own Win32
# window + D3D11 device). A headless/server build turns it off and the whole
# EOS_UI interface degrades to inert stubs. See src/overlay/ and src/interfaces/UI.cpp.
option(EOSEMU_ENABLE_OVERLAY "Build the Dear ImGui social overlay (Windows only)" ON)
if(NOT WIN32)
	set(EOSEMU_ENABLE_OVERLAY OFF)
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
# The generated stubs are committed, so building never requires Python.
# CONFIGURE_DEPENDS re-globs when gen_stubs.py adds or removes a file.
file(GLOB EOSEMU_GENERATED_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/generated/*.gen.cpp")

# Everything hand-written lives under src/, in per-concern subdirectories
# (core/, net/, interfaces/). The generated tree is compiled separately above,
# so exclude it here to avoid double definitions.
file(GLOB_RECURSE EOSEMU_HANDWRITTEN_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER EOSEMU_HANDWRITTEN_SOURCES EXCLUDE REGEX "/generated/")

if(NOT EOSEMU_GENERATED_SOURCES)
	message(FATAL_ERROR "No generated stubs found. Run: python tools/gen_stubs.py")
endif()

add_library(EOSEmu SHARED ${EOSEMU_GENERATED_SOURCES} ${EOSEMU_HANDWRITTEN_SOURCES})

target_include_directories(EOSEmu
	PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
		"${EOSEMU_SDK_INCLUDE_DIR}")

# EOS_BUILDING_SDK flips Epic's EOS_API macro from dllimport to dllexport
# (SDK/Include/eos_base.h:65). This is what makes EOS_DECLARE_FUNC usable as a
# *definition* macro and gives us the correct export decoration for free --
# undecorated on x64, _Name@N stdcall on Win32.
target_compile_definitions(EOSEmu PRIVATE EOS_BUILDING_SDK=1)

# The changelist Epic's shipping 1.19.1.2 DLL was cut from. eos_version.h folds
# BUILT_FROM_CHANGELIST into EOS_VERSION_STRING, so EOS_GetVersion reports
# "1.19.1.2-53289219" exactly as the real SDK does. Read off the real DLL
# (SDK/Bin/EOSSDK-Win64-Shipping.dll); a static_assert in src/core/Version.cpp
# pins it. Defined for every TU so no translation unit can disagree.
set(EOSEMU_EOS_CHANGELIST 53289219 CACHE STRING
	"Changelist reported by EOS_GetVersion, matching Epic's shipped DLL")
target_compile_definitions(EOSEmu PRIVATE BUILT_FROM_CHANGELIST=${EOSEMU_EOS_CHANGELIST})

# The LAN transport uses Winsock on Windows and BSD sockets elsewhere.
if(WIN32)
	target_link_libraries(EOSEmu PRIVATE ws2_32 iphlpapi)
endif()

# ---------------------------------------------------------------------------
# Dear ImGui overlay. Vendored under third_party/imgui at a pinned commit
# (see third_party/imgui/EOSEMU_VENDORED.txt). Built as a separate static
# library so ImGui's own warnings never trip our /W4, then linked into the DLL
# where its symbols stay internal (only EOS_API functions are exported). Only
# src/overlay/Overlay.cpp includes ImGui; no exported signature or generated
# stub TU may.
# ---------------------------------------------------------------------------
if(EOSEMU_ENABLE_OVERLAY)
	set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
	if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
		message(FATAL_ERROR "EOSEMU_ENABLE_OVERLAY is ON but ImGui is not vendored at ${IMGUI_DIR}")
	endif()
	add_library(eosemu_imgui STATIC
		"${IMGUI_DIR}/imgui.cpp"
		"${IMGUI_DIR}/imgui_draw.cpp"
		"${IMGUI_DIR}/imgui_tables.cpp"
		"${IMGUI_DIR}/imgui_widgets.cpp"
		"${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
		"${IMGUI_DIR}/backends/imgui_impl_dx11.cpp")
	target_include_directories(eosemu_imgui PUBLIC "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
	if(MSVC)
		target_compile_options(eosemu_imgui PRIVATE /W0)
	endif()

	target_link_libraries(EOSEmu PRIVATE eosemu_imgui d3d11 dxgi d3dcompiler dwmapi)
	target_include_directories(EOSEmu PRIVATE "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
	target_compile_definitions(EOSEmu PRIVATE EOSEMU_ENABLE_OVERLAY=1)
endif()

if(MSVC)
	target_compile_options(EOSEmu PRIVATE
		/W4
		/permissive-
		/wd4100  # unreferenced formal parameter -- every stub ignores its args
	)
else()
	target_compile_options(EOSEmu PRIVATE -Wall -Wextra -Wno-unused-parameter)
	# Only EOS_API (visibility=default) symbols should leave the shared object.
	set_target_properties(EOSEmu PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN ON)
endif()

# ---------------------------------------------------------------------------
# Output name -- consumers resolve the SDK by filename via the import table.
# There is no LoadLibrary anywhere, so the name must match exactly.
# ---------------------------------------------------------------------------
if(WIN32)
	# EOS SDK 1.19 dropped 32-bit (Win32) support, so EOSEmu is x64-only on
	# Windows -- there is no Win32 import lib to match and no Win32-Shipping DLL
	# name to produce.
	if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
		message(FATAL_ERROR "EOSEmu is 64-bit only: EOS SDK 1.19+ dropped 32-bit (Win32) support. Configure with -A x64.")
	endif()
	set_target_properties(EOSEmu PROPERTIES OUTPUT_NAME "EOSSDK-Win64-Shipping" PREFIX "")
elseif(APPLE)
	set_target_properties(EOSEmu PROPERTIES OUTPUT_NAME "EOSSDK-Mac-Shipping" PREFIX "lib" SUFFIX ".dylib")
elseif(UNIX)
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
		set_target_properties(EOSEmu PROPERTIES OUTPUT_NAME "EOSSDK-LinuxArm64-Shipping" PREFIX "lib")
	else()
		set_target_properties(EOSEmu PROPERTIES OUTPUT_NAME "EOSSDK-Linux-Shipping" PREFIX "lib")
	endif()
endif()

# ---------------------------------------------------------------------------
# Acceptance check: a missing export is a host-process load failure, not a
# runtime error, so verify the export table against the headers on every build.
# ---------------------------------------------------------------------------
if(EOSEMU_VERIFY_EXPORTS AND WIN32)
	find_package(Python3 COMPONENTS Interpreter QUIET)
	if(Python3_FOUND)
		add_custom_command(TARGET EOSEmu POST_BUILD
			COMMAND "${Python3_EXECUTABLE}"
				"${CMAKE_CURRENT_SOURCE_DIR}/tools/check_exports.py"
				"$<TARGET_FILE:EOSEmu>"
				--sdk-include "${EOSEMU_SDK_INCLUDE_DIR}"
			COMMENT "Verifying EOSEmu export table against SDK headers"
			VERBATIM)
	else()
		message(STATUS "Python3 not found; skipping export verification")
	endif()
endif()
