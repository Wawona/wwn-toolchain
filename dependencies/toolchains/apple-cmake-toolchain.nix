# Shell snippet: write ios-toolchain.cmake after mkIOSBuildEnv has run.
# Call from preConfigure once APPLE_SDK_NAME / APPLE_LINKER_TARGET are set.
{ iosToolchain, simulator ? false }:

let
  platformInfo = import ./apple-mobile-platform.nix;
  mobile = platformInfo { inherit iosToolchain simulator; };
  isVisionOS = mobile.isVisionOS;
in
''
# Host nixpkgs/darwin often exports MACOSX_DEPLOYMENT_TARGET=14.0; clear it so
# Apple mobile CMake platforms (esp. visionOS) never inherit a macOS min version.
unset MACOSX_DEPLOYMENT_TARGET IPHONEOS_DEPLOYMENT_TARGET TVOS_DEPLOYMENT_TARGET WATCHOS_DEPLOYMENT_TARGET XROS_DEPLOYMENT_TARGET || true
cat > ios-toolchain.cmake <<EOF
set(CMAKE_SYSTEM_NAME ${mobile.cmakeSystemName})
set(CMAKE_OSX_ARCHITECTURES $IOS_ARCH)
set(CMAKE_OSX_DEPLOYMENT_TARGET ${mobile.minVersion})
set(CMAKE_C_COMPILER "$XCODE_CLANG")
set(CMAKE_CXX_COMPILER "$XCODE_CLANGXX")
${if isVisionOS then "" else ''
set(CMAKE_C_COMPILER_TARGET "$APPLE_LINKER_TARGET")
set(CMAKE_CXX_COMPILER_TARGET "$APPLE_LINKER_TARGET")
''}
set(CMAKE_SYSROOT "$SDKROOT")
set(CMAKE_OSX_SYSROOT "$SDKROOT")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_AR "$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/ar")
set(CMAKE_RANLIB "$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/ranlib")
EOF
if [[ "''${APPLE_SDK_NAME:-}" == xros ]] || [[ "''${APPLE_SDK_NAME:-}" == xrsimulator ]]; then
  # visionOS: let CMAKE_OSX_DEPLOYMENT_TARGET drive Apple-Clang's
  # `--target=<ARCH>-apple-xros<VERSION>` flag; do not also pass
  # -mvisionos-version-min (clang rejects that combo with an explicit -target).
  cat >> ios-toolchain.cmake <<EOF
set(CMAKE_C_FLAGS "-isysroot $SDKROOT -fPIC -Wno-unknown-warning-option")
set(CMAKE_CXX_FLAGS "-isysroot $SDKROOT -fPIC -Wno-unknown-warning-option")
set(CMAKE_EXE_LINKER_FLAGS "-isysroot $SDKROOT")
set(CMAKE_SHARED_LINKER_FLAGS "-isysroot $SDKROOT")
EOF
else
  cat >> ios-toolchain.cmake <<EOF
set(CMAKE_C_FLAGS "-arch $IOS_ARCH -target $APPLE_LINKER_TARGET -isysroot $SDKROOT $APPLE_DEPLOYMENT_FLAG -fPIC -Wno-unknown-warning-option")
set(CMAKE_CXX_FLAGS "-arch $IOS_ARCH -target $APPLE_LINKER_TARGET -isysroot $SDKROOT $APPLE_DEPLOYMENT_FLAG -fPIC -Wno-unknown-warning-option")
set(CMAKE_EXE_LINKER_FLAGS "-arch $IOS_ARCH -target $APPLE_LINKER_TARGET -isysroot $SDKROOT $APPLE_DEPLOYMENT_FLAG")
set(CMAKE_SHARED_LINKER_FLAGS "-arch $IOS_ARCH -target $APPLE_LINKER_TARGET -isysroot $SDKROOT $APPLE_DEPLOYMENT_FLAG")
EOF
fi
''
