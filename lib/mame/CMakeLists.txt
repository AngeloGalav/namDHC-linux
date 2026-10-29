# Minimal build of MAME's chdman and the parts of MAME it depends on.
# Sources are vendored verbatim from https://github.com/mamedev/mame
# (see VERSION.txt for the exact revision).

set(MAME_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(TP ${MAME_DIR}/3rdparty)

# ---------------------------------------------------------------- zlib
add_library(mame_zlib STATIC
	${TP}/zlib/adler32.c ${TP}/zlib/compress.c ${TP}/zlib/crc32.c
	${TP}/zlib/deflate.c ${TP}/zlib/inffast.c ${TP}/zlib/inflate.c
	${TP}/zlib/infback.c ${TP}/zlib/inftrees.c ${TP}/zlib/trees.c
	${TP}/zlib/uncompr.c ${TP}/zlib/zutil.c)
target_include_directories(mame_zlib PUBLIC ${TP}/zlib)
target_compile_definitions(mame_zlib PUBLIC ZLIB_CONST PRIVATE HAVE_UNISTD_H)
target_compile_options(mame_zlib PRIVATE -w)

# ---------------------------------------------------------------- lzma (7z)
set(LZ ${TP}/lzma/C)
add_library(mame_7z STATIC
	${LZ}/7zAlloc.c ${LZ}/7zArcIn.c ${LZ}/7zBuf.c ${LZ}/7zBuf2.c ${LZ}/7zCrc.c
	${LZ}/7zCrcOpt.c ${LZ}/7zDec.c ${LZ}/7zFile.c ${LZ}/7zStream.c ${LZ}/Aes.c
	${LZ}/AesOpt.c ${LZ}/Alloc.c ${LZ}/Bcj2.c ${LZ}/Bra.c ${LZ}/Bra86.c
	${LZ}/BraIA64.c ${LZ}/CpuArch.c ${LZ}/Delta.c ${LZ}/LzFind.c ${LZ}/LzFindOpt.c
	${LZ}/Lzma2Dec.c ${LZ}/LzmaDec.c ${LZ}/LzmaEnc.c ${LZ}/Ppmd7.c ${LZ}/Ppmd7Dec.c
	${LZ}/Sha256.c ${LZ}/Sha256Opt.c ${LZ}/Sort.c)
target_include_directories(mame_7z PUBLIC ${TP}/lzma/C)
target_compile_definitions(mame_7z PRIVATE Z7_ST _7ZIP_ST)
target_compile_options(mame_7z PRIVATE -w)

# ---------------------------------------------------------------- flac
set(FL ${TP}/flac/src/libFLAC)
add_library(mame_flac STATIC
	${FL}/bitmath.c ${FL}/bitreader.c ${FL}/bitwriter.c ${FL}/cpu.c ${FL}/crc.c
	${FL}/fixed.c ${FL}/fixed_intrin_avx2.c ${FL}/fixed_intrin_sse2.c
	${FL}/fixed_intrin_sse42.c ${FL}/fixed_intrin_ssse3.c ${FL}/float.c
	${FL}/format.c ${FL}/lpc.c ${FL}/lpc_intrin_avx2.c ${FL}/lpc_intrin_fma.c
	${FL}/lpc_intrin_neon.c ${FL}/lpc_intrin_sse2.c ${FL}/lpc_intrin_sse41.c
	${FL}/md5.c ${FL}/memory.c ${FL}/stream_decoder.c ${FL}/stream_encoder.c
	${FL}/stream_encoder_framing.c ${FL}/stream_encoder_intrin_avx2.c
	${FL}/stream_encoder_intrin_sse2.c ${FL}/stream_encoder_intrin_ssse3.c
	${FL}/window.c)
target_include_directories(mame_flac PUBLIC ${TP}/flac/include PRIVATE ${FL}/include)
target_compile_definitions(mame_flac
	PUBLIC FLAC__NO_DLL
	PRIVATE NDEBUG HAVE_CONFIG_H OGG_FOUND=0 FLAC__HAS_OGG=0 HAVE_LROUND=1
		HAVE_INTTYPES_H HAVE_STDBOOL_H HAVE_STDINT_H HAVE_STDIO_H HAVE_STDLIB_H
		HAVE_STRING_H HAVE_FSEEKO _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE
		FLAC__SYS_LINUX)
if(CMAKE_C_BYTE_ORDER STREQUAL "BIG_ENDIAN")
	target_compile_definitions(mame_flac PRIVATE CPU_IS_BIG_ENDIAN=1 CPU_IS_LITTLE_ENDIAN=0 WORDS_BIGENDIAN=1)
else()
	target_compile_definitions(mame_flac PRIVATE CPU_IS_BIG_ENDIAN=0 CPU_IS_LITTLE_ENDIAN=1 WORDS_BIGENDIAN=0)
endif()
target_compile_options(mame_flac PRIVATE -w)

# ---------------------------------------------------------------- zstd
set(ZS ${TP}/zstd/lib)
add_library(mame_zstd STATIC
	${ZS}/common/debug.c ${ZS}/common/entropy_common.c ${ZS}/common/error_private.c
	${ZS}/common/fse_decompress.c ${ZS}/common/pool.c ${ZS}/common/threading.c
	${ZS}/common/xxhash.c ${ZS}/common/zstd_common.c
	${ZS}/compress/fse_compress.c ${ZS}/compress/hist.c ${ZS}/compress/huf_compress.c
	${ZS}/compress/zstd_compress.c ${ZS}/compress/zstd_compress_literals.c
	${ZS}/compress/zstd_compress_sequences.c ${ZS}/compress/zstd_compress_superblock.c
	${ZS}/compress/zstd_double_fast.c ${ZS}/compress/zstd_fast.c ${ZS}/compress/zstd_lazy.c
	${ZS}/compress/zstd_ldm.c ${ZS}/compress/zstdmt_compress.c ${ZS}/compress/zstd_opt.c
	${ZS}/compress/zstd_preSplit.c
	${ZS}/decompress/huf_decompress.c ${ZS}/decompress/zstd_ddict.c
	${ZS}/decompress/zstd_decompress_block.c ${ZS}/decompress/zstd_decompress.c)
target_include_directories(mame_zstd PUBLIC ${ZS})
target_compile_definitions(mame_zstd PRIVATE NDEBUG XXH_NAMESPACE=ZSTD_ DEBUGLEVEL=0 ZSTD_DISABLE_ASM)
target_compile_options(mame_zstd PRIVATE -w)

# ---------------------------------------------------------------- utf8proc
add_library(mame_utf8proc STATIC ${TP}/utf8proc/utf8proc.c)
target_include_directories(mame_utf8proc PUBLIC ${TP}/utf8proc)
target_compile_definitions(mame_utf8proc PUBLIC UTF8PROC_STATIC)
target_compile_options(mame_utf8proc PRIVATE -w)

# ---------------------------------------------------------------- MAME core (osd + util)
set(MAME_COMMON_DEFS NDEBUG CRLF=2 PTR64=1)
if(NOT CMAKE_CXX_BYTE_ORDER STREQUAL "BIG_ENDIAN")
	list(APPEND MAME_COMMON_DEFS LSB_FIRST)
endif()

set(OSD ${MAME_DIR}/src/osd)
set(UTIL ${MAME_DIR}/src/lib/util)
add_library(mame_util STATIC
	${OSD}/osdcore.cpp ${OSD}/strconv.cpp ${OSD}/osdsync.cpp
	${OSD}/modules/lib/osdlib_unix.cpp
	${OSD}/modules/file/posixdir.cpp ${OSD}/modules/file/posixfile.cpp
	${OSD}/modules/file/posixptty.cpp ${OSD}/modules/file/posixsocket.cpp
	${UTIL}/avhuff.cpp ${UTIL}/aviio.cpp ${UTIL}/bitmap.cpp ${UTIL}/cdrom.cpp
	${UTIL}/chd.cpp ${UTIL}/chdcodec.cpp ${UTIL}/corealloc.cpp ${UTIL}/corefile.cpp
	${UTIL}/corestr.cpp ${UTIL}/coreutil.cpp ${UTIL}/dvdrom.cpp ${UTIL}/flac.cpp
	${UTIL}/harddisk.cpp ${UTIL}/hashing.cpp ${UTIL}/huffman.cpp ${UTIL}/ioprocs.cpp
	${UTIL}/ioprocsfilter.cpp ${UTIL}/ioprocsstream.cpp ${UTIL}/md5.cpp
	${UTIL}/palette.cpp ${UTIL}/path.cpp ${UTIL}/strformat.cpp ${UTIL}/unicode.cpp
	${UTIL}/vbiparse.cpp ${UTIL}/vecstream.cpp)
target_include_directories(mame_util PUBLIC ${OSD} ${UTIL} ${MAME_DIR}/src/lib ${TP})
target_compile_definitions(mame_util PUBLIC ${MAME_COMMON_DEFS} PRIVATE NAMDHC_NO_SDL)
target_compile_options(mame_util PRIVATE -w)
target_link_libraries(mame_util PUBLIC mame_zlib mame_7z mame_flac mame_zstd mame_utf8proc Threads::Threads ${CMAKE_DL_LIBS})

# ---------------------------------------------------------------- chdman
add_executable(chdman ${CMAKE_CURRENT_SOURCE_DIR}/../../chdman/chdman.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../../chdman/version.cpp)
target_link_libraries(chdman PRIVATE mame_util)
target_compile_options(chdman PRIVATE -w)
set_target_properties(chdman PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
install(TARGETS chdman RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
