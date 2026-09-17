#!/usr/bin/env bash
# The same pinned sources build with Linux MinGW cross compilers or MSYS2 + w64devkit.
set -euo pipefail
arch=${1:?Usage: build-dependencies.sh x64|x86}
case "$arch" in
    x64) host=x86_64-w64-mingw32 ;;
    x86) host=i686-w64-mingw32 ;;
    *) echo 'Architecture must be x64 or x86.' >&2; exit 2 ;;
esac
root=$(cd "$(dirname "$0")/.." && pwd)
prefix="$root/.deps/$arch"
sources="$root/.deps/sources"
jobs=${BUILD_JOBS:-auto}
if [[ "$jobs" == auto ]]; then jobs=$(getconf _NPROCESSORS_ONLN); fi
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'BUILD_JOBS must be auto or a positive integer.' >&2; exit 2; }
export CONFIG_SHELL=/bin/bash
case "$(uname -s)" in
    MINGW*|MSYS*)
        compiler="$root/.tools/$arch/w64devkit/bin"
        export PATH="/usr/bin:$compiler:/ucrt64/bin:$PATH"
        export CC="$compiler/gcc.exe" CXX="$compiler/g++.exe" AR="$compiler/ar.exe"
        export RANLIB="$compiler/ranlib.exe" WINDRES="$compiler/windres.exe"
        prefix_native=$(cygpath -m "$prefix")
        cmake_cc=$(cygpath -m "$CC")
        cmake_cxx=$(cygpath -m "$CXX")
        cmake_rc=$(cygpath -m "$WINDRES")
        cmake=cmake.exe
        ;;
    *)
        export CC="$host-gcc-posix" CXX="$host-g++-posix" AR="$host-ar"
        export RANLIB="$host-ranlib" WINDRES="$host-windres"
        prefix_native="$prefix"
        cmake_cc="$CC"
        cmake_cxx="$CXX"
        cmake_rc="$WINDRES"
        cmake=cmake
        ;;
esac
for command in "$CC" "$CXX" "$AR" "$RANLIB" "$WINDRES" "$cmake" ninja pkg-config make python3 patch; do
    command -v "$command" >/dev/null || { echo "Missing build tool: $command" >&2; exit 1; }
done
python3 "$root/scripts/fetch-sources.py"
fingerprint=$({
    printf '%s\n' "$arch" "$prefix_native"
    "$CC" --version
    cat "$root/scripts/sources.json" "$0" "$root/scripts/build_common.py" \
        "$root/scripts/fetch-sources.py" "$root/scripts/source-directory.py" "$root"/client/patches/*.patch
} | sha256sum | cut -d' ' -f1)
work="$root/build/dependencies-$arch/$fingerprint"
stamp="$prefix/.native-dependencies.sha256"
complete=true
for library in openconnect stoken gnutls xml2 z hogweed nettle gmp; do
    [[ -s "$prefix/lib/lib$library.a" ]] || complete=false
done
[[ -s "$prefix/toolchain.cmake" && -s "$prefix/include/openconnect.h" ]] || complete=false
if [[ "$complete" == true && -f "$stamp" && "$(cat "$stamp")" == "$fingerprint" ]]; then
    echo "Verified dependency build already exists: $arch"
    exit 0
fi
mkdir -p "$prefix/lib" "$prefix/include" "$work"
export CFLAGS='-O2 -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -ffunction-sections -fdata-sections'
export CXXFLAGS="$CFLAGS"
export CPPFLAGS="-I$prefix_native/include -DGNUTLS_STATIC -DLIBXML_STATIC"
export LDFLAGS="-L$prefix_native/lib -static -static-libgcc"
export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig:$prefix/share/pkgconfig"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
export PKG_CONFIG='pkg-config --static'
toolchain="$work/mingw.cmake"
cat > "$toolchain" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER "$cmake_cc")
set(CMAKE_CXX_COMPILER "$cmake_cxx")
set(CMAKE_RC_COMPILER "$cmake_rc")
set(CMAKE_FIND_ROOT_PATH "$prefix_native")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF
source_for() { printf '%s/%s' "$sources" "$(python3 "$root/scripts/source-directory.py" "$1")"; }
autobuild() {
    local name=$1; shift
    local source
    source=$(source_for "$name")
    mkdir -p "$work/$name"
    (
        cd "$work/$name"
        "$CONFIG_SHELL" "$source/configure" --host="$host" --prefix="$prefix_native" \
            --disable-shared --enable-static --disable-dependency-tracking "$@"
        make -j"$jobs"
        make install
    )
}
echo "Building $arch dependencies with $jobs parallel jobs."
autobuild gmp --disable-cxx
autobuild nettle --disable-documentation --disable-openssl
"$cmake" -S "$(source_for zlib)" -B "$work/zlib" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix_native" \
    -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_STATIC=ON -DZLIB_BUILD_TESTING=OFF
"$cmake" --build "$work/zlib" --parallel "$jobs"
"$cmake" --install "$work/zlib"
if [[ -f "$prefix/lib/libzs.a" ]]; then cp "$prefix/lib/libzs.a" "$prefix/lib/libz.a"; fi
"$cmake" -S "$(source_for libxml2)" -B "$work/libxml2" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix_native" \
    -DCMAKE_C_FLAGS="$CFLAGS" -DBUILD_SHARED_LIBS=OFF \
    -DLIBXML2_WITH_ICONV=OFF -DLIBXML2_WITH_ICU=OFF -DLIBXML2_WITH_ZLIB=OFF \
    -DLIBXML2_WITH_MODULES=OFF -DLIBXML2_WITH_PYTHON=OFF \
    -DLIBXML2_WITH_PROGRAMS=OFF -DLIBXML2_WITH_TESTS=OFF -DLIBXML2_WITH_DOCS=OFF
"$cmake" --build "$work/libxml2" --parallel "$jobs"
"$cmake" --install "$work/libxml2"
autobuild gnutls --disable-cxx --disable-doc --disable-tools --disable-tests --disable-nls --disable-gcc-warnings \
    --disable-guile --without-p11-kit --without-tpm --without-tpm2 --without-idn \
    --with-included-libtasn1 --with-included-unistring --without-brotli --without-zstd
autobuild stoken --without-gtk --without-tomcrypt --with-nettle
original=$(source_for openconnect)
# Apply the small, documented Windows error-propagation fixes to a build-local copy.
if [[ ! -d "$work/openconnect-source" ]]; then
    cp -a "$original" "$work/openconnect-source"
    for patchfile in "$root"/client/patches/*.patch; do
        patch --batch --forward --fuzz=0 -d "$work/openconnect-source" -p1 < "$patchfile"
    done
fi
mkdir -p "$work/openconnect"
(
    cd "$work/openconnect"
    "$CONFIG_SHELL" "$work/openconnect-source/configure" --host="$host" --prefix="$prefix_native" \
        --disable-shared --enable-static --disable-nls --disable-nsis-installer \
        --disable-dependency-tracking --disable-maintainer-mode \
        --with-gnutls --without-openssl --without-libproxy --with-stoken \
        --without-libpskc --without-libpcsclite --without-gssapi --without-lz4 \
        --without-gnutls-tss2 --without-java --with-builtin-json --with-vpnc-script=vpnc-script-win.js
    make -j"$jobs" libopenconnect.la
    cp .libs/libopenconnect.a "$prefix/lib/"
    cp "$original/openconnect.h" "$prefix/include/"
)
cp "$toolchain" "$prefix/toolchain.cmake"
printf '%s\n' "$fingerprint" > "$stamp"
echo "Static official OpenConnect / GnuTLS dependencies ready: $arch"
