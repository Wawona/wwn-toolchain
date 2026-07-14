# fcft - font loading / glyph rasterization (fuzzel dependency) for Android.
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
  fetchSource = common.fetchSource;
  fcftSource = {
    source = "codeberg";
    owner = "dnkl";
    repo = "fcft";
    tag = "3.3.3";
    sha256 = "sha256-MkGlph9WpqH4daov5ZZPO2ua2mUbrsuo8Xk6GoKhoxg=";
  };
  src = fetchSource fcftSource;

  freetype = buildModule.buildForAndroid "freetype" { };
  fontconfig = buildModule.buildForAndroid "fontconfig" { };
  pixman = buildModule.buildForAndroid "pixman" { };
  tllist = buildModule.buildForAndroid "tllist" { };
  utf8proc = buildModule.buildForAndroid "utf8proc" { };
  expat = buildModule.buildForAndroid "expat" { };
  pcPath = lib.concatMapStringsSep ":" (d: "${d}/lib/pkgconfig") [
    freetype
    fontconfig
    pixman
    tllist
    utf8proc
    expat
  ];
in
pkgs.stdenv.mkDerivation (androidMesonSandbox.apply {
  pname = "fcft";
  version = "3.3.3";
  inherit src;

  nativeBuildInputs = with buildPackages; [
    meson
    ninja
    pkg-config
    scdoc
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

  dontUseMesonConfigure = true;

  configurePhase = ''
    runHook preConfigure
    export PKG_CONFIG_PATH="${pcPath}:''${PKG_CONFIG_PATH:-}"
    export PKG_CONFIG_ALLOW_CROSS=1
    meson setup build \
      --prefix=$out \
      --libdir=$out/lib \
      --native-file=native-file.txt \
      --cross-file=android-cross-file.txt \
      --buildtype=release \
      -Ddefault_library=static \
      -Ddocs=disabled \
      -Dtest-text-shaping=false \
      -Dgrapheme-shaping=disabled \
      -Drun-shaping=disabled
    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    export PKG_CONFIG_PATH="${pcPath}:''${PKG_CONFIG_PATH:-}"
    meson compile -C build
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    meson install -C build
    runHook postInstall
  '';

  meta = with lib; {
    description = "Font loading and glyph rasterization for Android (fuzzel)";
    homepage = "https://codeberg.org/dnkl/fcft";
    license = licenses.mit;
  };
})
