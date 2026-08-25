# Goodix GR5525 BLE Protocol Stack

`libble_sdk.a` is the unmodified soft-float protocol-stack library from the
official Goodix GR5525 SDK:

- Repository: <https://github.com/goodix-ble/GR5525.SDK>
- Tag: `v1.0.3_patch_2`
- Commit: `335fa6f3a860abe54b5f156781220d077e097ebb`
- Upstream path: `platform/soc/linker/gcc/libble_sdk.a`
- Size: `9601850` bytes
- Git blob: `1e8353d33f6242ba607835c3ec258f7ed3c626c6`
- SHA-256:
  `9F3A6FEB733D691962AA44039D26187ADC28ED6877FDA599208EB0EE7B450E5B`

The binary remains subject to the vendor SDK's applicable distribution terms.
CMake verifies its SHA-256 before every GR5525 BLE build and rejects any
mismatch with the paired Stack ROM symbol table.
