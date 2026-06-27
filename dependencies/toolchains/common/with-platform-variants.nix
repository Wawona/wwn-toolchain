# Shared registry-entry normalizer. Extracted so that out-of-tree registry
# fragments (wwn-zsh, wwn-weston, ...) can build entries with identical
# fallback semantics to the in-tree base registry.
let
  firstNonNull =
    values:
    let
      filtered = builtins.filter (value: value != null) values;
    in
    if filtered == [ ] then null else builtins.head filtered;

  withPlatformVariants =
    entry:
    let
      iosDevice = firstNonNull [ (entry.iosDevice or null) (entry.ios or null) ];
      iosSim = firstNonNull [ (entry.iosSim or null) iosDevice ];
      tvosDevice = firstNonNull [ (entry.tvosDevice or null) (entry.tvos or null) ];
      tvosSim = firstNonNull [ (entry.tvosSim or null) tvosDevice ];
      ipadosDevice = firstNonNull [ (entry.ipadosDevice or null) (entry.ipados or null) ];
      ipadosSim = firstNonNull [ (entry.ipadosSim or null) ipadosDevice ];
      watchosDevice = firstNonNull [ (entry.watchosDevice or null) (entry.watchos or null) ];
      watchosSim = firstNonNull [ (entry.watchosSim or null) watchosDevice ];
      visionosDevice = firstNonNull [ (entry.visionosDevice or null) (entry.visionos or null) ];
      visionosSim = firstNonNull [ (entry.visionosSim or null) visionosDevice ];
      androidDevice = firstNonNull [ (entry.androidDevice or null) (entry.android or null) ];
      androidEmulator = firstNonNull [ (entry.androidEmulator or null) androidDevice ];
      wearosDevice = firstNonNull [ (entry.wearosDevice or null) (entry.wearos or null) androidDevice ];
      wearosEmulator = firstNonNull [ (entry.wearosEmulator or null) wearosDevice ];
      linuxNative = firstNonNull [ (entry.linuxNative or null) (entry.linux or null) ];
    in
    entry
    // {
      inherit iosDevice iosSim tvosDevice tvosSim ipadosDevice ipadosSim watchosDevice watchosSim visionosDevice visionosSim androidDevice androidEmulator wearosDevice wearosEmulator linuxNative;
      ios = entry.ios or iosDevice;
      tvos = entry.tvos or tvosDevice;
      ipados = entry.ipados or ipadosDevice;
      watchos = entry.watchos or watchosDevice;
      visionos = entry.visionos or visionosDevice;
      android = entry.android or androidDevice;
      wearos = entry.wearos or wearosDevice;
      linux = entry.linux or linuxNative;
    };
in
{
  inherit firstNonNull withPlatformVariants;
}
