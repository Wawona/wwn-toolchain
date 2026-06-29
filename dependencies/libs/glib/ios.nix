# glib cross-compiled for iOS (static). Core type/utility library required by the
# pango/harfbuzz half of the weston toytoolkit stack on Apple platforms.
#
# Depends on libffi-ios (closures) + pcre2-ios (gregex), wired via PKG_CONFIG_PATH;
# zlib comes from the SDK. glib builds codegen helpers on the BUILD machine, so a
# native-file points meson at Xcode clang + macOS SDK and needs_exe_wrapper=true
# stops meson from trying to run iOS host binaries during configure.
{
  lib,
  pkgs,
  buildPackages,
  common,
  buildModule,
  simulator ? false,
  iosToolchain ? null,
}:

let
  xcodeUtils = import ../../../utils/xcode-wrapper.nix { inherit lib pkgs; };
  mesonSetup = import ../../toolchains/apple-mobile-meson.nix {
    inherit lib buildPackages xcodeUtils iosToolchain simulator;
  };
  src = pkgs.glib.src;
  libffi = buildModule.buildForIOS "libffi" { inherit simulator; };
  pcre2 = buildModule.buildForIOS "pcre2" { inherit simulator; };
  pkgConfigPaths = "${libffi}/lib/pkgconfig:${pcre2}/lib/pkgconfig";
  # glib falls back to proxy-libintl via meson wrapdb when no system intl exists.
  # Prefetch the wrap tarball so configure stays offline in the Nix sandbox.
  proxyLibintlSrc = pkgs.fetchurl {
    url = "https://github.com/mesonbuild/wrapdb/releases/download/proxy-libintl_0.5-1/proxy-libintl-0.5.tar.gz";
    hash = "sha256-96HL11ebqvV1xm+dmftilemwaEoosJWWfP2heFdZUwM=";
  };
  buildFlags = [
    "-Dtests=false"
    "-Dnls=disabled"
    "-Dlibmount=disabled"
    "-Dselinux=disabled"
    "-Dman-pages=disabled"
    "-Dintrospection=disabled"
    "-Ddtrace=false"
    "-Dsystemtap=false"
  ];
in
pkgs.stdenv.mkDerivation {
  name = "glib-ios";
  inherit src;

  __noChroot = true;

  nativeBuildInputs = with buildPackages; [
    meson
    ninja
    pkg-config
    python3
  ];
  buildInputs = [ ];

  preConfigure =
    mesonSetup.preConfigureShell {
      includePolyfills = true;
      needsExeWrapper = true;
    }
    + mesonSetup.nativeFileShell
    + ''
      # glib ships subprojects/proxy-libintl.wrap; extract the prefetched sources
      # so meson never hits wrapdb/GitHub in the Nix sandbox.
      mkdir -p subprojects
      tar -xzf ${proxyLibintlSrc} -C subprojects
    '';

  configurePhase = ''
    runHook preConfigure
    unset SDKROOT
    export PKG_CONFIG_PATH="${pkgConfigPaths}:''${PKG_CONFIG_PATH:-}"
    meson setup build \
      --prefix=$out \
      --libdir=$out/lib \
      --native-file=native-file.txt \
      --cross-file=ios-cross-file.txt \
      --wrap-mode=nodownload \
      --buildtype=release \
      -Ddefault_library=static \
      ${lib.concatMapStringsSep " " (flag: flag) buildFlags}
    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    meson compile -C build
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    meson install -C build
    runHook postInstall
  '';
}
