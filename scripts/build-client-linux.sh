#!/usr/bin/env bash
# Cross-compile the Win7+ MSVCRT client on a standard Ubuntu runner.
set -euo pipefail
arch=${1:?Usage: build-client-linux.sh x64|x86}
case "$arch" in
    x64) host=x86_64-w64-mingw32; openssl_target=mingw64 ;;
    x86) host=i686-w64-mingw32; openssl_target=mingw ;;
    *) printf '%s\n' 'Architecture must be x64 or x86.' >&2; exit 2 ;;
esac
root=$(cd "$(dirname "$0")/.." && pwd)
prefix="$root/.deps/$arch"
src="$root/.deps/sources"
work="$root/build/linux-$arch"
jobs=${BUILD_JOBS:-2}
mkdir -p "$prefix" "$work"
python3 "$root/scripts/fetch-sources.py"
openssl_src="$src/$(python3 "$root/scripts/source-directory.py" openssl)"
zlib_src="$src/$(python3 "$root/scripts/source-directory.py" zlib)"
libxml_src="$src/$(python3 "$root/scripts/source-directory.py" libxml2)"
openconnect_src="$src/$(python3 "$root/scripts/source-directory.py" openconnect)"

export CC="$host-gcc-posix" CXX="$host-g++-posix" AR="$host-ar" RANLIB="$host-ranlib" RC="$host-windres" WINDRES="$host-windres"
for command in "$CC" "$CXX" "$AR" "$RC" cmake ninja perl pkg-config make; do
    command -v "$command" >/dev/null || { printf 'Missing build tool: %s\n' "$command" >&2; exit 1; }
done
export CFLAGS='-O2 -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -ffunction-sections -fdata-sections'
export CXXFLAGS="$CFLAGS"
export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
export PKG_CONFIG='pkg-config --static'
toolchain="$work/mingw.cmake"
cat > "$toolchain" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER $CC)
set(CMAKE_CXX_COMPILER $CXX)
set(CMAKE_RC_COMPILER $RC)
set(CMAKE_FIND_ROOT_PATH "$prefix" "/usr/$host")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF

mkdir -p "$work/openssl"
(
    cd "$work/openssl"
    perl "$openssl_src/Configure" "$openssl_target" \
        --prefix="$prefix" --openssldir="$prefix/ssl" --libdir=lib \
        no-shared no-module no-tests no-docs no-autoload-config no-comp no-legacy no-engine no-dso \
        -D_WIN32_WINNT=0x0601 -DWINVER=0x0601
    make -j"$jobs" build_sw
    make install_sw
)
cmake -S "$zlib_src" -B "$work/zlib" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_STATIC=ON -DZLIB_BUILD_TESTING=OFF
cmake --build "$work/zlib" --parallel "$jobs"
cmake --install "$work/zlib"
if [[ -f "$prefix/lib/libzs.a" ]]; then cp "$prefix/lib/libzs.a" "$prefix/lib/libz.a"; fi
cmake -S "$libxml_src" -B "$work/libxml2" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_C_FLAGS="$CFLAGS" -DBUILD_SHARED_LIBS=OFF \
    -DLIBXML2_WITH_ICONV=OFF -DLIBXML2_WITH_ICU=OFF -DLIBXML2_WITH_ZLIB=OFF \
    -DLIBXML2_WITH_MODULES=OFF -DLIBXML2_WITH_PYTHON=OFF \
    -DLIBXML2_WITH_PROGRAMS=OFF -DLIBXML2_WITH_TESTS=OFF -DLIBXML2_WITH_DOCS=OFF
cmake --build "$work/libxml2" --parallel "$jobs"
cmake --install "$work/libxml2"
mkdir -p "$work/openconnect"
(
    cd "$work/openconnect"
    CPPFLAGS="-DLIBXML_STATIC -I$prefix/include" LDFLAGS="-L$prefix/lib -static -Wl,--gc-sections" \
        "$openconnect_src/configure" --host="$host" --prefix="$prefix" \
        --disable-shared --enable-static --disable-nls --disable-nsis-installer \
        --disable-dependency-tracking --disable-maintainer-mode \
        --with-openssl --without-gnutls --without-libproxy --without-stoken \
        --without-libpskc --without-libpcsclite --without-gssapi --without-lz4 \
        --without-gnutls-tss2 --without-java --with-builtin-json --with-vpnc-script=vpnc-script-win.js
    make -j"$jobs" libopenconnect.la
    cp .libs/libopenconnect.a "$prefix/lib/"
    cp "$openconnect_src/openconnect.h" "$prefix/include/"
)
cmake -S "$root" -B "$work/client" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$work/client" --parallel "$jobs"
python3 "$root/scripts/package-client.py" --arch "$arch" --build "$work/client" --strip "$host-strip"
