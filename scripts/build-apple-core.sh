#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
[[ "$(uname -s)" == Darwin ]] || { echo 'Apple clients require macOS and Xcode'; exit 2; }
platform=${1:?Usage: build-apple-core.sh ios|macos}
case "$platform" in
  ios) sdk_name=iphoneos; minimum=15.0; system=iOS; architectures=(arm64); folder=mobile ;;
  macos) sdk_name=macosx; minimum=13.0; system=Darwin; architectures=(arm64 x86_64); folder=desktop ;;
  *) echo 'Expected ios or macos'; exit 2 ;;
esac
python3 scripts/mobile-sources.py "$platform"
root="$PWD"
work="$root/build/$folder/$platform"
sdk="$(xcrun --sdk "$sdk_name" --show-sdk-path)"
jobs="$(sysctl -n hw.ncpu)"
export CC="$(xcrun --sdk "$sdk_name" --find clang)"
export AR="$(xcrun --find ar)"
export RANLIB="$(xcrun --find ranlib)"
for arch in "${architectures[@]}"; do
unset CPPFLAGS PKG_CONFIG_LIBDIR PKG_CONFIG_PATH
prefix="$work/$arch/prefix"
build="$work/$arch"
host=aarch64-apple-darwin
[[ "$arch" != x86_64 ]] || host=x86_64-apple-darwin
if [[ "$platform" == ios ]]; then
  target=ios64-xcrun; deployment="-miphoneos-version-min=$minimum"
else
  target="darwin64-$arch-cc"; deployment="-mmacosx-version-min=$minimum"
fi
export CFLAGS="-arch $arch -isysroot $sdk $deployment -Os"
export LDFLAGS="$CFLAGS"
export CPP="$CC -E $CFLAGS"
mkdir -p "$build/openssl-build"
cd "$build/openssl-build"
"$work/openssl-3.5.8/Configure" "$target" no-shared no-tests no-apps no-module no-async \
  --prefix="$prefix" --libdir=lib
make -j"$jobs" build_libs
make install_dev
cmake -S "$work/libxml2-2.15.3" -B "$build/xml-build" \
  -DCMAKE_SYSTEM_NAME="$system" -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_OSX_ARCHITECTURES="$arch" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$minimum" -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DLIBXML2_WITH_PROGRAMS=OFF -DLIBXML2_WITH_TESTS=OFF -DLIBXML2_WITH_PYTHON=OFF \
  -DLIBXML2_WITH_ICONV=OFF -DLIBXML2_WITH_ZLIB=OFF -DLIBXML2_WITH_LZMA=OFF
cmake --build "$build/xml-build" --parallel "$jobs" --target install
export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
export CPPFLAGS="-I$prefix/include -include $root/client/apple/extension-sandbox.h"
mkdir -p "$build/oc-build"
cd "$build/oc-build"
"$work/openconnect-9.21/configure" --host="$host" --prefix="$prefix" \
  --with-openssl --without-gnutls --disable-shared --enable-static --disable-nls \
  --without-stoken --without-libpcsclite --without-gssapi --without-libproxy \
  --without-libpskc --without-lz4 --without-java --with-vpnc-script=/disabled \
  OPENSSL_CFLAGS="-I$prefix/include" \
  OPENSSL_LIBS="-L$prefix/lib -lssl -lcrypto" LIBS=-lz \
  ac_cv_func_strchrnul=no CFLAGS="$CFLAGS -Werror=unguarded-availability"
make -j"$jobs" libopenconnect.la
cp .libs/libopenconnect.a "$prefix/lib/"
cp "$work/openconnect-9.21/openconnect.h" "$prefix/include/"
for library in openconnect ssl crypto xml2; do
  xcrun lipo "$prefix/lib/lib$library.a" -verify_arch "$arch"
  if xcrun ar -t "$prefix/lib/lib$library.a" | grep '\.a$'; then
    echo "Unexpected nested archive in lib$library.a" >&2
    exit 1
  fi
done
done
prefix="$work/prefix"
mkdir -p "$prefix/include" "$prefix/lib"
cp -R "$work/arm64/prefix/include/." "$prefix/include/"
for library in openconnect ssl crypto xml2; do
  inputs=()
  for arch in "${architectures[@]}"; do inputs+=("$work/$arch/prefix/lib/lib$library.a"); done
  xcrun lipo -create "${inputs[@]}" -output "$prefix/lib/lib$library.a"
  xcrun lipo "$prefix/lib/lib$library.a" -verify_arch "${architectures[@]}"
done
