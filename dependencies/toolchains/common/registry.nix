# wwn-toolchain base registry: the cross-compile LIBRARY substrate + first-party
# wawona-pty. Patched-application entries (zsh, weston, foot, iland, waypipe, ...)
# live in their own wwn-* repos and are merged in as registry fragments by the
# consumer (Wawona, or a wwn-* repo's standalone build). See lib/default.nix.
let
  inherit (import ./with-platform-variants.nix) withPlatformVariants;
in
{
  libwayland = withPlatformVariants {
    android = ../../libs/libwayland/android.nix;
    wearos = ../../libs/libwayland/wearos.nix;
    ios = ../../libs/libwayland/ios.nix;
    tvos = ../../libs/libwayland/tvos.nix;
    ipados = ../../libs/libwayland/ios.nix;
    visionos = ../../libs/libwayland/visionos.nix;
    watchos = ../../libs/libwayland/watchos.nix;
    macos = ../../libs/libwayland/macos.nix;
  };
  expat = withPlatformVariants {
    android = ../../libs/expat/android.nix;
    wearos = ../../libs/expat/wearos.nix;
    ios = ../../libs/expat/ios.nix;
    tvos = ../../libs/expat/tvos.nix;
    ipados = ../../libs/expat/ios.nix;
    visionos = ../../libs/expat/visionos.nix;
    watchos = ../../libs/expat/watchos.nix;
    macos = ../../libs/expat/macos.nix;
  };
  libffi = withPlatformVariants {
    android = ../../libs/libffi/android.nix;
    wearos = ../../libs/libffi/wearos.nix;
    ios = ../../libs/libffi/ios.nix;
    tvos = ../../libs/libffi/tvos.nix;
    ipados = ../../libs/libffi/ios.nix;
    visionos = ../../libs/libffi/visionos.nix;
    watchos = ../../libs/libffi/watchos.nix;
    macos = ../../libs/libffi/macos.nix;
  };
  libintl = withPlatformVariants {
    android = ../../libs/libintl/android.nix;
    ios = ../../libs/libintl/ios.nix;
    ipados = ../../libs/libintl/ios.nix;
    tvos = ../../libs/libintl/tvos.nix;
    visionos = ../../libs/libintl/visionos.nix;
    watchos = ../../libs/libintl/watchos.nix;
    macos = null;
  };
  libxml2 = withPlatformVariants {
    android = ../../libs/libxml2/android.nix;
    wearos = ../../libs/libxml2/wearos.nix;
    ios = ../../libs/libxml2/ios.nix;
    tvos = ../../libs/libxml2/tvos.nix;
    ipados = ../../libs/libxml2/ios.nix;
    visionos = ../../libs/libxml2/visionos.nix;
    watchos = ../../libs/libxml2/watchos.nix;
    macos = ../../libs/libxml2/macos.nix;
  };
  swiftshader = withPlatformVariants {
    android = ../../libs/swiftshader/android.nix;
    wearos = ../../libs/swiftshader/wearos.nix;
    ios = null;
    macos = null;
  };
  zlib = withPlatformVariants {
    android = null;
    ios = ../../libs/zlib/ios.nix;
    tvos = ../../libs/zlib/tvos.nix;
    ipados = ../../libs/zlib/ios.nix;
    visionos = ../../libs/zlib/visionos.nix;
    watchos = ../../libs/zlib/watchos.nix;
    macos = null;
  };
  zstd = withPlatformVariants {
    android = ../../libs/zstd/android.nix;
    wearos = ../../libs/zstd/wearos.nix;
    ios = ../../libs/zstd/ios.nix;
    tvos = ../../libs/zstd/tvos.nix;
    ipados = ../../libs/zstd/ios.nix;
    visionos = ../../libs/zstd/visionos.nix;
    watchos = ../../libs/zstd/watchos.nix;
    macos = ../../libs/zstd/macos.nix;
  };
  lz4 = withPlatformVariants {
    android = ../../libs/lz4/android.nix;
    wearos = ../../libs/lz4/wearos.nix;
    ios = ../../libs/lz4/ios.nix;
    tvos = ../../libs/lz4/tvos.nix;
    ipados = ../../libs/lz4/ios.nix;
    visionos = ../../libs/lz4/visionos.nix;
    watchos = ../../libs/lz4/watchos.nix;
    macos = ../../libs/lz4/macos.nix;
  };
  ffmpeg = withPlatformVariants {
    android = ../../libs/ffmpeg/android.nix;
    wearos = ../../libs/ffmpeg/wearos.nix;
    ios = ../../libs/ffmpeg/ios.nix;
    tvos = ../../libs/ffmpeg/tvos.nix;
    ipados = ../../libs/ffmpeg/ios.nix;
    visionos = ../../libs/ffmpeg/visionos.nix;
    watchos = ../../libs/ffmpeg/watchos.nix;
    macos = ../../libs/ffmpeg/macos.nix;
  };
  spirv-tools = withPlatformVariants {
    android = null;
    ios = ../../libs/spirv-tools/ios.nix;
    tvos = ../../libs/spirv-tools/tvos.nix;
    ipados = ../../libs/spirv-tools/ios.nix;
    visionos = ../../libs/spirv-tools/visionos.nix;
    watchos = ../../libs/spirv-tools/watchos.nix;
    macos = ../../libs/spirv-tools/macos.nix;
  };
  pixman = withPlatformVariants {
    android = ../../libs/pixman/android.nix;
    wearos = ../../libs/pixman/wearos.nix;
    ios = ../../libs/pixman/ios.nix;
    tvos = ../../libs/pixman/tvos.nix;
    ipados = ../../libs/pixman/ios.nix;
    visionos = ../../libs/pixman/visionos.nix;
    watchos = ../../libs/pixman/watchos.nix;
    macos = ../../libs/pixman/macos.nix;
  };
  freetype = withPlatformVariants {
    android = ../../libs/freetype/android.nix;
    ios = ../../libs/freetype/ios.nix;
    ipados = ../../libs/freetype/ios.nix;
    tvos = ../../libs/freetype/ios.nix;
    visionos = ../../libs/freetype/ios.nix;
    watchos = ../../libs/freetype/ios.nix;
    macos = null; # uses pkgs.freetype
  };
  fribidi = withPlatformVariants {
    android = ../../libs/fribidi/android.nix;
    ios = ../../libs/fribidi/ios.nix;
    ipados = ../../libs/fribidi/ios.nix;
    tvos = ../../libs/fribidi/ios.nix;
    visionos = ../../libs/fribidi/ios.nix;
    watchos = ../../libs/fribidi/ios.nix;
    macos = null; # uses pkgs.fribidi
  };
  pcre2 = withPlatformVariants {
    android = ../../libs/pcre2/android.nix;
    ios = ../../libs/pcre2/ios.nix;
    ipados = ../../libs/pcre2/ios.nix;
    tvos = ../../libs/pcre2/ios.nix;
    visionos = ../../libs/pcre2/ios.nix;
    watchos = ../../libs/pcre2/ios.nix;
    macos = null; # uses pkgs.pcre2
  };
  fontconfig = withPlatformVariants {
    android = ../../libs/fontconfig/android.nix;
    ios = ../../libs/fontconfig/ios.nix;
    ipados = ../../libs/fontconfig/ios.nix;
    tvos = ../../libs/fontconfig/ios.nix;
    visionos = ../../libs/fontconfig/ios.nix;
    watchos = ../../libs/fontconfig/ios.nix;
    macos = null; # uses pkgs.fontconfig
  };
  glib = withPlatformVariants {
    android = ../../libs/glib/android.nix;
    ios = ../../libs/glib/ios.nix;
    ipados = ../../libs/glib/ios.nix;
    tvos = ../../libs/glib/ios.nix;
    visionos = ../../libs/glib/ios.nix;
    watchos = ../../libs/glib/ios.nix;
    macos = null; # uses pkgs.glib
  };
  harfbuzz = withPlatformVariants {
    android = ../../libs/harfbuzz/android.nix;
    ios = ../../libs/harfbuzz/ios.nix;
    ipados = ../../libs/harfbuzz/ios.nix;
    tvos = ../../libs/harfbuzz/ios.nix;
    visionos = ../../libs/harfbuzz/ios.nix;
    watchos = ../../libs/harfbuzz/ios.nix;
    macos = null; # uses pkgs.harfbuzz
  };
  cairo = withPlatformVariants {
    android = ../../libs/cairo/android.nix;
    ios = ../../libs/cairo/ios.nix;
    ipados = ../../libs/cairo/ios.nix;
    tvos = ../../libs/cairo/ios.nix;
    visionos = ../../libs/cairo/ios.nix;
    watchos = ../../libs/cairo/ios.nix;
    macos = null; # uses pkgs.cairo
  };
  cairo-gobject = withPlatformVariants {
    android = ../../libs/cairo-gobject/android.nix;
    ios = ../../libs/cairo-gobject/ios.nix;
    ipados = ../../libs/cairo-gobject/ios.nix;
    tvos = ../../libs/cairo-gobject/ios.nix;
    visionos = ../../libs/cairo-gobject/ios.nix;
    watchos = ../../libs/cairo-gobject/ios.nix;
    macos = null; # uses pkgs.cairo (gobject support built in)
  };
  pango = withPlatformVariants {
    android = ../../libs/pango/android.nix;
    ios = ../../libs/pango/ios.nix;
    ipados = ../../libs/pango/ios.nix;
    tvos = ../../libs/pango/ios.nix;
    visionos = ../../libs/pango/ios.nix;
    watchos = ../../libs/pango/ios.nix;
    macos = null; # uses pkgs.pango
  };
  libpng = withPlatformVariants {
    android = ../../libs/libpng/android.nix;
    ios = ../../libs/libpng/ios.nix;
    ipados = ../../libs/libpng/ios.nix;
    tvos = ../../libs/libpng/ios.nix;
    visionos = ../../libs/libpng/ios.nix;
    watchos = ../../libs/libpng/ios.nix;
    macos = null; # uses pkgs.libpng
  };
  xkbcommon = withPlatformVariants {
    android = ../../libs/xkbcommon/android.nix;
    wearos = ../../libs/xkbcommon/wearos.nix;
    ios = ../../libs/xkbcommon/ios.nix;
    tvos = ../../libs/xkbcommon/tvos.nix;
    ipados = ../../libs/xkbcommon/ios.nix;
    visionos = ../../libs/xkbcommon/visionos.nix;
    watchos = ../../libs/xkbcommon/watchos.nix;
    macos = ../../libs/xkbcommon/macos.nix;
  };
  openssl = withPlatformVariants {
    android = ../../libs/openssl/android.nix;
    wearos = ../../libs/openssl/wearos.nix;
    ios = ../../libs/openssl/ios.nix;
    tvos = ../../libs/openssl/tvos.nix;
    ipados = ../../libs/openssl/ios.nix;
    visionos = ../../libs/openssl/visionos.nix;
    watchos = ../../libs/openssl/watchos.nix;
    macos = null; # uses pkgs.openssl
  };
  # SSH stack (openssh, libssh2, sshpass) moved to the wwn-ssh repo; consumers
  # merge wwn-ssh.registryFragment on top of this base registry.
  mbedtls = withPlatformVariants {
    android = ../../libs/mbedtls/android.nix;
    wearos = ../../libs/mbedtls/wearos.nix;
    ios = ../../libs/mbedtls/ios.nix;
    tvos = ../../libs/mbedtls/tvos.nix;
    ipados = ../../libs/mbedtls/ios.nix;
    visionos = ../../libs/mbedtls/visionos.nix;
    watchos = ../../libs/mbedtls/watchos.nix;
    macos = null;
  };
  epoll-shim = withPlatformVariants {
    android = null; # bionic has epoll
    ios = ../../libs/epoll-shim/ios.nix;
    tvos = ../../libs/epoll-shim/tvos.nix;
    ipados = ../../libs/epoll-shim/ios.nix;
    visionos = ../../libs/epoll-shim/visionos.nix;
    watchos = ../../libs/epoll-shim/watchos.nix;
    macos = ../../libs/epoll-shim/macos.nix;
  };
  fcft = withPlatformVariants {
    android = null;
    ios = ../../libs/fcft/ios.nix;
    ipados = ../../libs/fcft/ios.nix;
    tvos = ../../libs/fcft/ios.nix;
    visionos = ../../libs/fcft/ios.nix;
    watchos = ../../libs/fcft/ios.nix;
    macos = ../../libs/fcft/macos.nix;
  };
  tllist = withPlatformVariants {
    android = null;
    ios = ../../libs/tllist/ios.nix;
    ipados = ../../libs/tllist/ios.nix;
    tvos = ../../libs/tllist/ios.nix;
    visionos = ../../libs/tllist/ios.nix;
    watchos = ../../libs/tllist/ios.nix;
    macos = ../../libs/tllist/macos.nix;
  };
  utf8proc = withPlatformVariants {
    android = null;
    ios = ../../libs/utf8proc/ios.nix;
    ipados = ../../libs/utf8proc/ios.nix;
    tvos = ../../libs/utf8proc/ios.nix;
    visionos = ../../libs/utf8proc/ios.nix;
    watchos = ../../libs/utf8proc/ios.nix;
    macos = ../../libs/utf8proc/macos.nix;
  };
  # ANGLE: OpenGL ES (GLES2/3) over Metal. macOS uses nixpkgs#angle (cached).
  # iOS/Android cross-compiled via GN (see dependencies/libs/angle/).
  angle = withPlatformVariants {
    android = ../../libs/angle/android.nix;
    ios = ../../libs/angle/ios.nix;
    ipados = ../../libs/angle/ios.nix;
    tvos = ../../libs/angle/ios.nix;
    visionos = ../../libs/angle/ios.nix;
    watchos = ../../libs/angle/ios.nix;
    macos = ../../libs/angle/macos.nix;
  };
  # In-process zsh stack: the iOS recipes are platform-agnostic (they resolve the
  # SDK/min-version from iosToolchain via apple-mobile-platform.nix), so the whole
  # Apple family reuses them — exactly like angle/iland above.
  "wawona-pty" = withPlatformVariants {
    android = ../../libs/wawona-pty/android.nix;
    ios = ../../libs/wawona-pty/ios.nix;
    ipados = ../../libs/wawona-pty/ios.nix;
    tvos = ../../libs/wawona-pty/ios.nix;
    visionos = ../../libs/wawona-pty/ios.nix;
    watchos = ../../libs/wawona-pty/ios.nix;
    macos = null;
  };
}
