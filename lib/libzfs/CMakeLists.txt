
use_clang()

add_library(libzfs
	libzfs_changelist.c
	libzfs_config.c
	libzfs_crypto.c
	libzfs_dataset.c
	libzfs_diff.c
	libzfs_import.c
	libzfs_iter.c
	libzfs_mount.c
	libzfs_pool.c
	libzfs_sendrecv.c
	libzfs_status.c
	libzfs_util.c
	os/windows/libzfs_disk_os.cpp
	os/windows/libzfs_mount_os.c
	os/windows/libzfs_pool_os.c
	os/windows/libzfs_util_os.c
)

# find_package(OpenSSL)'s own LIB_EAY_DEBUG/LIB_EAY_RELEASE search (see
# contrib/windows/cmake/FindOpenSSL.cmake) correctly locates the CRT-
# matched static libs, but nothing in this tree actually links against
# OPENSSL_CRYPTO_LIBRARY - hence this direct find_library, picking the
# variant that matches CMAKE_BUILD_TYPE (a fresh search hardcoded to the
# MTd/debug name here would silently link the debug-CRT crypto lib into
# a Release build too). Cache variable name changed from the old
# CRYPTO_STATIC_TEST so this re-searches on the next configure rather
# than reusing a stale cached path from before this logic existed.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(_libzfs_openssl_crypto_name libcrypto64MTd)
else()
	set(_libzfs_openssl_crypto_name libcrypto64MT)
endif()

find_library(LIBZFS_OPENSSL_CRYPTO
    NAMES
      ${_libzfs_openssl_crypto_name}
    NAMES_PER_DIR
	HINTS
	  "C:/Program Files/OpenSSL-Win64/lib/VC/static"
    PATH_SUFFIXES
      lib
	REQUIRED
  )
unset(_libzfs_openssl_crypto_name)

target_include_directories(libzfs PRIVATE "${CMAKE_SOURCE_DIR}/lib/libzfs")
target_link_libraries(libzfs PUBLIC libpthread zlib libzutil libshare libzfs_core libnvpair libuutil)
target_link_libraries(libzfs PRIVATE Crypt32.lib ${LIBZFS_OPENSSL_CRYPTO})
