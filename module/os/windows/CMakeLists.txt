
#include(ExternalProject)

#ExternalProject_Add ( spl
#    PREFIX "spl"
#    CONFIGURE_COMMAND ${CMAKE_COMMAND} 
#    SOURCE_DIR "spl" # Tell CMake to use subdirectory as source.
#)

#ExternalProject_Add ( zfs
#    PREFIX "zfs"
#    CONFIGURE_COMMAND ${CMAKE_COMMAND} 
#    SOURCE_DIR "zfs" # Tell CMake to use subdirectory as source.
#)


add_subdirectory(spl)
add_subdirectory(zfs)

file(GLOB CSOURCES CONFIGURE_DEPENDS "*.c")

set(DRIVER_INSTALL_DIR "ZFSin")

set(TMH_FILE_LIST "")
tmh_generation("${CSOURCES}" TMH_FILE_LIST)

wdk_add_driver(ZFSin
  debug.c
  driver.c
  Wpp.c
  OpenZFS_perf.rc
  resource.rc
  ${TMH_FILE_LIST}
)

message(STATUS "ZFSIN_MAIN:- ${TMH_FILE_LIST}")

target_link_libraries(ZFSin PRIVATE
	splkern
	zlibkern
	icpkern
	luakern
	zfskern
	zfskern_os
	zcommonkern
	nvpairkern
	unicodekern
	# WDK libraries
	WDK::WDMSEC
	WDK::STORPORT
	WDK::SCSIWMI
)

# set(CMAKE_LINKER link.exe)

set_target_properties(ZFSin
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/driver"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/driver"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/driver"
)

configure_file(ZFSin.inf "${CMAKE_CURRENT_BINARY_DIR}/driver/ZFSin.inf" COPYONLY)
configure_file(OpenZFS.man "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.man" COPYONLY)

add_custom_command(
	TARGET ZFSin
	POST_BUILD
	BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/driver/ZFSin.cat"
    COMMAND "${STAMPINF_PROGRAM}" -d "*" -a "amd64" -v "*" -f "${CMAKE_CURRENT_BINARY_DIR}\\driver\\ZFSin.inf"
	
	COMMAND "${INF2CAT_PROGRAM}" /verbose "/driver:${CMAKE_CURRENT_BINARY_DIR}/driver" /os:10_19H1_X86,10_19H1_X64,ServerRS5_X64 /uselocaltime
	#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /fd sha1 /s "${ZFSIN_SIGNTOOL_CERTSTORE}" /n "${ZFSIN_SIGNTOOL_CERTNAME}" /t "${ZFSIN_SIGNTOOL_TSA}" "${CMAKE_CURRENT_BINARY_DIR}/driver/ZFSin.cat"
	#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /fd sha1 /sha1 "${ZFSIN_SIGNTOOL_SHA1}" /t "${ZFSIN_SIGNTOOL_TSA}" "${CMAKE_CURRENT_BINARY_DIR}/driver/ZFSin.cat"
	#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /s "${ZFSIN_SIGNTOOL_CERTSTORE}" /n "${ZFSIN_SIGNTOOL_CERTNAME}" /tr "${ZFSIN_SIGNTOOL_TSA}" "${CMAKE_CURRENT_BINARY_DIR}/driver/ZFSin.cat"
	#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /sha1 "${ZFSIN_SIGNTOOL_SHA1}" /tr "${ZFSIN_SIGNTOOL_TSA}" "${CMAKE_CURRENT_BINARY_DIR}/driver/ZFSin.cat"
	#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /fd sha1 /s "${ZFSIN_SIGNTOOL_CERTSTORE}" /n "${ZFSIN_SIGNTOOL_CERTNAME}" /t "${ZFSIN_SIGNTOOL_TSA}" $<TARGET_FILE:ZFSin>
	#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /fd sha1 /sha1 "${ZFSIN_SIGNTOOL_SHA1}" /t "${ZFSIN_SIGNTOOL_TSA}" $<TARGET_FILE:ZFSin>
	#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /s "${ZFSIN_SIGNTOOL_CERTSTORE}" /n "${ZFSIN_SIGNTOOL_CERTNAME}" /tr "${ZFSIN_SIGNTOOL_TSA}" $<TARGET_FILE:ZFSin>
	#COMMAND "${SIGNTOOL_PROGRAM}" sign /v /as /fd sha256 /sha1 "${ZFSIN_SIGNTOOL_SHA1}" /tr "${ZFSIN_SIGNTOOL_TSA}" $<TARGET_FILE:ZFSin>
	WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/driver"
	VERBATIM COMMENT "Generating and signing .cat file"
)
	
install(TARGETS ZFSin RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/${DRIVER_INSTALL_DIR}")
install(FILES 
	"${CMAKE_CURRENT_BINARY_DIR}/driver/ZFSin.inf"
	"${CMAKE_CURRENT_BINARY_DIR}/driver/ZFSin.cat"
    "${CMAKE_CURRENT_BINARY_DIR}/driver/OpenZFS.man"
	DESTINATION "${CMAKE_INSTALL_BINDIR}/${DRIVER_INSTALL_DIR}"
)

install(FILES
	$<TARGET_PDB_FILE:ZFSin>
	DESTINATION "${CMAKE_INSTALL_BINDIR}/${DRIVER_INSTALL_DIR}"
	OPTIONAL
)

execute_process(COMMAND  "${WDK_ROOT}/bin/${WDK_VERSION}/x86/ctrpp.exe"
  -o ${CMAKE_CURRENT_SOURCE_DIR}/OpenZFS_perf.h
  -rc ${CMAKE_CURRENT_SOURCE_DIR}/OpenZFS_perf.rc
  -ch ${CMAKE_CURRENT_SOURCE_DIR}/OpenZFS_counters.h
 ${CMAKE_CURRENT_SOURCE_DIR}/OpenZFS.man )
