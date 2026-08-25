# GR5525 Stack ROM ABI

`rom_symbol_gcc_v1.0.3_patch_2.txt` is the GNU linker symbol table from the
official Goodix GR5525 SDK tag `v1.0.3_patch_2`, commit
`335fa6f3a860abe54b5f156781220d077e097ebb`:

`platform/soc/linker/gcc/rom_symbol_gcc.txt`

The SDK source is published at <https://github.com/goodix-ble/GR5525.SDK>.
The imported file canonical SHA-256 is:

`A2F1AD8517F00398BB71938CC66F3162F3F082D38A47677D326A80762A1EC72D`

CMake normalizes CRLF line endings to LF before hashing so Git checkout
settings cannot change the integrity result. Bare CR line endings are rejected.

The matching soft-float `libble_sdk.a` SHA-256 is:

`9F3A6FEB733D691962AA44039D26187ADC28ED6877FDA599208EB0EE7B450E5B`

The library is imported from the same commit at:

`platform/soc/linker/gcc/libble_sdk.a`

The exact upstream binary is tracked at
`component/ble/goodix/lib/libble_sdk.a` so clean and offline checkouts are
reproducible. See the adjacent `README.md` for provenance and redistribution
scope.

All GR5525 images require GNU Arm C and C++ compiler version 9.3.1. CMake
rejects other compiler versions even if they can link the precompiled library.

CMake verifies both hashes during configuration. Updating either artifact
requires importing the ROM symbol table and BLE library from the same official
SDK release, then updating both expected hashes together. A hash mismatch is a
hard configuration error because using a mismatched ROM ABI can branch to an
incorrect on-chip address at runtime.
