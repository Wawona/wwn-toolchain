{ lib, pkgs, androidSDK ? null, allowExperimentalFallback ? false }:

let
  androidConfig = import ../android/sdk-config.nix {
    inherit lib androidSDK;
    system = pkgs.stdenv.hostPlatform.system;
  };

  androidApiLevel = androidConfig.androidApiLevel;
  androidNdkApiLevel = androidConfig.androidNdkApiLevel;
  androidTarget = androidConfig.androidTarget;
  androidNdkCflags = "-fuse-ld=lld";
  hostTagRequested = androidConfig.hostTag;

  ndkRoot =
    if androidConfig.sdkRoot == null then
      throw "androidSDK with sdkRoot/androidsdk is required for Android toolchain resolution"
    else
      "${androidConfig.sdkRoot}/ndk/${androidConfig.ndkVersion}";

  nativeToolchainBase = "${ndkRoot}/toolchains/llvm/prebuilt/${hostTagRequested}";
  compatHostTag =
    if hostTagRequested == "linux-arm64" then
      "linux-x86_64"
    else if hostTagRequested == "darwin-arm64" then
      "darwin-x86_64"
    else
      hostTagRequested;
  compatToolchainBase = "${ndkRoot}/toolchains/llvm/prebuilt/${compatHostTag}";
  nativePrebuiltExists = builtins.pathExists nativeToolchainBase;
  compatPrebuiltExists = builtins.pathExists compatToolchainBase;
  useSourceFallback =
    if nativePrebuiltExists then
      false
    else if allowExperimentalFallback && compatPrebuiltExists then
      true
    else
      throw ''
        Android NDK prebuilt toolchain for host tag '${hostTagRequested}' is unavailable at:
          ${nativeToolchainBase}

        Stable CI policy requires native host prebuilts only.
        If you want to try the compatibility fallback path, opt in explicitly with:
          WAWONA_ANDROID_EXPERIMENTAL_FALLBACK=1
        and run with --impure.
      '';
  toolchainBase = if useSourceFallback then compatToolchainBase else nativeToolchainBase;
  prebuiltCC = "${nativeToolchainBase}/bin/${androidTarget}${toString androidNdkApiLevel}-clang";
  prebuiltCXX = "${nativeToolchainBase}/bin/${androidTarget}${toString androidNdkApiLevel}-clang++";
  prebuiltAR = "${nativeToolchainBase}/bin/llvm-ar";
  prebuiltSTRIP = "${nativeToolchainBase}/bin/llvm-strip";
  prebuiltRANLIB = "${nativeToolchainBase}/bin/llvm-ranlib";
  useCompatNdkDriver = hostTagRequested == "darwin-arm64" && useSourceFallback;
  fallbackCC =
    if useCompatNdkDriver then
      "${toolchainBase}/bin/clang"
    else
      "${pkgs.llvmPackages.clang-unwrapped}/bin/clang";
  fallbackCXX =
    if useCompatNdkDriver then
      "${toolchainBase}/bin/clang++"
    else
      "${pkgs.llvmPackages.clang-unwrapped}/bin/clang++";
  fallbackLld =
    if useCompatNdkDriver then
      "${toolchainBase}/bin/ld.lld"
    else
      "${pkgs.llvmPackages.lld}/bin/ld.lld";
  fallbackAR =
    if useCompatNdkDriver then
      "${toolchainBase}/bin/llvm-ar"
    else
      "${pkgs.llvmPackages.bintools}/bin/ar";
  fallbackSTRIP =
    if useCompatNdkDriver then
      "${toolchainBase}/bin/llvm-strip"
    else
      "${pkgs.llvmPackages.bintools}/bin/strip";
  fallbackRANLIB =
    if useCompatNdkDriver then
      "${toolchainBase}/bin/llvm-ranlib"
    else
      "${pkgs.llvmPackages.bintools}/bin/ranlib";
  ndkSysroot = "${toolchainBase}/sysroot";
  ndkAbiLibDir = "${ndkSysroot}/usr/lib/aarch64-linux-android/${toString androidNdkApiLevel}";

  # Common compile-only detector: when clang is invoked with -c/-S/-E/-fsyntax-only
  # it does not link, so passing linker flags (-L, -fuse-ld, -Wl,-rpath-link) makes
  # clang emit -Wunused-command-line-argument. Meson's feature checks compile with
  # -Werror=unused-command-line-argument, which turns those into hard errors (seen
  # in fontconfig's gperf-len-type probe). Drop the linker flags in that case.
  adaptiveProbeAndLinkDetect = ''
    # Autoconf/libtool GCC probe compatibility.
    if [ "$#" -eq 1 ]; then
      case "$1" in
        -print-multi-os-directory|-print-multi-directory)
          echo "."
          exit 0
          ;;
      esac
    fi

    compile_only=0
    for arg in "$@"; do
      case "$arg" in
        -c|-S|-E|-fsyntax-only) compile_only=1 ;;
      esac
    done
    if [ "$compile_only" -eq 1 ]; then
      LINK_ARGS=()
    else
      LINK_ARGS=(-L"${ndkAbiLibDir}" -Wl,-rpath-link,"${ndkAbiLibDir}" -fuse-ld="${fallbackLld}")
    fi
  '';

  adaptiveCC = pkgs.writeShellScript "android-cc-adaptive" ''
    ${adaptiveProbeAndLinkDetect}

    # For arm64-host fallback, use the fallback clang's own resource headers.
    # Mixing NDK prebuilt resource headers with fallback clang triggers NEON
    # builtin mismatches (seen in zstd-android on linux-aarch64 CI).
    # NB: do not pass -D__ANDROID_API__ here. The target triple already encodes the
    # API level, and the NDK headers define __ANDROID_API__ = __ANDROID_MIN_SDK_VERSION__;
    # redefining it textually trips -Wmacro-redefined, which breaks meson feature
    # checks that compile with -Werror (e.g. glib's size_t-typedef probe).
    exec "${fallbackCC}" \
      --target=${androidTarget}${toString androidNdkApiLevel} \
      --sysroot="${ndkSysroot}" \
      -B"${ndkAbiLibDir}" \
      "''${LINK_ARGS[@]}" \
      "$@"
  '';

  adaptiveCXX = pkgs.writeShellScript "android-cxx-adaptive" ''
    ${adaptiveProbeAndLinkDetect}

    # Keep fallback C++ path symmetric with C path above.
    exec "${fallbackCXX}" \
      --target=${androidTarget}${toString androidNdkApiLevel} \
      --sysroot="${ndkSysroot}" \
      -B"${ndkAbiLibDir}" \
      "''${LINK_ARGS[@]}" \
      "$@"
  '';

  # NDK r29's prebuilt Mach-O tools ship as universal (x86_64+arm64) binaries
  # whose arm64 slice carries a hardened-runtime signature that AMFI rejects
  # inside the Nix build sandbox on Apple Silicon — the tool is SIGKILLed
  # ("Killed: 9", exit 137) the moment the build user execs it, even though the
  # exact same binary runs fine interactively. Observed on llvm-ar; the same
  # class can hit any downloaded NDK tool. Durable fix: ad-hoc re-sign a private
  # copy once per build into TMPDIR and exec that. This replaces the old manual
  # "binary warming" hack. Reusable across tools via `amfiResign`.
  #
  # `amfiResign slot binary extraArgs`: emits a shell snippet that sets `_bin`
  # to an AMFI-safe path (re-signed copy on Darwin, original elsewhere) and
  # execs it. `slot` must be unique per tool so their TMPDIR copies don't clash.
  #
  # NB1: the copy must keep the tool's own basename (llvm-ar etc.): llvm-ar
  # dispatches ar/ranlib/lib/dlltool from argv[0]'s *stem*, and a leading-dot
  # name like ".android-llvm-ar-resigned" has an empty stem (everything after
  # the first '.' is treated as the extension), which makes it bail with
  # "not ranlib, ar, lib or dlltool". So: hidden per-slot dir, real basename.
  #
  # NB2: this wrapper runs massively in parallel (the stdenv strip hook uses
  # xargs -P), so the resign must be race-safe: copy+sign into a unique temp
  # file and rename(2) it into place. Exec'ing a partially-copied binary gets
  # SIGKILLed, and a child killed by signal makes xargs exit 125, which the
  # strip hook treats as fatal (unlike plain per-file strip errors, code 123).
  amfiResign = slot: binary: extraArgs: ''
    _bin="${binary}"
    if [ "$(uname)" = "Darwin" ] && [ -n "''${TMPDIR:-}" ] && [ -x /usr/bin/codesign ]; then
      _dir="$TMPDIR/.android-resign-${slot}"
      _fixed="$_dir/${slot}"
      if [ ! -x "$_fixed" ]; then
        mkdir -p "$_dir" 2>/dev/null || true
        _tmp="$_dir/.tmp.$$"
        if cp "$_bin" "$_tmp" 2>/dev/null && chmod u+wx "$_tmp" 2>/dev/null \
          && /usr/bin/codesign --remove-signature "$_tmp" 2>/dev/null \
          && /usr/bin/codesign -f -s - "$_tmp" 2>/dev/null \
          && mv -f "$_tmp" "$_fixed" 2>/dev/null; then
          :
        else
          rm -f "$_tmp" 2>/dev/null || true
        fi
      fi
      if [ -x "$_fixed" ]; then
        _bin="$_fixed"
      fi
    fi
    exec "$_bin" ${extraArgs} "$@"
  '';
  androidGnuAR = pkgs.writeShellScript "android-gnu-ar" (
    amfiResign "llvm-ar" (if useSourceFallback then fallbackAR else prebuiltAR) "--format=gnu"
  );
  androidGnuRANLIB = pkgs.writeShellScript "android-gnu-ranlib" (
    amfiResign "llvm-ranlib" (if useSourceFallback then fallbackRANLIB else prebuiltRANLIB) ""
  );
  androidGnuSTRIP = pkgs.writeShellScript "android-gnu-strip" (
    amfiResign "llvm-strip" (if useSourceFallback then fallbackSTRIP else prebuiltSTRIP) ""
  );

in
rec {
  inherit androidApiLevel androidNdkApiLevel androidTarget androidNdkCflags;
  # Contract: callers should consume these exported values rather than
  # reconstructing host-tag/prebuilt paths in module-local code.
  androidNdkHostTag = hostTagRequested;
  androidNdkCompatHostTag = compatHostTag;
  androidNdkNativePrebuiltExists = nativePrebuiltExists;
  androidNdkCompatPrebuiltExists = compatPrebuiltExists;
  androidNdkExperimentalFallbackEnabled = allowExperimentalFallback;
  androidNdkIsFallback = useSourceFallback;
  androidNdkToolchainBase = toolchainBase;

  androidCC = if useSourceFallback then adaptiveCC else prebuiltCC;
  androidCXX = if useSourceFallback then adaptiveCXX else prebuiltCXX;
  androidAR = androidGnuAR;
  androidSTRIP = androidGnuSTRIP;
  androidRANLIB = androidGnuRANLIB;
  androidndkRoot = ndkRoot;
  # Unified sysroot + per-API lib dir (crtbegin_*.o, libc) — required when clang triple has no API suffix.
  androidNdkSysroot = ndkSysroot;
  androidNdkAbiLibDir = ndkAbiLibDir;
  androidNdkAbiLibDirFallback = "${androidNdkSysroot}/usr/lib/aarch64-linux-android";
}
