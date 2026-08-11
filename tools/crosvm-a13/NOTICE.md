# Bundled crosvm provenance

The bundled ARM64 binary is built from AOSP Android 13 sources:

- `platform/external/crosvm` commit `d5b0ee8806620d92da18368137882d3839677a9e`
- `platform/external/minijail` commit `8910803d95f549371f5b3057d5e7d9d6d6a7d826`
- Rust toolchain `1.90.0`
- Ubuntu Jammy ARM64 `libcap-dev` is used only while statically linking.

`build.sh`, the pinned `Cargo.lock`, and `crosvm-a13.patch` reproduce the
binary. The patch makes Linux syslog optional on Android, fixes a mutable KVM
ioctl fd write-back exposed by modern LTO, and pins old wildcard dependencies.

crosvm and minijail are distributed under their upstream BSD-style licenses.
The complete corresponding source is available from the AOSP repositories at
the commits above.
