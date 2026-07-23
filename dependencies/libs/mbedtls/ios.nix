{
  lib,
  pkgs,
  buildPackages,
  common,
  buildModule,
  simulator ? false,
  iosToolchain,
}:

let
  xcodeUtils = iosToolchain;
  platformInfo = import ../../toolchains/apple-mobile-platform.nix;
  cmakeToolchain = import ../../toolchains/apple-cmake-toolchain.nix;
  mobile = platformInfo { inherit iosToolchain simulator; };
  mobileMinVersion = mobile.minVersion;
  # mbedtls source - fetch from GitHub with submodules
  src = pkgs.fetchFromGitHub {
    owner = "Mbed-TLS";
    repo = "mbedtls";
    rev = "v3.6.0";
    sha256 = "sha256-tCwAKoTvY8VCjcTPNwS3DeitflhpKHLr6ygHZDbR6wQ=";
    fetchSubmodules = true;
  };
in
pkgs.stdenv.mkDerivation {
  name = "mbedtls-ios";
  inherit src;
  patches = [ ];
  
  # Allow access to Xcode SDKs and toolchain
  __noChroot = true;
  nativeBuildInputs = with buildPackages; [
    cmake
    perl
  ];
  buildInputs = [ ];
  preConfigure = ''
    ${xcodeUtils.mkIOSBuildEnv { inherit simulator; minVersion = mobileMinVersion; }}
    unset MACOSX_DEPLOYMENT_TARGET IPHONEOS_DEPLOYMENT_TARGET
    export NIX_CFLAGS_COMPILE=""
    export NIX_CXXFLAGS_COMPILE=""
    export NIX_LDFLAGS=""
    ${cmakeToolchain { inherit iosToolchain simulator; }}
    cat >> ios-toolchain.cmake <<EOF
set(CMAKE_C_FLAGS "\''${CMAKE_C_FLAGS} -fPIC -Wno-unknown-warning-option -Wno-unterminated-string-initialization")
set(CMAKE_CXX_FLAGS "\''${CMAKE_CXX_FLAGS} -fPIC -Wno-unknown-warning-option -Wno-unterminated-string-initialization")
EOF
    unset SDKROOT
  '';
  cmakeFlags = [
    "-DCMAKE_TOOLCHAIN_FILE=ios-toolchain.cmake"
    "-DENABLE_PROGRAMS=OFF"
    "-DENABLE_TESTING=OFF"
    "-DUSE_SHARED_MBEDTLS_LIBRARY=OFF"
    "-DUSE_STATIC_MBEDTLS_LIBRARY=ON"
    "-DMBEDTLS_FATAL_WARNINGS=OFF"
  ];
}
