#!/usr/bin/env bash
# Cross-compiles the shared Rust core and its JNI shim for Android, then drops
# the libraries where Gradle expects them.
#
#   ANDROID_NDK_HOME=/path/to/ndk JOT_ABIS="arm64-v8a" ./build-rust.sh
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"

if [ -z "${ANDROID_NDK_HOME:-}" ]; then
    echo "ANDROID_NDK_HOME is not set; point it at an NDK r27 or newer." >&2
    exit 1
fi

if ! command -v cargo-ndk >/dev/null 2>&1; then
    echo "cargo-ndk is missing; install it with: cargo install cargo-ndk --locked" >&2
    exit 1
fi

abis="${JOT_ABIS:-arm64-v8a x86_64}"
targets=()
for abi in $abis; do
    targets+=(-t "$abi")
done

# Android 15 devices require 16 KB page alignment for native libraries.
export RUSTFLAGS="${RUSTFLAGS:-} -C link-arg=-Wl,-z,max-page-size=16384"

cd "$here/jni"
cargo ndk "${targets[@]}" -o "$here/app/src/main/jniLibs" build --release
