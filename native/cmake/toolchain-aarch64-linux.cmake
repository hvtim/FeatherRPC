# Cross-compiles the Linux target for aarch64 using Fedora's
# gcc-aarch64-linux-gnu toolchain against a dev-package sysroot built via:
#
#   sudo dnf --forcearch=aarch64 --installroot=<sysroot> --releasever=44 \
#       --use-host-config install --setopt=install_weak_deps=False \
#       glibc-devel gtk3-devel dbus-devel \
#       libayatana-appindicator-gtk3-devel libcurl-devel
#
# That command reports "Transaction failed" on an x86_64 host - its
# post-install scriptlets (ldconfig, glib2/fontconfig cache triggers) try
# to exec aarch64 binaries, which only fails because nothing here can run
# them. The headers/.so/.pc files still land on disk before those
# scriptlets run, which is all a cross-compile needs - confirmed by
# actually building with this toolchain file.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

if(NOT DEFINED ITUNES_RPC_AARCH64_SYSROOT)
    set(ITUNES_RPC_AARCH64_SYSROOT "/opt/aarch64-sysroot" CACHE PATH "aarch64 dev-package sysroot")
endif()
set(CMAKE_SYSROOT "${ITUNES_RPC_AARCH64_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH "${ITUNES_RPC_AARCH64_SYSROOT}")

# gcc-c++-aarch64-linux-gnu is cross-tooling only, with no bundled
# libstdc++ headers of its own, and doesn't search <sysroot>/usr/include/c++
# by default - the dnf --forcearch sysroot's headers live under the
# native Fedora aarch64 triple name (aarch64-redhat-linux), not this cross
# package's own triple (aarch64-linux-gnu), so they must be added explicitly.
set(_libstdcxx_include "${ITUNES_RPC_AARCH64_SYSROOT}/usr/include/c++/16")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isystem ${_libstdcxx_include} -isystem ${_libstdcxx_include}/aarch64-redhat-linux")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# The sysroot's .pc files bake in absolute host-style prefixes (/usr), so
# PKG_CONFIG_SYSROOT_DIR alone won't redirect pkg-config to them - point
# PKG_CONFIG_LIBDIR directly at the sysroot's own pkgconfig directories
# instead of the host's x86_64 ones.
set(ENV{PKG_CONFIG_LIBDIR} "${ITUNES_RPC_AARCH64_SYSROOT}/usr/lib64/pkgconfig:${ITUNES_RPC_AARCH64_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${ITUNES_RPC_AARCH64_SYSROOT}")
