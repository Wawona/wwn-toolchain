# utf8proc - Unicode processing (fcft dependency) for Apple mobile.
{
  lib,
  pkgs,
  buildPackages,
  common,
  buildModule ? null,
  simulator ? false,
  iosToolchain ? null,
}:

let
  platformInfo = import ../../toolchains/apple-mobile-platform.nix;
  cmakeToolchain = import ../../toolchains/apple-cmake-toolchain.nix;
  mobile = platformInfo { inherit iosToolchain simulator; };
  src = pkgs.fetchFromGitHub {
    owner = "JuliaStrings";
    repo = "utf8proc";
    rev = "v2.11.3";
    hash = "sha256-DF2//R8Oc/+IEJuiG9+rTxQ7nltPcPqdCkzR4T7pUes=";
  };
in
pkgs.stdenv.mkDerivation {
  pname = "utf8proc";
  version = "2.11.3";
  inherit src;

  nativeBuildInputs = with buildPackages; [ cmake ninja ];
  __noChroot = true;

  preConfigure = ''
    ${iosToolchain.mkIOSBuildEnv { inherit simulator; minVersion = mobile.minVersion; }}
    export NIX_CFLAGS_COMPILE=""
    export NIX_LDFLAGS=""
    ${cmakeToolchain { inherit iosToolchain simulator; }}
    unset SDKROOT
  '';

  cmakeFlags = [
    "-DCMAKE_TOOLCHAIN_FILE=ios-toolchain.cmake"
    "-DCMAKE_INSTALL_LIBDIR=lib"
    "-DCMAKE_INSTALL_INCLUDEDIR=include"
    "-DBUILD_SHARED_LIBS=OFF"
    "-DUTF8PROC_ENABLE_TESTING=OFF"
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
  ];

  meta = with lib; {
    description = "UTF-8 Unicode processing for Apple mobile";
    homepage = "https://github.com/JuliaStrings/utf8proc";
    license = licenses.mit;
  };
}
