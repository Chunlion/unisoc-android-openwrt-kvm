#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
WORK_DIR="${CROSVM_BUILD_DIR:-$PROJECT_DIR/cache/crosvm-a13-build}"
OUTPUT="${CROSVM_OUTPUT:-$PROJECT_DIR/assets/crosvm/android13/crosvm}"
CROSVM_COMMIT=d5b0ee8806620d92da18368137882d3839677a9e
MINIJAIL_COMMIT=8910803d95f549371f5b3057d5e7d9d6d6a7d826
RUST_TOOLCHAIN=1.90.0
LIBCAP_URL=https://ports.ubuntu.com/ubuntu-ports/pool/main/libc/libcap2/libcap-dev_2.44-1ubuntu0.22.04.3_arm64.deb
LIBCAP_SHA256=3bed08372a42d5969dddfcdebb09e37bbf29f4f8fef01484eb2c0c03199cecba

die() { echo "build-crosvm-a13: $*" >&2; exit 1; }
for command in git curl sha256sum dpkg-deb rustup aarch64-linux-gnu-gcc \
        aarch64-linux-gnu-ar aarch64-linux-gnu-strip pkg-config; do
    command -v "$command" >/dev/null 2>&1 || die "missing command: $command"
done

mkdir -p "$WORK_DIR" "$(dirname -- "$OUTPUT")"
if [[ ! -d "$WORK_DIR/crosvm/.git" ]]; then
    git clone --filter=blob:none --no-checkout \
        https://android.googlesource.com/platform/external/crosvm "$WORK_DIR/crosvm"
    git -C "$WORK_DIR/crosvm" checkout --detach "$CROSVM_COMMIT"
fi
if [[ ! -d "$WORK_DIR/minijail/.git" ]]; then
    git clone --filter=blob:none --no-checkout \
        https://android.googlesource.com/platform/external/minijail "$WORK_DIR/minijail"
    git -C "$WORK_DIR/minijail" checkout --detach "$MINIJAIL_COMMIT"
fi
[[ "$(git -C "$WORK_DIR/crosvm" rev-parse HEAD)" == "$CROSVM_COMMIT" ]] || \
    die "crosvm source has the wrong commit"
[[ "$(git -C "$WORK_DIR/minijail" rev-parse HEAD)" == "$MINIJAIL_COMMIT" ]] || \
    die "minijail source has the wrong commit"

if git -C "$WORK_DIR/crosvm" apply --check "$SCRIPT_DIR/crosvm-a13.patch"; then
    git -C "$WORK_DIR/crosvm" apply "$SCRIPT_DIR/crosvm-a13.patch"
elif ! git -C "$WORK_DIR/crosvm" apply --reverse --check "$SCRIPT_DIR/crosvm-a13.patch"; then
    die "crosvm source is modified and the portability patch cannot be applied"
fi
cp "$SCRIPT_DIR/Cargo.lock" "$WORK_DIR/crosvm/Cargo.lock"

mkdir -p "$WORK_DIR/packages" "$WORK_DIR/sysroot"
if [[ ! -s "$WORK_DIR/packages/libcap-dev_arm64.deb" ]]; then
    curl -fL --retry 3 -o "$WORK_DIR/packages/libcap-dev_arm64.deb.part" "$LIBCAP_URL"
    mv "$WORK_DIR/packages/libcap-dev_arm64.deb.part" \
        "$WORK_DIR/packages/libcap-dev_arm64.deb"
fi
echo "$LIBCAP_SHA256  $WORK_DIR/packages/libcap-dev_arm64.deb" | sha256sum -c -
dpkg-deb -x "$WORK_DIR/packages/libcap-dev_arm64.deb" "$WORK_DIR/sysroot"

rustup toolchain install "$RUST_TOOLCHAIN" --profile minimal
rustup target add aarch64-unknown-linux-gnu --toolchain "$RUST_TOOLCHAIN"
CARGO_BIN="$(rustup which cargo --toolchain "$RUST_TOOLCHAIN")"
RUSTC_BIN="$(rustup which rustc --toolchain "$RUST_TOOLCHAIN")"

sysroot="$WORK_DIR/sysroot"
(
    cd "$WORK_DIR/crosvm"
    export PKG_CONFIG_ALLOW_CROSS=1
    export PKG_CONFIG_ALL_STATIC=1
    export PKG_CONFIG_SYSROOT_DIR="$sysroot"
    export PKG_CONFIG_PATH="$sysroot/usr/lib/aarch64-linux-gnu/pkgconfig"
    export CFLAGS="-I$sysroot/usr/include"
    export LDFLAGS="-L$sysroot/usr/lib/aarch64-linux-gnu"
    export CROSS_COMPILE=aarch64-linux-gnu-
    export CC_aarch64_unknown_linux_gnu=aarch64-linux-gnu-gcc
    export AR_aarch64_unknown_linux_gnu=aarch64-linux-gnu-ar
    export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-linux-gnu-gcc
    export RUSTFLAGS='-C target-feature=+crt-static'
    export RUSTC="$RUSTC_BIN"
    "$CARGO_BIN" build --locked --profile lto \
        --target aarch64-unknown-linux-gnu --no-default-features \
        --features default-no-sandbox --bin crosvm
)

install -m 0755 \
    "$WORK_DIR/crosvm/target/aarch64-unknown-linux-gnu/lto/crosvm" "$OUTPUT"
aarch64-linux-gnu-strip "$OUTPUT"
file "$OUTPUT"
sha256sum "$OUTPUT"
