# fcft - font loading / glyph rasterization (fuzzel dependency) for Apple mobile.
{
  lib,
  pkgs,
  buildPackages,
  common,
  buildModule,
  simulator ? false,
  iosToolchain ? null,
}:

let
  fetchSource = common.fetchSource;
  platformInfo = import ../../toolchains/apple-mobile-platform.nix;
  mobile = platformInfo { inherit iosToolchain simulator; };
  fcftSource = {
    source = "codeberg";
    owner = "dnkl";
    repo = "fcft";
    tag = "3.3.3";
    sha256 = "sha256-MkGlph9WpqH4daov5ZZPO2ua2mUbrsuo8Xk6GoKhoxg=";
  };
  src = fetchSource fcftSource;

  freetype = buildModule.buildForIOS "freetype" { inherit simulator; };
  fontconfig = buildModule.buildForIOS "fontconfig" { inherit simulator; };
  pixman = buildModule.buildForIOS "pixman" { inherit simulator; };
  tllist = buildModule.buildForIOS "tllist" { inherit simulator; };
  utf8proc = buildModule.buildForIOS "utf8proc" { inherit simulator; };
  expat = buildModule.buildForIOS "expat" { inherit simulator; };
  pcPath = lib.concatMapStringsSep ":" (d: "${d}/lib/pkgconfig") [
    freetype fontconfig pixman tllist utf8proc expat
  ];
in
pkgs.stdenv.mkDerivation {
  pname = "fcft";
  version = "3.3.3";
  inherit src;

  nativeBuildInputs = with pkgs; [ meson ninja pkg-config scdoc ];
  buildInputs = [ freetype fontconfig pixman tllist utf8proc expat ];
  __noChroot = true;

  postPatch = ''
    mkdir -p threads_compat
    cat > threads_compat/threads.h << 'EOF'
#ifndef FCFT_THREADS_H_COMPAT
#define FCFT_THREADS_H_COMPAT
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <sched.h>
typedef pthread_t thrd_t;
typedef pthread_mutex_t mtx_t;
typedef pthread_cond_t cnd_t;
typedef pthread_once_t once_flag;
typedef pthread_key_t tss_t;
typedef void (*tss_dtor_t)(void *);
typedef int (*thrd_start_t)(void *);
enum { thrd_success = 0, thrd_nomem = ENOMEM, thrd_timedout = ETIMEDOUT, thrd_busy = EBUSY, thrd_error = -1 };
enum { mtx_plain = 0, mtx_recursive = 1, mtx_timed = 2 };
#define ONCE_FLAG_INIT PTHREAD_ONCE_INIT
static inline int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {
    return pthread_create(thr, NULL, (void*(*)(void*))func, arg) == 0 ? thrd_success : thrd_error;
}
static inline int thrd_join(thrd_t thr, int *res) {
    void *retval;
    int r = pthread_join(thr, &retval);
    if (res) *res = (int)(intptr_t)retval;
    return r == 0 ? thrd_success : thrd_error;
}
static inline thrd_t thrd_current(void) { return pthread_self(); }
static inline int thrd_equal(thrd_t a, thrd_t b) { return pthread_equal(a, b); }
static inline void thrd_yield(void) { sched_yield(); }
static inline int mtx_init(mtx_t *mtx, int type) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    if (type & mtx_recursive) pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    int r = pthread_mutex_init(mtx, &attr);
    pthread_mutexattr_destroy(&attr);
    return r == 0 ? thrd_success : thrd_error;
}
static inline int mtx_lock(mtx_t *mtx) { return pthread_mutex_lock(mtx) == 0 ? thrd_success : thrd_error; }
static inline int mtx_unlock(mtx_t *mtx) { return pthread_mutex_unlock(mtx) == 0 ? thrd_success : thrd_error; }
static inline int mtx_trylock(mtx_t *mtx) {
    int r = pthread_mutex_trylock(mtx);
    if (r == 0) return thrd_success;
    if (r == EBUSY) return thrd_busy;
    return thrd_error;
}
static inline void mtx_destroy(mtx_t *mtx) { pthread_mutex_destroy(mtx); }
static inline int cnd_init(cnd_t *cnd) { return pthread_cond_init(cnd, NULL) == 0 ? thrd_success : thrd_error; }
static inline int cnd_signal(cnd_t *cnd) { return pthread_cond_signal(cnd) == 0 ? thrd_success : thrd_error; }
static inline int cnd_broadcast(cnd_t *cnd) { return pthread_cond_broadcast(cnd) == 0 ? thrd_success : thrd_error; }
static inline int cnd_wait(cnd_t *cnd, mtx_t *mtx) { return pthread_cond_wait(cnd, mtx) == 0 ? thrd_success : thrd_error; }
static inline void cnd_destroy(cnd_t *cnd) { pthread_cond_destroy(cnd); }
static inline void call_once(once_flag *flag, void (*func)(void)) { pthread_once(flag, func); }
static inline int tss_create(tss_t *key, tss_dtor_t dtor) { return pthread_key_create(key, dtor) == 0 ? thrd_success : thrd_error; }
static inline void *tss_get(tss_t key) { return pthread_getspecific(key); }
static inline int tss_set(tss_t key, void *val) { return pthread_setspecific(key, val) == 0 ? thrd_success : thrd_error; }
static inline void tss_delete(tss_t key) { pthread_key_delete(key); }
#endif
EOF
    cat > threads_compat/byteswap.h << 'EOF'
#ifndef FCFT_BYTESWAP_H_COMPAT
#define FCFT_BYTESWAP_H_COMPAT
#include <stdint.h>
#include <libkern/OSByteOrder.h>
#ifndef bswap_16
#define bswap_16(x) OSSwapInt16((uint16_t)(x))
#endif
#ifndef bswap_32
#define bswap_32(x) OSSwapInt32((uint32_t)(x))
#endif
#ifndef bswap_64
#define bswap_64(x) OSSwapInt64((uint64_t)(x))
#endif
#endif
EOF
    sed -i '1i#include <xlocale.h>' fcft.c
  '';

  preConfigure = ''
    ${iosToolchain.mkIOSBuildEnv { inherit simulator; minVersion = mobile.minVersion; }}
    export NIX_CFLAGS_COMPILE=""
    export NIX_LDFLAGS=""
    export CC="$XCODE_CLANG"
    export CXX="$XCODE_CLANGXX"
    export PKG_CONFIG_PATH="${pcPath}:$PKG_CONFIG_PATH"
    export PKG_CONFIG_ALLOW_CROSS=1

    _CC="$XCODE_CLANG"
    _CXX="$XCODE_CLANGXX"
    _SDK="$SDKROOT"
    _ARCH="$IOS_ARCH"
    _DEPLOY="$APPLE_DEPLOYMENT_FLAG"
    _TARGET="$APPLE_LINKER_TARGET"

    cat > ios-cross.txt <<EOF
[binaries]
c = '$_CC'
cpp = '$_CXX'
c_for_build = '${buildPackages.clang}/bin/clang'
cpp_for_build = '${buildPackages.clang}/bin/clang++'
ar = 'ar'
strip = 'strip'
pkgconfig = 'pkg-config'

[host_machine]
system = 'darwin'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'

[properties]
c_args = ['-arch', '$_ARCH', '-target', '$_TARGET', '-isysroot', '$_SDK', '$_DEPLOY', '-fPIC', '-I$(pwd)/threads_compat']
c_link_args = ['-arch', '$_ARCH', '-target', '$_TARGET', '-isysroot', '$_SDK', '$_DEPLOY']
needs_exe_wrapper = true
EOF
  '';

  dontUseMesonConfigure = true;

  buildPhase = ''
    runHook preBuild
    export PKG_CONFIG_PATH="${pcPath}:$PKG_CONFIG_PATH"
    export PKG_CONFIG_ALLOW_CROSS=1
    unset SDKROOT
    meson setup build --prefix=$out --cross-file=ios-cross.txt \
      -Ddocs=disabled \
      -Dtest-text-shaping=false \
      -Dgrapheme-shaping=disabled \
      -Drun-shaping=disabled \
      -Ddefault_library=static \
      --buildtype=plain
    meson compile -C build
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    meson install -C build
    runHook postInstall
  '';

  meta = with lib; {
    description = "Font loading and glyph rasterization for Apple mobile";
    homepage = "https://codeberg.org/dnkl/fcft";
    license = licenses.mit;
  };
}
