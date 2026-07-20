# PSX-iTests - build script adapted from ps1-ram-tester / ps1-bare-metal
# by spicyjpeg (https://github.com/spicyjpeg), used here under the MIT
# license. See LICENSE for the full original license text.
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
# REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
# AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
# INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
# LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
# OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
# PERFORMANCE OF THIS SOFTWARE.

cmake_minimum_required(VERSION 3.25)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/toolchain.cmake")

project(
	PSX-iTests
	LANGUAGES    C ASM
	VERSION      1.0.0
	DESCRIPTION  "PlayStation 1 basic CPU/GPU/SPU functionality test tool"
	HOMEPAGE_URL "https://github.com/spicyjpeg/ps1-bare-metal"
)

include(cmake/options.cmake)
include(cmake/setup.cmake)
include(cmake/tools.cmake)

## Standard library

add_library(
	common OBJECT
	src/libc/clz.s
	#src/libc/crt0.c
	#src/libc/cxxsupport.cpp
	#src/libc/malloc.c
	src/libc/misc.c
	src/libc/setjmp.s
	src/libc/string.c
	src/libc/string.s
	src/ps1/cache.s
	src/vendor/printf.c
)
target_include_directories(
	common PUBLIC
	src
	src/libc
)
link_libraries(common)

## Main executable

addPS1Executable(
	"${RELEASE_NAME}"
	"${RELEASE_INFO}"
	src/common/gpu.c
	src/common/reboot.c
	src/common/reboot.s
	src/common/sio0.c
	src/common/spu.c
	src/main/cd_player.c
	src/main/cdboot.c
	src/main/cdboot.s
	src/main/cpu_bench.c
	src/main/console_info.c
	src/main/memcard.c
	src/main/crt0.s
	src/main/font.c
	src/main/gpu_colorbars.c
	src/main/gpu_cube.c
	src/main/main.c
	src/main/mainmenu.c
	src/main/modals.c
	src/main/pad_test.c
	src/main/ramconfig.c
	src/main/ramtester.c
	src/main/renderer.c
	src/main/sound.c
	src/main/spu_channel_test.c
	src/main/test.c
	src/main/trig.c
	src/main/ui.c
)
target_compile_options(
	"${RELEASE_NAME}" PRIVATE
	-Wall
	-Wextra
)
target_compile_definitions(
	"${RELEASE_NAME}" PRIVATE
	$<$<BOOL:${ENABLE_LOGGING}>:ENABLE_LOGGING=1>
	VERSION="${PROJECT_VERSION}"
)

convertImage(assets/background.png 4 assets/bgTexture.dat assets/bgPalette.dat)
addBinaryFile("${RELEASE_NAME}" bgTexture "${PROJECT_BINARY_DIR}/assets/bgTexture.dat")
addBinaryFile("${RELEASE_NAME}" bgPalette "${PROJECT_BINARY_DIR}/assets/bgPalette.dat")

convertImage(assets/font.png 4 assets/fontTexture.dat assets/fontPalette.dat)
addBinaryFile("${RELEASE_NAME}" fontTexture "${PROJECT_BINARY_DIR}/assets/fontTexture.dat")
addBinaryFile("${RELEASE_NAME}" fontPalette "${PROJECT_BINARY_DIR}/assets/fontPalette.dat")

addBinaryFile("${RELEASE_NAME}" uiSound assets/pickup.vag)
addBinaryFile("${RELEASE_NAME}" bgmSound assets/bgm.vag)
addBinaryFile("${RELEASE_NAME}" spuTestSound assets/spu.vag)

## CD-ROM image

configure_file(assets/cdreadme.txt  readme.txt      NEWLINE_STYLE CRLF)
configure_file(LICENSE              license.txt     NEWLINE_STYLE CRLF)
configure_file(assets/system.cnf    system.cnf      NEWLINE_STYLE CRLF)
configure_file(assets/isoproject.xml isoproject.xml NEWLINE_STYLE LF)

# Builds the CD-ROM image with mkpsxiso (https://github.com/Lameguy64/mkpsxiso)
# instead of the previous custom Python builder - the custom builder never
# wrote a SYSTEM.CNF at all (silently non-bootable on real hardware) and
# mkpsxiso is the same proven, widely-used tool most PS1 homebrew projects
# rely on. NOTE: this build does not inject Sony license/logo data (the
# region text + boot logo splash) - the confirmed-working reference
# project this was adapted from didn't use it either, and it's a cosmetic
# omission only, not a functional one (the disc still boots fine without
# it).
if(NOT MKPSXISO_PATH)
	message(FATAL_ERROR
		"mkpsxiso not found. Install it and make sure it's on PATH, or "
		"pass -DMKPSXISO_PATH=/path/to/mkpsxiso when configuring."
	)
endif()

add_custom_command(
	COMMAND
		"${MKPSXISO_PATH}"
		isoproject.xml
	OUTPUT
		"${RELEASE_NAME}.bin"
		"${RELEASE_NAME}.cue"
	DEPENDS
		isoproject.xml
		readme.txt
		license.txt
		system.cnf
		"${RELEASE_NAME}"
	COMMENT "Building CD-ROM BIN/CUE image with mkpsxiso"
	VERBATIM
)

add_custom_target(
	cdrom ALL
	DEPENDS "${RELEASE_NAME}.bin"
)
