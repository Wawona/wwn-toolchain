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
  # zstd source - fetch from GitHub
  src = pkgs.fetchFromGitHub {
    owner = "facebook";
    repo = "zstd";
    rev = "v1.5.7";
    sha256 = "sha256-tNFWIT9ydfozB8dWcmTMuZLCQmQudTFJIkSr0aG7S44=";
  };
in
pkgs.stdenv.mkDerivation {
  name = "zstd-ios";
  inherit src;
  patches = [ ];
  
  # Allow access to Xcode SDKs and toolchain
  __noChroot = true;
  nativeBuildInputs = with buildPackages; [
    cmake
    pkg-config
  ];
  buildInputs = [ ];
  preConfigure = ''
    ${xcodeUtils.mkIOSBuildEnv { inherit simulator; minVersion = mobileMinVersion; }}
    unset MACOSX_DEPLOYMENT_TARGET IPHONEOS_DEPLOYMENT_TARGET
    export NIX_CFLAGS_COMPILE=""
    export NIX_CXXFLAGS_COMPILE=""
    export NIX_LDFLAGS=""
    ${cmakeToolchain { inherit iosToolchain simulator; }}
    unset SDKROOT
  '';

  # zstd has CMakeLists.txt in build/cmake subdirectory
  sourceRoot = "source/build/cmake";

  cmakeFlags = [
    "-DCMAKE_TOOLCHAIN_FILE=ios-toolchain.cmake"
    "-DZSTD_BUILD_PROGRAMS=OFF"
    "-DZSTD_BUILD_SHARED=OFF"
    "-DZSTD_BUILD_STATIC=ON"
  ];
}
