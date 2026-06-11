cmake_minimum_required(VERSION 3.13)
project(OpenZFS LANGUAGES C CXX ASM)
SET (CMAKE_ASM_COMPILER_ID Clang)
SET (CMAKE_ASM_COMPILER clang-cl.exe)

include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/contrib/windows/cmake")
find_package(WDK REQUIRED)

option(BUILD_SHARED_LIBS "Build Shared Libraries" OFF)

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
	set(OPENSSL_CRYPTO_LIBRARY "C:/Program Files/OpenSSL-Win64/lib/VC/x64/MTd/libcrypto_static.lib")
	set(OPENSSL_SSL_LIBRARY "C:/Program Files/OpenSSL-Win64/lib/VC/x64/MTd/libssl_static.lib")
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "ARM64")
	set(OPENSSL_CRYPTO_LIBRARY "C:/Program Files/OpenSSL-Win64-ARM/lib/VC/arm64/MTd/libcrypto_static.lib")
	set(OPENSSL_SSL_LIBRARY "C:/Program Files/OpenSSL-Win64-ARM/lib/VC/arm64/MTd/libssl_static.lib")
endif()

set(OPENSSL_USE_STATIC_LIBS TRUE)
set(OPENSSL_MSVC_STATIC_RT TRUE)
find_package(OpenSSL REQUIRED)
# message(STATUS "OpenSSL libs: " ${OPENSSL_LIBRARIES})
# Seriously, why does it pick MD versions and not MT like requested?
# string(REGEX REPLACE "64MD" "64MT" INTERNAL_OPENSSL_LIBRARIES ${OPENSSL_LIBRARIES})
#message(STATUS "OpenSSL libs: " ${OPENSSL_CRYPTO_LIBRARY})
#cmake_path(GET ${OPENSSL_LIBRARIES} ROOT_PATH rootPath)
#get_filename_component(rootPath "${OPENSSL_CRYPTO_LIBRARY}" DIRECTORY)
#message(STATUS "OpenSSL root: " ${rootPath})


# Attempt to simulate scripts/make-gitrev.h and zfs_gitrev.h
include(GetGitRevisionDescription)
# Take this out, or all cherry-picks do full recompile.
# get_git_head_revision(GIT_REFSPEC GIT_HASH)
git_describe_working_tree(GIT_GITREV)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/include/zfs_gitrev.h.win" "${CMAKE_CURRENT_SOURCE_DIR}/include/zfs_gitrev.h" @ONLY)
#

function(use_clang)
# It seems "-include file" is "/FI file" on Windows
add_compile_options(/FI
	${CMAKE_SOURCE_DIR}/include/os/windows/zfs/zfs_config.h
)
set(CMAKE_C_COMPILER clang-cl.exe PARENT_SCOPE)
set(CMAKE_CXX_COMPILER clang-cl.exe PARENT_SCOPE)

if (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x86")
		add_compile_options(-m32)
elseif (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
		add_definitions(-D_LP64 -D__LP64__)
		add_compile_options(-m64)
elseif (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64") # all except wdk_add_library
		add_compile_options(-target aarch64-pc-windows-msvc)
else()
	message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

add_compile_options(
	# These are all MS headers
	-Wno-nonportable-include-path
	-Wno-unknown-pragmas
	-Wno-ignored-pragma-intrinsic
	-Wno-pragma-pack
	-Wno-microsoft-enum-forward-reference
	-Wno-visibility
	-Wno-microsoft-anon-tag
	# -Wno-ignored-attributes
	-Wno-unused-command-line-argument
	-Wno-unused-local-typedef
	-Wno-int-to-void-pointer-cast
	# ZFS related
	-Wno-missing-field-initializers
	#-fms-extensions
	-Wno-unused-function
	-Wno-unused-label
	#Visual Studio 2022 update 2022114 started breaking builds with errors
	-Wno-int-conversion
	-Wunknown-pragmas
)

add_compile_options(/Zi /Od /Oy- /Zo /Ob0)
add_link_options(/DEBUG:FULL /INCREMENTAL /OPT:NOREF /OPT:NOICF)

endfunction()

string(CONCAT WDK_LINK_FLAGS
	"/DEBUG "
)
if (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x86")
		list(APPEND WDK_COMPILE_DEFINITIONS	_KERNEL __KERNEL__ _LP32 __LP32__)
		list(APPEND WDK_COMPILE_FLAGS
			/Z7
			-m32
		)
elseif (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
		list(APPEND WDK_COMPILE_DEFINITIONS	_KERNEL __KERNEL__ _LP64 __LP64__)
		list(APPEND WDK_COMPILE_FLAGS
			/Z7
			-m64
		)
elseif (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64") # all except wdk_add_library
		list(APPEND WDK_COMPILE_DEFINITIONS	_KERNEL __KERNEL__ ARM64 _ARM64_ __aarch64__ _M_ARM64 _LP64 __LP64__)
		list(APPEND WDK_COMPILE_FLAGS
			/Z7
			--target=arm64-windows-msvc
		)
else()
	message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDBG -DZFS_DEBUG)
endif()

# Avoid dependency add_compile_definitions vcruntime140.dll
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")

# Let's try to emulate gitrev
#find_path(BASH_DIR NAMES bash.exe git-bash.exe PATHS "/c/Program Files (x86)/Git/")
#execute_process(COMMAND $BASH_DIR -c "scripts/make_gitrev.h include/zfs_gitrev.h" )


set(WPP_DIR "${WDK_ROOT}/bin/${WDK_VERSION}")
set(TRACE "TraceEvent{FLAGS=MYDRIVER_ALL_INFO}(LEVEL, MSG, ...)")
set(DPRINT "dprintf{FLAGS=MYDRIVER_ALL_INFO, LEVEL=TRACE_INFO}(MSG, ...)")
set(CFGDIR "${WPP_DIR}/wppconfig/rev1")
set(SCAN "${WDK_ROOT}/Include/wdf/kmdf/1.9/WdfTraceEnums.h")
set(WPP_MACRO "WPP_INLINE __inline")


function(tmh_generation CSOURCES TMH_FILE_LIST)
	foreach (C_FILE ${CSOURCES})
		get_filename_component(OUTPUT_FILE_WE ${C_FILE} NAME_WE)
		set(TMH_FILE  ${CMAKE_SOURCE_DIR}/out/build/${OUTPUT_FILE_WE}.tmh)
		set_source_files_properties(${C_FILE} PROPERTIES COMPILE_FLAGS -DWPPFILE=${TMH_FILE})
		set(LOCAL_TMH_FILE_LIST ${LOCAL_TMH_FILE_LIST} ${TMH_FILE})

		add_custom_command(OUTPUT ${TMH_FILE}
		COMMAND         "${WPP_DIR}/${WDK_PLATFORM}/tracewpp.exe"
		-scan:${SCAN} /D${WPP_MACRO}
		-cfgdir:${CFGDIR} -I${CMAKE_CURRENT_BINARY_DIR} -odir:"${CMAKE_SOURCE_DIR}/out/build" -km -func:${TRACE}
		-func:${DPRINT} -gen:{km-default.tpl}*.tmh ${C_FILE}
		WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" DEPENDS ${C_FILE})

		message(STATUS "Kernel Mode FILES_IN ======: ${CMAKE_SOURCE_DIR}/out/build/${OUTPUT_FILE_WE}.tmh")
	endforeach()
	set(TMH_FILE_LIST ${LOCAL_TMH_FILE_LIST} PARENT_SCOPE)
endfunction()

function(tmh_generation_for_usermode CSOURCES TMH_FILE_LIST)
	foreach (C_FILE ${CSOURCES})
		get_filename_component(OUTPUT_FILE_WE ${C_FILE} NAME_WE)
		get_filename_component(WORK_DIRECTORY ${C_FILE} DIRECTORY)
		set(TMH_FILE  ${CMAKE_SOURCE_DIR}/out/build/${OUTPUT_FILE_WE}.tmh)
		set_source_files_properties(${C_FILE} PROPERTIES COMPILE_FLAGS -DWPPFILE=${TMH_FILE})
		set(LOCAL_TMH_FILE_LIST ${LOCAL_TMH_FILE_LIST} ${TMH_FILE})

		add_custom_command(OUTPUT ${TMH_FILE}
		COMMAND         "${WPP_DIR}/${WDK_PLATFORM}/tracewpp.exe"
		-scan:${SCAN} /D${WPP_MACRO}
		-cfgdir:${CFGDIR} -I${CMAKE_CURRENT_BINARY_DIR} -odir:"${CMAKE_SOURCE_DIR}/out/build" -p:usermode -func:${TRACE}
		-func:${DPRINT} ${C_FILE}
		WORKING_DIRECTORY "${WORK_DIRECTORY}" DEPENDS ${C_FILE})

		message(STATUS "User Mode FILES_IN ======: ${CMAKE_SOURCE_DIR}/out/build/${OUTPUT_FILE_WE}.tmh")
	endforeach()
	set(TMH_FILE_LIST ${LOCAL_TMH_FILE_LIST} PARENT_SCOPE)
endfunction()

add_subdirectory(module)
#add_subdirectory(module/zfs)
#add_subdirectory(zfsinstaller)

add_subdirectory(lib)
add_subdirectory(cmd)

