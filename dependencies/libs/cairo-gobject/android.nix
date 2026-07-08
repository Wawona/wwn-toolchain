# cairo-gobject cross-compiled for Android (static). GObject type glue for
# cairo, required by Rust's cairo-sys-rs (pulled in by pango/pangocairo crates,
# e.g. the wwn-niri port). The main cairo recipe builds with -Dglib=disabled to
# stay on the proven minimal path, so this module compiles cairo's checked-in
# util/cairo-gobject sources (two C files) against cairo + glib directly.
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
  src = pkgs.cairo.src;
  version = pkgs.cairo.version;
  cairo = buildModule.buildForAndroid "cairo" { };
  glib = buildModule.buildForAndroid "glib" { };
  pcre2 = buildModule.buildForAndroid "pcre2" { };
  libffi = buildModule.buildForAndroid "libffi" { };
  pixman = buildModule.buildForAndroid "pixman" { };
  freetype = buildModule.buildForAndroid "freetype" { };
  fontconfig = buildModule.buildForAndroid "fontconfig" { };
  expat = buildModule.buildForAndroid "expat" { };
  libpng = buildModule.buildForAndroid "libpng" { };
  pcDeps = [ cairo glib pcre2 libffi pixman freetype fontconfig expat libpng ];
  pcPath = lib.concatMapStringsSep ":" (d: "${d}/lib/pkgconfig") pcDeps;
in
pkgs.stdenv.mkDerivation {
  name = "cairo-gobject-android";
  inherit src;

  __noChroot = true;

  nativeBuildInputs = with buildPackages; [ pkg-config ];
  buildInputs = [ ];

  buildPhase = ''
    runHook preBuild
    export PKG_CONFIG_PATH="${pcPath}:''${PKG_CONFIG_PATH:-}"
    CFLAGS_PC=$(pkg-config --cflags cairo glib-2.0 gobject-2.0)
    cd util/cairo-gobject
    # The sources include the autotools/meson config.h. Our cairo is built
    # with -Dglib=disabled, so its cairo-features.h lacks
    # CAIRO_HAS_GOBJECT_FUNCTIONS; supply it here (the glue only needs the
    # cairo core API + glib-object.h, both of which are present).
    printf '#define CAIRO_HAS_GOBJECT_FUNCTIONS 1\n' > config.h
    for f in cairo-gobject-enums.c cairo-gobject-structs.c; do
      "${androidToolchain.androidCC}" -O2 -fPIC -I. $CFLAGS_PC -c "$f" -o "''${f%.c}.o"
    done
    "${androidToolchain.androidAR}" rcs libcairo-gobject.a cairo-gobject-enums.o cairo-gobject-structs.o
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/lib/pkgconfig $out/include/cairo
    cp libcairo-gobject.a $out/lib/
    cp cairo-gobject.h $out/include/cairo/
    cat > $out/lib/pkgconfig/cairo-gobject.pc <<EOF
    prefix=$out
    libdir=\''${prefix}/lib
    includedir=\''${prefix}/include

    Name: cairo-gobject
    Description: GObject type glue for cairo (static, Wawona Android cross build)
    Version: ${version}
    Requires: cairo gobject-2.0 glib-2.0
    Libs: -L\''${libdir} -lcairo-gobject
    Cflags: -I\''${includedir}/cairo
    EOF
    runHook postInstall
  '';
}
