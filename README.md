# wwn-toolchain

Cross-compile toolchain, library substrate, and composable Nix registry shared by
[Wawona](https://github.com/Wawona/Wawona) and every `wwn-*` patched-software repo
(`wwn-zsh`, `wwn-weston`, `wwn-iland`, `wwn-kmscube`, `wwn-waypipe`, `wwn-coreutils`, `wwn-foot`, `wwn-fastfetch`).

It provides the Apple-platform (iOS, iPadOS, tvOS, watchOS, visionOS, macOS),
Android/Wear OS, and Linux cross builders plus the pristine cross-compiled
libraries those ports depend on (cairo, pango, fontconfig, freetype, harfbuzz,
fribidi, glib, pixman, libwayland, xkbcommon, epoll-shim, openssl, mbedtls,
ffmpeg, ANGLE, ...) and the first-party `wawona-pty`.

## Use

```nix
# flake.nix of a consumer
inputs.wwn-toolchain.url = "github:Wawona/wwn-toolchain";

# then, per system:
let
  tc = wwn-toolchain.lib.mkToolchains {
    inherit pkgs;
    # merge application registry fragments on top of the base library registry:
    registry = wwn-toolchain.lib.baseRegistry
      // wwn-zsh.registryFragment
      // wwn-fastfetch.registryFragment
      // wwn-weston.registryFragment;
    # inject cross-repo source paths recipes ask for:
    extraArgs = { ilandSrc = wwn-iland; };
  };
in
tc.buildForIOS "weston" { }
```

### `lib` surface

- `lib.mkToolchains { pkgs, pkgsIos?, pkgsAndroid?, androidSDK?, wawonaSrc?, registry?, extraArgs? }`
  -> `{ buildForIOS, buildForIPadOS, buildForTVOS, buildForWatchOS, buildForVisionOS, buildForAndroid, buildForWearOS, buildForMacOS, buildForLinux, androidToolchain, macos }`
- `lib.baseRegistry` - the library substrate registry (module name -> per-platform recipe paths).
- `lib.withPlatformVariants` - the registry-entry normalizer fragments reuse so their fallback semantics match the base registry.

## Registry fragments

Each `wwn-*` app repo exports a `registryFragment` using `withPlatformVariants`,
e.g. `wwn-zsh`:

```nix
registryFragment = {
  zsh = wwn-toolchain.lib.withPlatformVariants {
    ios = ./ios.nix; android = ./android.nix; ipados = ./ios.nix; # ...
  };
};
```

Consumers merge fragments over `baseRegistry` and pass the result to `mkToolchains`.

## License

MIT (the Nix recipes / framework). Upstream sources fetched at build time retain
their own licenses.
