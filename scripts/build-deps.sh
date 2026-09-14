#!/usr/bin/env bash
# Build-time MSYS2 tools only; the delivered application uses the Windows MSVCRT.
set -euo pipefail
arch=${1:?usage: build-deps.sh x64|x86}
case "$arch" in x64) target=mingw64; host=x86_64-w64-mingw32;; x86) target=mingw; host=i686-w64-mingw32;; *) exit 2;; esac
root=$(cd "$(dirname "$0")/.." && pwd)
tool="$root/.tools/$arch/w64devkit/bin"
prefix="$root/.deps/$arch"
prefix_win=$(cygpath -m "$prefix")
src="$root/.deps/sources"
work="$root/build/deps-$arch"
export PATH="/usr/bin:$tool:/ucrt64/bin:$PATH"
export CC="$tool/gcc.exe" CXX="$tool/g++.exe" AR="$tool/ar.exe" RANLIB="$tool/ranlib.exe" WINDRES="$tool/windres.exe"
export CFLAGS='-O2 -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -ffunction-sections -fdata-sections'
export CXXFLAGS="$CFLAGS"
export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
export PKG_CONFIG='/usr/bin/pkg-config --static'
mkdir -p "$prefix" "$work"
jobs=${BUILD_JOBS:-6}

if [[ ! -f "$prefix/lib/libssl.a" ]]; then
    mkdir -p "$work/openssl"
    cd "$work/openssl"
    perl "$src/openssl-3.5.8/Configure" "$target" \
        --prefix="$prefix_win" --openssldir="$prefix_win/ssl" --libdir=lib \
        no-shared no-module no-tests no-docs no-autoload-config no-comp no-legacy no-engine no-dso \
        -D_WIN32_WINNT=0x0601 -DWINVER=0x0601
    make -j"$jobs" build_sw
    make install_sw
fi

cmake_bin=/ucrt64/bin/cmake.exe
ninja_bin=$(cygpath -m /ucrt64/bin/ninja.exe)
cc_win=$(cygpath -m "$CC")
if [[ ! -f "$prefix/lib/libz.a" ]]; then
    "$cmake_bin" -S "$(cygpath -m "$src/zlib-1.3.2")" -B "$(cygpath -m "$work/zlib")" -G Ninja \
        -DCMAKE_C_COMPILER="$cc_win" -DCMAKE_MAKE_PROGRAM="$ninja_bin" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix_win" \
        -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_STATIC=ON -DZLIB_BUILD_TESTING=OFF
    "$cmake_bin" --build "$(cygpath -m "$work/zlib")" --parallel "$jobs"
    "$cmake_bin" --install "$(cygpath -m "$work/zlib")"
    if [[ -f "$prefix/lib/libzs.a" ]]; then cp "$prefix/lib/libzs.a" "$prefix/lib/libz.a"; fi
fi
if [[ ! -f "$prefix/lib/libxml2.a" ]]; then
    "$cmake_bin" -S "$(cygpath -m "$src/libxml2-2.15.3")" -B "$(cygpath -m "$work/libxml2")" -G Ninja \
        -DCMAKE_C_COMPILER="$cc_win" -DCMAKE_MAKE_PROGRAM="$ninja_bin" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix_win" \
        -DCMAKE_C_FLAGS="$CFLAGS" -DBUILD_SHARED_LIBS=OFF \
        -DLIBXML2_WITH_ICONV=OFF -DLIBXML2_WITH_ICU=OFF -DLIBXML2_WITH_ZLIB=OFF \
        -DLIBXML2_WITH_MODULES=OFF -DLIBXML2_WITH_PYTHON=OFF \
        -DLIBXML2_WITH_PROGRAMS=OFF -DLIBXML2_WITH_TESTS=OFF -DLIBXML2_WITH_DOCS=OFF
    "$cmake_bin" --build "$(cygpath -m "$work/libxml2")" --parallel "$jobs"
    "$cmake_bin" --install "$(cygpath -m "$work/libxml2")"
fi

mkdir -p "$work/openconnect"
cd "$work/openconnect"
if [[ ! -f Makefile ]]; then
    CPPFLAGS="-DLIBXML_STATIC -I$prefix_win/include" \
    LDFLAGS="-L$prefix_win/lib -static -Wl,--gc-sections" \
    "$src/openconnect-9.21/configure" --host="$host" --prefix="$prefix_win" \
        --disable-shared --enable-static --disable-nls --disable-nsis-installer \
        --disable-dependency-tracking --disable-maintainer-mode \
        --with-openssl --without-gnutls --without-libproxy --without-stoken \
        --without-libpskc --without-libpcsclite --without-gssapi --without-lz4 \
        --without-gnutls-tss2 --without-java --with-builtin-json --with-vpnc-script=vpnc-script-win.js
fi
make -j"$jobs" libopenconnect.la
cp .libs/libopenconnect.a "$prefix/lib/"
cp "$src/openconnect-9.21/openconnect.h" "$prefix/include/"
cp "$src/openconnect-9.21/COPYING.LGPL" "$prefix/"
printf 'Built native %s libraries in %s\n' "$arch" "$prefix_win"
