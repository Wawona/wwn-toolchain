{
  description = "wwn-toolchain: Wawona's cross-compile toolchain (Apple platforms, Android, Linux) + library substrate + composable Nix registry consumed by Wawona and every wwn-* repo.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    rust-overlay.url = "github:oxalica/rust-overlay";
    rust-overlay.inputs.nixpkgs.follows = "nixpkgs";
    # Build tooling for L3′/L4 Rust (Relay, Wawona backend). Same class as
    # rust-overlay: not a graphics/app fragment. Consumers follow this pin
    # with `crate2nix.follows = "wwn-toolchain/crate2nix"`.
    crate2nix.url = "github:nix-community/crate2nix";
    crate2nix.inputs.nixpkgs.follows = "nixpkgs";
    android-nixpkgs = {
      url = "github:tadfisher/android-nixpkgs/5585cc3ee71bdd8d9ee255523f11b920138fa688";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, nixpkgs, rust-overlay, crate2nix, ... }:
    let
      darwinSystems = [ "x86_64-darwin" "aarch64-darwin" ];
      linuxSystems = [ "x86_64-linux" "aarch64-linux" ];
      allSystems = darwinSystems ++ linuxSystems;
      forAll = nixpkgs.lib.genAttrs allSystems;

      pkgsFor = system: import nixpkgs {
        inherit system;
        overlays = [ (import rust-overlay) ];
        config = {
          allowUnfree = true;
          allowUnsupportedSystem = true;
          android_sdk.accept_license = true;
        };
      };
    in
    {
      # Primary deliverable: reusable lib (withPlatformVariants, baseRegistry, mkToolchains).
      lib = import ./lib;

      # Smoke outputs proving the substrate instantiates. These exercise mkToolchains
      # + the base registry + the Apple/Android recipes. Full artifact builds run in CI.
      packages = forAll (system:
        let
          pkgs = pkgsFor system;
          tc = self.lib.mkToolchains { inherit pkgs; };
          isDarwin = builtins.elem system darwinSystems;
        in
        (if isDarwin then {
          xkbcommon-ios = tc.buildForIOS "xkbcommon" { };
          libwayland-ios = tc.buildForIOS "libwayland" { };
          pixman-ios = tc.buildForIOS "pixman" { };
          xkbcommon-macos = tc.buildForMacOS "xkbcommon" { };
        } else {
          xkbcommon-android = tc.buildForAndroid "xkbcommon" { };
          libwayland-android = tc.buildForAndroid "libwayland" { };
        }));

      formatter = forAll (system: (pkgsFor system).nixfmt-rfc-style);
    };
}
