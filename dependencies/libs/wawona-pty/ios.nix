{
  lib,
  pkgs,
  buildPackages,
  iosToolchain,
  simulator ? false,
}:

let
  platformInfo = import ../../toolchains/apple-mobile-platform.nix;
  mobile = platformInfo { inherit iosToolchain simulator; };
in
pkgs.stdenv.mkDerivation {
  name = "wawona-pty-ios";
  src = ./.;

  __noChroot = true;

  nativeBuildInputs = [ ];

  preConfigure = ''
    ${iosToolchain.mkIOSBuildEnv { inherit simulator; minVersion = mobile.minVersion; }}
    unset MACOSX_DEPLOYMENT_TARGET IPHONEOS_DEPLOYMENT_TARGET
    export NIX_CFLAGS_COMPILE=""
    export NIX_LDFLAGS=""
  '';

  buildPhase = ''
    runHook preBuild
    $CC -c src/wwn_pty.c -Iinclude \
      -arch arm64 -isysroot "$SDKROOT" ${mobile.minVerFlag} \
      -fPIC -O2 -o wwn_pty.o
    # In-process external-command dispatch shim (forwards to the statically
    # linked uutils umbrella's wawona_coreutils_main, weak-linked).
    $CC -c src/wawona-dispatch.c -Iinclude \
      -arch arm64 -isysroot "$SDKROOT" ${mobile.minVerFlag} \
      -fPIC -O2 -o wawona-dispatch.o
    $AR rcs libwwn-pty.a wwn_pty.o wawona-dispatch.o
    # Note: consumers must link -ldl -lpthread when using wwn_pty on iOS.
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/lib $out/include
    cp libwwn-pty.a $out/lib/
    cp include/wwn_pty.h $out/include/
    runHook postInstall
  '';
}
