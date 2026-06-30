# glib cross-compiled for Android (static). Core type/utility library required by
# the pango/harfbuzz half of the weston toytoolkit stack on Android (NDK).
#
# Depends on libffi-android + pcre2-android (PKG_CONFIG_PATH); zlib from the NDK
# sysroot. glib builds codegen helpers on the BUILD machine, so a native-file
# points meson at the build-host compiler and needs_exe_wrapper=true stops meson
# from trying to run Android host binaries during configure.
{
  lib,
  pkgs,
  buildPackages,
  common,
  buildModule,
  androidToolchain ? (import ../../toolchains/android.nix { inherit lib pkgs; }),
  ...
}:

let
  src = pkgs.glib.src;
  libffi = buildModule.buildForAndroid "libffi" { };
  pcre2 = buildModule.buildForAndroid "pcre2" { };
  libintl = buildModule.buildForAndroid "libintl" { };
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
  name = "glib-android";
  inherit src;

  # glib's meson build execs bundled codegen scripts (e.g.
  # tools/gen-visibility-macros.py) via their `#!/usr/bin/env python3` shebang.
  # The Android cross build is fully sandboxed, so on the macOS builder
  # /usr/bin/env is outside the sandbox and exec is denied (EPERM). Rewrite the
  # shebangs to the buildPackages python3 before configure.
  postPatch = ''
    patchShebangs .
  '';

  # The installed glib codegen tools (glib-mkenums, glib-genmarshal,
  # gdbus-codegen) keep their `#!/usr/bin/env python3` shebang; the cross-build
  # fixup leaves output shebangs alone. Downstream meson builds (pango,
  # harfbuzz) resolve glib-mkenums from this package's pkg-config and exec it on
  # the build machine, so /usr/bin/env is denied in the sandbox. Patch the
  # installed tools to the buildPackages python3 (they are pure-python codegen).
  postInstall = ''
    patchShebangs --build $out/bin
  '';

  nativeBuildInputs = with buildPackages; [
    meson
    ninja
    pkg-config
    python3
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

    [properties]
    needs_exe_wrapper = true
    pkg_config_libdir = '${libintl}/lib/pkgconfig:${libffi}/lib/pkgconfig:${pcre2}/lib/pkgconfig'

    [built-in options]
    c_args = ['-fPIC', '-I${libintl}/include']
    cpp_args = ['-fPIC', '-I${libintl}/include']
    c_link_args = ['-L${libintl}/lib', '-lintl']
    cpp_link_args = ['-L${libintl}/lib', '-lintl']
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
    export PKG_CONFIG_PATH="${libintl}/lib/pkgconfig:${libffi}/lib/pkgconfig:${pcre2}/lib/pkgconfig:''${PKG_CONFIG_PATH:-}"
    meson setup build \
      --prefix=$out \
      --libdir=$out/lib \
      --native-file=native-file.txt \
      --cross-file=android-cross-file.txt \
      --pkg-config-path="${libintl}/lib/pkgconfig:${libffi}/lib/pkgconfig:${pcre2}/lib/pkgconfig" \
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
