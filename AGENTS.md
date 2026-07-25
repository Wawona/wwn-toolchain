# AGENTS.md — wwn-toolchain

Guidance for AI agents editing this repository.

## What this repo is

**L0: the substrate layer** for the whole Wawona organization. Cross-compilation
builders (`mkToolchains`, apple/android toolchains, `wawona-pty`) plus the shared
non-graphics-stack libraries every `wwn-*` port needs, exported as `baseRegistry`
(`dependencies/toolchains/common/registry.nix`, re-exported from `lib/default.nix`).

Substrate libs owned here: cairo, cairo-gobject, pango, fontconfig, freetype,
harfbuzz, fribidi, glib, **pixman**, libwayland, xkbcommon, epoll-shim, libpng,
expat, libffi, libintl, libxml2, zlib, zstd, lz4, pcre2, openssl, mbedtls, fcft,
tllist, utf8proc, ffmpeg.

## Repo DAG layer (L0) — never invert

`wwn-toolchain` is the **base** of the acyclic L0–L4 DAG. Everything depends on
it; it depends on **no `wwn-*` repo**.

- **Never** add `wwn-iland` / `wwn-weston` / `wwn-kmscube` (or any `wwn-*`) as a
  flake input of this repo.
- **Never** put graphics-stack keys (`iland`, `weston`, `kmscube`, `angle`,
  `swiftshader`, Vulkan ICDs) into `baseRegistry`. Those
  belong to `wwn-iland` (L1).
- `pixman` stays here (cairo depends on it); moving it into iland would create a
  cairo→iland cycle.
- Keep the intentional one-way `freetype ← harfbuzz`, `harfbuzz ↛ cairo`,
  `cairo ← pixman` disable edges in the ios/android recipes — do not re-enable
  casually (meson cycle risk).

`angle` and `swiftshader` have moved to `wwn-iland` (L1). Do not re-add their
recipes or registry entries here.

Canonical: `Wawona/docs/wwn-repo-dag.md` + workspace rule `wawona-repo-dag`.

## Do / don't

- **Do** keep this repo free of GLES/Vulkan present-stack code.
- **Do** expose new substrate libs through `baseRegistry` for all consumers.
- **Don't** import any app/graphics fragment back into `baseRegistry`.
- **Don't** add `wwn-*` flake inputs — the base layer has none.
