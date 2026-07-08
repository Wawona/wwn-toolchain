{
  lib,
  pkgs,
  buildPackages,
  common,
  buildModule,
  simulator ? false,
  iosToolchain,
}:

{
  buildForIPadOS =
    name: entry:
    if name == "libwayland" then
      pkgs.callPackage ../libs/libwayland/ipados.nix { inherit buildPackages common buildModule simulator iosToolchain; }
    else if name == "expat" then
      pkgs.callPackage ../libs/expat/ipados.nix { inherit buildPackages common buildModule simulator iosToolchain; }
    else if name == "libffi" then
      pkgs.callPackage ../libs/libffi/ipados.nix { inherit buildPackages common buildModule simulator iosToolchain; }
    else if name == "libxml2" then
      pkgs.callPackage ../libs/libxml2/ipados.nix { inherit buildPackages common buildModule simulator iosToolchain; }
    else if name == "waypipe" then
      pkgs.callPackage ../libs/waypipe/ipados.nix { inherit buildPackages common buildModule simulator iosToolchain; }
    else if name == "zlib" then
      pkgs.callPackage ../libs/zlib/ipados.nix { inherit buildPackages common buildModule simulator iosToolchain; }
    else if name == "zstd" then
      pkgs.callPackage ../libs/zstd/ipados.nix { inherit buildPackages common buildModule simulator iosToolchain; }
    else if name == "lz4" then
      pkgs.callPackage ../libs/lz4/ipados.nix { inherit buildPackages common buildModule simulator iosToolchain; }
    else if name == "ffmpeg" then
      pkgs.callPackage ../libs/ffmpeg/ipados.nix { inherit buildPackages common buildModule simulator iosToolchain; }
    else if name == "spirv-tools" then
      pkgs.callPackage ../libs/spirv-tools/ipados.nix { inherit buildPackages common buildModule simulator iosToolchain; }
    else if name == "xkbcommon" then
      pkgs.callPackage ../libs/xkbcommon/ipados.nix { inherit buildPackages common buildModule simulator iosToolchain; }
    else if name == "mbedtls" then
      pkgs.callPackage ../libs/mbedtls/ipados.nix { inherit buildPackages common buildModule simulator iosToolchain; }
    else if name == "openssl" then
      pkgs.callPackage ../libs/openssl/ipados.nix { inherit buildPackages common buildModule simulator iosToolchain; }
    # SSH stack (openssh/libssh2/sshpass) moved to the wwn-ssh repo; resolved
    # via the merged registry, never via this fallback.
    else if name == "epoll-shim" then
      pkgs.callPackage ../libs/epoll-shim/ipados.nix { inherit buildPackages common buildModule simulator iosToolchain; }
    else if name == "pixman" then
      pkgs.callPackage ../libs/pixman/ipados.nix { inherit buildPackages common buildModule simulator iosToolchain; }
    else if name == "weston" then
      pkgs.callPackage ../clients/weston/ipados.nix { inherit buildPackages common buildModule simulator iosToolchain; }
    else if name == "weston-simple-shm" then
      pkgs.callPackage ../libs/weston-simple-shm/ipados.nix { inherit buildPackages common buildModule simulator iosToolchain; }
    else if name == "foot" then
      pkgs.callPackage ../clients/foot/ipados.nix { inherit buildPackages common buildModule simulator iosToolchain; }
    else
      throw "Unknown iPadOS dependency '${name}'. Add it to dependencies/platforms/ipados.nix and registry.nix.";
}
