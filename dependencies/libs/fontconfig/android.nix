# fontconfig cross-compiled for Android (static). Font discovery/matching for the
# weston toytoolkit cairo/pango stack on Android (NDK).
#
# Depends on freetype-android + expat-android (PKG_CONFIG_PATH). fontconfig runs
# fc-case/fc-lang/fc-glyphname codegen on the BUILD machine, so a native-file
# points meson at the build-host compiler. gperf is needed for fcobjshash.
{
  lib,
  pkgs,
  buildPackages,
  common,
  buildModule,
  androidToolchain ? (import ../../toolchains/android.nix { inherit lib pkgs; }),
  androidMesonSandbox ? (import ../../toolchains/android-meson-sandbox.nix { inherit lib; }),
  ...
}:

let
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
  freetype = buildModule.buildForAndroid "freetype" { };
  expat = buildModule.buildForAndroid "expat" { };
  buildFlags = [
    "-Ddoc=disabled"
    "-Dnls=disabled"
    "-Dtests=disabled"
    "-Dtools=disabled"
    "-Dcache-build=disabled"
  ];
in
pkgs.stdenv.mkDerivation (androidMesonSandbox.apply {
  name = "fontconfig-android";
  inherit src;

  nativeBuildInputs = with buildPackages; [
    meson
    ninja
    pkg-config
    python3
    gperf
    stdenv.cc
  ];
  buildInputs = [ ];

  preConfigure = ''
    cat > android-cross-file.txt <<EOF
    [binaries]
    c = '${androidToolchain.androidCC}'
    cpp = '${androidToolchain.androidCXX}'
    ar = '${androidToolchain.androidAR}'
    strip = '${androidToolchain.androidSTRIP}'
    pkgconfig = '${buildPackages.pkg-config}/bin/pkg-config'

    [host_machine]
    system = 'android'
    cpu_family = 'aarch64'
    cpu = 'aarch64'
    endian = 'little'

    [built-in options]
    c_args = ['-fPIC']
    cpp_args = ['-fPIC']
    c_link_args = []
    cpp_link_args = []
    EOF

    cat > native-file.txt <<EOF
    [binaries]
    c = '${buildPackages.stdenv.cc}/bin/cc'
    cpp = '${buildPackages.stdenv.cc}/bin/c++'
    ar = '${buildPackages.stdenv.cc}/bin/ar'
    strip = 'strip'
    pkg-config = '${buildPackages.pkg-config}/bin/pkg-config'
    EOF
  '';

  configurePhase = ''
    runHook preConfigure
    export PKG_CONFIG_PATH="${freetype}/lib/pkgconfig:${expat}/lib/pkgconfig:''${PKG_CONFIG_PATH:-}"
    meson setup build \
      --prefix=$out \
      --libdir=$out/lib \
      --native-file=native-file.txt \
      --cross-file=android-cross-file.txt \
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
})
