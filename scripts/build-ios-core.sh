#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
[[ "$(uname -s)" == Darwin ]] || { echo 'iOS requires macOS and Xcode'; exit 2; }
python3 scripts/mobile-sources.py ios
root="$PWD"
work="$root/build/mobile/ios"
prefix="$work/prefix"
sdk="$(xcrun --sdk iphoneos --show-sdk-path)"
jobs="$(sysctl -n hw.ncpu)"
export CC="$(xcrun --sdk iphoneos --find clang)"
export AR="$(xcrun --find ar)"
export RANLIB="$(xcrun --find ranlib)"
export CFLAGS="-arch arm64 -isysroot $sdk -miphoneos-version-min=15.0 -O2"
export LDFLAGS="$CFLAGS"
export CPP="$CC -E $CFLAGS"
mkdir -p "$work/openssl-build"
cd "$work/openssl-build"
"$work/openssl-3.5.8/Configure" ios64-xcrun no-shared no-tests no-apps no-module no-async \
  --prefix="$prefix" --libdir=lib
make -j"$jobs" build_libs
make install_dev
cmake -S "$work/libxml2-2.15.3" -B "$work/xml-build" \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DLIBXML2_WITH_PROGRAMS=OFF -DLIBXML2_WITH_TESTS=OFF -DLIBXML2_WITH_PYTHON=OFF \
  -DLIBXML2_WITH_ICONV=OFF -DLIBXML2_WITH_ZLIB=OFF -DLIBXML2_WITH_LZMA=OFF
cmake --build "$work/xml-build" --parallel "$jobs" --target install
export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
export CPPFLAGS="-I$prefix/include -include $root/mobile/ios/ios-compat.h"
mkdir -p "$work/oc-build"
cd "$work/oc-build"
"$work/openconnect-9.21/configure" --host=aarch64-apple-darwin --prefix="$prefix" \
  --with-openssl --without-gnutls --disable-shared --enable-static --disable-nls \
  --without-stoken --without-libpcsclite --without-gssapi --without-libproxy \
  --without-libpskc --without-lz4 --without-java --with-vpnc-script=/disabled \
  OPENSSL_CFLAGS="-I$prefix/include" \
  OPENSSL_LIBS="$prefix/lib/libssl.a $prefix/lib/libcrypto.a" LIBS=-lz
make -j"$jobs" libopenconnect.la
cp .libs/libopenconnect.a "$prefix/lib/"
cp "$work/openconnect-9.21/openconnect.h" "$prefix/include/"
for library in openconnect ssl crypto xml2; do
  xcrun lipo "$prefix/lib/lib$library.a" -verify_arch arm64
done
