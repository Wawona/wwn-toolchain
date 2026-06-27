# wwn-toolchain reusable library.
#
# This is the public surface consumed by Wawona and every wwn-* application repo.
# It is pure (no system/pkgs captured here); callers pass `pkgs` to mkToolchains.
let
  wpv = import ../dependencies/toolchains/common/with-platform-variants.nix;
in
rec {
  # Registry-entry normalizer shared with out-of-tree fragments.
  inherit (wpv) withPlatformVariants firstNonNull;

  # The cross-compile LIBRARY substrate + wawona-pty. Merge wwn-* fragments on
  # top of this to obtain a full registry, e.g.
  #   registry = baseRegistry // wwn-zsh.registryFragment // wwn-weston.registryFragment;
  baseRegistry = import ../dependencies/toolchains/common/registry.nix;

  # Build the per-platform toolchain interface
  #   { buildForIOS, buildForIPadOS, buildForTVOS, buildForWatchOS,
  #     buildForVisionOS, buildForAndroid, buildForWearOS, buildForMacOS,
  #     buildForLinux, androidToolchain, macos }
  #
  # `registry` defaults to baseRegistry; consumers pass `baseRegistry // fragments`.
  # `extraArgs` is threaded into every recipe (intersected by its signature), used
  # to inject cross-repo source paths such as `ilandSrc` for wwn-weston.
  mkToolchains =
    { pkgs
    , pkgsIos ? null
    , pkgsAndroid ? null
    , androidSDK ? null
    , androidAllowExperimentalFallback ? false
    , wawonaSrc ? null
    , registry ? null
    , extraArgs ? { }
    }:
    import ../dependencies/toolchains {
      inherit (pkgs) lib stdenv buildPackages;
      inherit pkgs pkgsIos pkgsAndroid androidSDK androidAllowExperimentalFallback;
      wawonaSrc = if wawonaSrc != null then wawonaSrc else ../.;
      registryOverride = if registry != null then registry else baseRegistry;
      # `toolchainSrc` lets relocated wwn-* recipes import toolchain-internal
      # helpers (apple-mobile-platform.nix, android.nix) from this input's store
      # path instead of a now-invalid ../../toolchains relative path. Consumers
      # may override it via extraArgs.
      extraArgs = { toolchainSrc = ../.; } // extraArgs;
    };
}
