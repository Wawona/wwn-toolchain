# utf8proc - Unicode processing (fcft dependency) for Android NDK.
{
  lib,
  pkgs,
  buildPackages,
  common,
  buildModule ? null,
  androidToolchain ? (import ../../toolchains/android.nix { inherit lib pkgs; }),
  ...
}:

let
  src = pkgs.fetchFromGitHub {
    owner = "JuliaStrings";
    repo = "utf8proc";
    rev = "v2.11.3";
    hash = "sha256-DF2//R8Oc/+IEJuiG9+rTxQ7nltPcPqdCkzR4T7pUes=";
  };
  androidCmake = import ../../toolchains/android-cmake.nix {
    inherit lib pkgs androidToolchain;
  };
in
pkgs.stdenv.mkDerivation {
  pname = "utf8proc";
  version = "2.11.3";
  inherit src;

  nativeBuildInputs = with buildPackages; [ cmake ninja ];
  buildInputs = [ ];

  preConfigure = ''
    export CC="${androidToolchain.androidCC}"
    export CXX="${androidToolchain.androidCXX}"
    export AR="${androidToolchain.androidAR}"
    export STRIP="${androidToolchain.androidSTRIP}"
    export RANLIB="${androidToolchain.androidRANLIB}"
  '';

  cmakeFlags = [
    "-DCMAKE_C_COMPILER=${androidToolchain.androidCC}"
    "-DCMAKE_CXX_COMPILER=${androidToolchain.androidCXX}"
  ]
  ++ androidCmake.mkCrossFlags {
    abi = "arm64-v8a";
    useAndroidToolchainFile = true;
  }
  ++ [
    "-DCMAKE_INSTALL_LIBDIR=lib"
    "-DCMAKE_INSTALL_INCLUDEDIR=include"
    "-DBUILD_SHARED_LIBS=OFF"
    "-DUTF8PROC_ENABLE_TESTING=OFF"
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
  ];

  meta = with lib; {
    description = "UTF-8 Unicode processing for Android";
    homepage = "https://github.com/JuliaStrings/utf8proc";
    license = licenses.mit;
  };
}
