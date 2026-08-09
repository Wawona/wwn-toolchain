# fontconfig cross-compiled for iOS (static). Font discovery/matching for the
# weston toytoolkit (clients/window.c) cairo/pango stack on Apple platforms.
#
# Depends on freetype-ios (rasterizer) + expat-ios (XML config parser), wired in
# via PKG_CONFIG_PATH. fontconfig runs codegen tools (fc-case/fc-lang/fc-glyphname)
# on the BUILD machine, so a native-file points meson at Xcode clang + the macOS
# SDK (same gotcha solved for fribidi). gperf is needed for fcobjshash.
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
  # Pin fontconfig to 2.17.1: fontconfig 2.18.0 raised its meson.build floor to
  # `meson_version : '>= 1.11.0'`, but nixpkgs (and hence this toolchain's
  # buildPackages.meson) is still 1.10.2. nixpkgs sidesteps the skew by building
  # fontconfig with autotools; our cross build uses meson, so hold at the last
  # 2.17.x (its meson floor is >= 1.6.1, satisfied by 1.10.2).
  fontconfigVersion = "2.17.1";
  src = pkgs.fetchurl {
    url = "https://gitlab.freedesktop.org/api/v4/projects/890/packages/generic/fontconfig/${fontconfigVersion}/fontconfig-${fontconfigVersion}.tar.xz";
    hash = "sha256-n1yuk/T//B+8Ba6ZzfxwjNYN/WYS/8BRKCcCXAJvpUE=";
  };
  freetype = buildModule.buildForIOS "freetype" { inherit simulator; };
  expat = buildModule.buildForIOS "expat" { inherit simulator; };
  buildFlags = [
    "-Ddoc=disabled"
    "-Dnls=disabled"
    "-Dtests=disabled"
    "-Dtools=disabled"
    "-Dcache-build=disabled"
  ];
in
pkgs.stdenv.mkDerivation {
  name = "fontconfig-ios";
  inherit src;

  __noChroot = true;

  nativeBuildInputs = with buildPackages; [
    meson
    ninja
    pkg-config
    python3
    gperf
  ];
  buildInputs = [ ];

  preConfigure =
    mesonSetup.preConfigureShell { }
    + mesonSetup.nativeFileShell;

  configurePhase = ''
    runHook preConfigure
    unset SDKROOT
    export PKG_CONFIG_PATH="${freetype}/lib/pkgconfig:${expat}/lib/pkgconfig:''${PKG_CONFIG_PATH:-}"
    meson setup build \
      --prefix=$out \
      --libdir=$out/lib \
      --native-file=native-file.txt \
      --cross-file=ios-cross-file.txt \
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
