# cairo-gobject cross-compiled for iOS (static). GObject type glue for cairo,
# required by Rust's cairo-sys-rs (pulled in by pango/pangocairo crates, e.g.
# the wwn-niri port). The main cairo recipe builds with -Dglib=disabled to stay
# on the proven minimal meson path, so this module compiles cairo's checked-in
# util/cairo-gobject sources (two C files) against cairo + glib directly.
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
  src = pkgs.cairo.src;
  version = pkgs.cairo.version;
  cairo = buildModule.buildForIOS "cairo" { inherit simulator; };
  glib = buildModule.buildForIOS "glib" { inherit simulator; };
  pcre2 = buildModule.buildForIOS "pcre2" { inherit simulator; };
  libffi = buildModule.buildForIOS "libffi" { inherit simulator; };
  pixman = buildModule.buildForIOS "pixman" { inherit simulator; };
  freetype = buildModule.buildForIOS "freetype" { inherit simulator; };
  fontconfig = buildModule.buildForIOS "fontconfig" { inherit simulator; };
  expat = buildModule.buildForIOS "expat" { inherit simulator; };
  libpng = buildModule.buildForIOS "libpng" { inherit simulator; };
  pcDeps = [ cairo glib pcre2 libffi pixman freetype fontconfig expat libpng ];
  pcPath = lib.concatMapStringsSep ":" (d: "${d}/lib/pkgconfig") pcDeps;
in
pkgs.stdenv.mkDerivation {
  name = "cairo-gobject-ios";
  inherit src;

  __noChroot = true;

  nativeBuildInputs = with buildPackages; [ pkg-config ];
  buildInputs = [ ];

  preConfigure = xcodeUtils.mkIOSBuildEnv { inherit simulator; };

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
      "$XCODE_CLANG" -target "$APPLE_LINKER_TARGET" -isysroot "$SDKROOT" \
        $APPLE_DEPLOYMENT_FLAG -O2 -fPIC -I. $CFLAGS_PC -c "$f" -o "''${f%.c}.o"
    done
    ar rcs libcairo-gobject.a cairo-gobject-enums.o cairo-gobject-structs.o
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
    Description: GObject type glue for cairo (static, Wawona iOS cross build)
    Version: ${version}
    Requires: cairo gobject-2.0 glib-2.0
    Libs: -L\''${libdir} -lcairo-gobject
    Cflags: -I\''${includedir}/cairo
    EOF
    runHook postInstall
  '';
}
