#!/usr/bin/env bash
# Build the server into /realm/build; with --install, copy the binaries to /realm/install.
# Runs inside the toolchain container (see compose.yaml). LA_SANITIZE=address builds a
# separate AddressSanitizer server in /realm/build-address for hunting memory errors.
set -euo pipefail
install=0
if [ "${1:-}" = "--install" ]; then install=1; shift; fi
build=/realm/build sanitize=()
if [ -n "${LA_SANITIZE:-}" ]; then
  build=/realm/build-$LA_SANITIZE install=0
  sanitize=("-DCMAKE_CXX_FLAGS=-fsanitize=$LA_SANITIZE -fno-omit-frame-pointer"
            "-DCMAKE_C_FLAGS=-fsanitize=$LA_SANITIZE -fno-omit-frame-pointer"
            "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=$LA_SANITIZE" -DNOJEM=1)
fi
mkdir -p "$build"
exec 9>"$build.lock"
flock -n 9 || { echo 'Another build of this realm is running.' >&2; exit 1; }
cmake -S /source -B "$build" -G Ninja \
  -DCMAKE_INSTALL_PREFIX=/realm/install -DCONF_DIR=/realm/config \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_C_COMPILER=clang-18 \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DBoost_USE_STATIC_LIBS=ON -DAPPS_BUILD=all -DTOOLS_BUILD=db-only \
  -DSCRIPTS=static -DMODULES=static -DWITH_WARNINGS=ON -DBUILD_TESTING=ON -DINSTALL_GTEST=OFF \
  "${sanitize[@]}"
if [ "$#" -eq 0 ]; then
  set -- worldserver authserver dbimport
fi
cmake --build "$build" --parallel "${LA_BUILD_JOBS:-4}" --target "$@"
if [ "$install" = 1 ]; then
  cmake --install "$build" >/dev/null
fi
