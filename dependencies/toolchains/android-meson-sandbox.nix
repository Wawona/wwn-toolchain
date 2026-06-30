# Shared hooks for Android meson cross builds on sandboxed macOS CI runners.
#
# Meson often exec's bundled scripts with `#!/usr/bin/env python3` (or perl).
# The macOS Nix sandbox denies exec of /usr/bin/env (outside the sandbox ->
# EPERM). Cross builds also skip the default output fixup that would rewrite
# shebangs, so installed codegen tools (glib-mkenums) remain broken for
# downstream derivations until postInstall runs patchShebangs --build.
#
# Usage (inside wwn-toolchain android recipes):
#   pkgs.stdenv.mkDerivation (androidMesonSandbox.apply { ... })
#
# androidMesonSandbox is threaded through androidArgs in toolchains/default.nix.
{ lib }:
let
  postPatchHook = ''
    patchShebangs .
  '';

  postInstallHook = ''
    if [ -d "$out/bin" ]; then
      patchShebangs --build $out/bin
    fi
  '';

  appendHook =
    hookName: fragment: attrs:
    let
      existing = attrs.${hookName} or "";
    in
    if existing == "" then
      attrs // {
        ${hookName} = fragment;
      }
    else
      attrs // {
        ${hookName} = existing + "\n" + fragment;
      };

  apply = attrs:
    appendHook "postPatch" postPatchHook (appendHook "postInstall" postInstallHook attrs);
in
{
  inherit postPatchHook postInstallHook apply;
}
