# ANGLE for Apple mobile — static .a archives (Metal backend, App Store–safe).
# Prebuilt (default): XCSoar static libs force-loaded into the app binary
# (ILAND_ANGLE_STATIC — no dlopen, no Frameworks/libEGL.dylib).
# GN cross-build (usePrebuilt=false) is the from-source fallback.
{
  lib,
  pkgs,
  buildPackages,
  common ? null,
  buildModule ? null,
  simulator ? false,
  iosToolchain ? null,
  usePrebuilt ? true,
}:

let
  isVisionOS = iosToolchain.isVisionOSToolchain or false;
  sdkPlatform =
    if isVisionOS then
      if simulator then "XRSimulator" else "XROS"
    else if simulator then "iPhoneSimulator" else "iPhoneOS";
  minFlag =
    if isVisionOS then
      # xrOS deployment is encoded in the clang target triple; unlike iOS,
      # Apple clang has no -mvisionos[-simulator]-version-min spelling.
      ""
    else if simulator then
      "-mios-simulator-version-min=${iosToolchain.deploymentTarget}"
    else
      "-miphoneos-version-min=${iosToolchain.deploymentTarget}";
  packageSuffix =
    if isVisionOS then
      if simulator then "visionos-simulator" else "visionos"
    else if simulator then "ios-simulator" else "ios";
in
if usePrebuilt && !isVisionOS && simulator then
  let
    sources = import ./prebuilt-sources.nix { inherit lib pkgs; };
    deviceHeaders = pkgs.fetchurl {
      url = sources.iosArm64.url;
      hash = sources.iosArm64.hash;
    };
  in
  pkgs.stdenv.mkDerivation {
    pname = "angle-ios-simulator";
    version = sources.iosUniversal.version;

    src = pkgs.fetchurl {
      url = sources.iosUniversal.url;
      hash = sources.iosUniversal.hash;
    };

    nativeBuildInputs = [ pkgs.unzip pkgs.gnutar ];
    dontConfigure = true;
    dontBuild = true;
    unpackPhase = "true";
    sourceRoot = ".";

    installPhase = ''
      runHook preInstall
      mkdir -p $out/lib $out/include $out/nix-support
      unzip -q $src -d "$TMPDIR/angle-universal"
      root="$TMPDIR/angle-universal"
      install -m644 "$root/${sources.iosUniversal.simEgl}" $out/lib/libEGL.dylib
      install -m644 "$root/${sources.iosUniversal.simGles}" $out/lib/libGLESv2.dylib
      tar -xzf ${deviceHeaders} -C "$TMPDIR"
      cp -r "$TMPDIR/${sources.iosArm64.unpackDir}/include/"* $out/include/
      echo dylib > $out/nix-support/link-kind
      runHook postInstall
    '';

    meta = with lib; {
      description = "ANGLE OpenGL ES for iOS Simulator (prebuilt dylibs from universal XCFramework)";
      homepage = "https://angleproject.org";
      license = licenses.bsd3;
      platforms = platforms.darwin;
    };
  }
else if usePrebuilt && !isVisionOS && !simulator then
  let
    sources = import ./prebuilt-sources.nix { inherit lib pkgs; };
    deviceHeaders = pkgs.fetchurl {
      url = sources.iosArm64.url;
      hash = sources.iosArm64.hash;
    };
  in
  pkgs.stdenv.mkDerivation {
    pname = "angle-ios";
    version = sources.iosUniversal.version;

    src = pkgs.fetchurl {
      url = sources.iosUniversal.url;
      hash = sources.iosUniversal.hash;
    };

    nativeBuildInputs = [ pkgs.unzip pkgs.gnutar ];
    dontConfigure = true;
    dontBuild = true;
    unpackPhase = "true";
    sourceRoot = ".";

    installPhase = ''
      runHook preInstall
      mkdir -p $out/lib $out/include $out/nix-support
      unzip -q $src -d "$TMPDIR/angle-universal"
      root="$TMPDIR/angle-universal"
      install -m644 "$root/${sources.iosUniversal.deviceEgl}" $out/lib/libEGL.dylib
      install -m644 "$root/${sources.iosUniversal.deviceGles}" $out/lib/libGLESv2.dylib
      tar -xzf ${deviceHeaders} -C "$TMPDIR"
      cp -r "$TMPDIR/${sources.iosArm64.unpackDir}/include/"* $out/include/
      # Dylibs (not force_load .a) avoid an iOS 26 / Xcode 26 linker bug where
      # force_load libkmscube.a + force_load libGLESv2.a fails to resolve libc++ for
      # ANGLE when WWNIlandPresenter.o is in the link unit.
      echo dylib > $out/nix-support/link-kind
      runHook postInstall
    '';

    meta = with lib; {
      description = "ANGLE OpenGL ES for iOS device (prebuilt dylibs from universal XCFramework, Metal, App Store–safe)";
      homepage = "https://angleproject.org";
      license = licenses.bsd3;
      platforms = platforms.darwin;
    };
  }
else
  let
    xcodeUtils = import ../../utils/xcode-wrapper.nix { inherit lib pkgs; };
  in
  import ./cross-base.nix {
    inherit lib pkgs buildPackages;
    pname = "angle-${packageSuffix}";
    clangBasePath = if isVisionOS then "xcode-clang" else null;

    gnExtraFlags = [
      "target_os=\"ios\""
      "target_cpu=\"arm64\""
      "target_environment=\"${if simulator then "simulator" else "device"}\""
    ] ++ lib.optionals isVisionOS [
      # Retain Chromium's supported iPhone platform branches and override only
      # the selected SDK below; target_os has no native visionOS value yet.
      "angle_visionos=true"
    ] ++ [
      "angle_enable_metal=true"
      "angle_enable_vulkan=false"
      "ios_deployment_target=\"${iosToolchain.deploymentTarget}\""
      "ios_enable_code_signing=false"
      "use_custom_libcxx=false"
    ];

    preConfigureHook = ''
      unset DEVELOPER_DIR
      if [ -z "''${XCODE_APP:-}" ]; then
        XCODE_APP=$(${xcodeUtils.findXcodeScript}/bin/find-xcode || true)
        [ -n "$XCODE_APP" ] && export DEVELOPER_DIR="$XCODE_APP/Contents/Developer"
      fi
      export SDKROOT="$DEVELOPER_DIR/Platforms/${sdkPlatform}.platform/Developer/SDKs/${sdkPlatform}.sdk"
      export PATH="/usr/bin:/usr/sbin:$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin:$PATH"
      export CC="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
      export CXX="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++"
      export AR="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/ar"
      export CFLAGS="-arch arm64 -isysroot $SDKROOT ${minFlag}"
      export CXXFLAGS="$CFLAGS"
      export LDFLAGS="-arch arm64 -isysroot $SDKROOT ${minFlag}"
      ${lib.optionalString isVisionOS ''
        rm -rf xcode-clang
        ln -s "$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr" xcode-clang
        python3 -c 'from pathlib import Path; import re; p = Path("build/config/apple/mobile_config.gni"); s = p.read_text(); p.write_text(s if "\"xros\"" in s else re.sub(r"(?m)^(\s*)\"tvos\",\s*$", "\\1\"tvos\",\n\\1\"xros\",", s, count=1))'
        python3 -c 'from pathlib import Path; p = Path("build/config/ios/ios_sdk.gni"); s = p.read_text(); p.write_text(s.replace("declare_args() {", "declare_args() {\n  angle_visionos = false", 1))'
        python3 -c 'from pathlib import Path; p = Path("build/config/ios/ios_sdk.gni"); s = p.read_text(); m = "if (target_platform == \"iphoneos\") {"; r = """if (angle_visionos) {\n  if (target_environment == \"simulator\") {\n    ios_sdk_name = \"xrsimulator\"\n    ios_sdk_platform = \"XRSimulator\"\n  } else if (target_environment == \"device\") {\n    ios_sdk_name = \"xros\"\n    ios_sdk_platform = \"XROS\"\n  } else {\n    assert(false, \"unsupported target_environment=$target_environment\")\n  }\n} else if (target_platform == \"iphoneos\") {"""; p.write_text(s.replace(m, r, 1))'
        python3 -c 'from pathlib import Path; p = Path("build/config/rust.gni"); s = p.read_text(); m = "} else if (target_platform == \"tvos\") {"; r = """} else if (target_platform == \"xros\") {\n  if (target_environment == \"simulator\") {\n    rust_abi_target = \"aarch64-apple-visionos-sim\"\n  } else if (target_environment == \"device\") {\n    rust_abi_target = \"aarch64-apple-visionos\"\n  } else {\n    assert(false, \"unsupported target_environment=$target_environment\")\n  }\n} else if (target_platform == \"tvos\") {"""; p.write_text(s if "target_platform == \"xros\"" in s else s.replace(m, r, 1))'
        python3 -c 'from pathlib import Path; p = Path("build/config/apple/sdk_info.py"); s = p.read_text(); q = chr(39); p.write_text(s if "xrsimulator" in s else s.replace("watchsimulator", "watchsimulator" + q + ",\n        " + q + "xros" + q + ",\n        " + q + "xrsimulator", 1))'
        python3 -c 'from pathlib import Path; p = Path("build/config/ios/BUILD.gn"); s = p.read_text(); p.write_text(s.replace("apple-ios", "apple-xros"))'
      ''}
    '';

    installHook = ''
      OUT_DIR=$(find . -maxdepth 3 -type d -name 'Release*' | head -n1)
      [ -n "$OUT_DIR" ] || OUT_DIR=out
      mkdir -p $out/lib $out/include $out/nix-support
      shopt -s nullglob
      for lib in "$OUT_DIR"/*.a "$OUT_DIR"/obj/lib*.a "$OUT_DIR"/lib*.a; do
        base=$(basename "$lib")
        case "$base" in
          libEGL.a|libGLESv2.a|libGLESv1_CM.a|libangle_common.a) install -m644 "$lib" $out/lib/ ;;
        esac
      done
      for lib in $(find "$OUT_DIR" -name 'libEGL.a' -o -name 'libGLESv2.a' 2>/dev/null); do
        install -m644 "$lib" $out/lib/
      done
      if [ ! -f $out/lib/libEGL.a ] || [ ! -f $out/lib/libGLESv2.a ]; then
        echo "ERROR: expected libEGL.a and libGLESv2.a in ANGLE iOS build output" >&2
        find "$OUT_DIR" -name '*.a' | head -20 >&2 || true
        exit 1
      fi
      ${pkgs.bash}/bin/bash ${./rename-angle-symbols.sh} $out/lib/libEGL.a $out/lib/libEGL.a
      cp -rv ../../include/EGL ../../include/GLES2 ../../include/GLES3 ../../include/KHR $out/include/
      echo static > $out/nix-support/link-kind
    '';
  }
