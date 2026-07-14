# tllist - header-only typed linked list (fuzzel + fcft) for Android.
{
  lib,
  pkgs,
  common,
  ...
}:

let
  fetchSource = common.fetchSource;
  tllistSource = {
    source = "codeberg";
    owner = "dnkl";
    repo = "tllist";
    tag = "1.1.0";
    sha256 = "sha256-4WW0jGavdFO3LX9wtMPzz3Z1APCPgUQOktpmwAM0SQw=";
  };
  src = fetchSource tllistSource;
in
pkgs.stdenv.mkDerivation {
  pname = "tllist";
  version = "1.1.0";
  inherit src;

  dontConfigure = true;
  dontBuild = true;

  installPhase = ''
    mkdir -p $out/include $out/lib/pkgconfig
    cp tllist.h $out/include/
    cat > $out/lib/pkgconfig/tllist.pc <<EOF
prefix=$out
libdir=$out/lib
includedir=$out/include

Name: tllist
Description: Typed linked list C header library
Version: 1.1.0
Cflags: -I$out/include
EOF
  '';

  meta = with lib; {
    description = "Typed linked list C library for Android";
    homepage = "https://codeberg.org/dnkl/tllist";
    license = licenses.mit;
  };
}
