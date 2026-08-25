# GR5525 Stack ROM ABI

`rom_symbol_gcc_v1.0.3_patch_2.txt` is the GNU linker symbol table from the
official Goodix GR5525 SDK tag `v1.0.3_patch_2`, commit
`335fa6f3a860abe54b5f156781220d077e097ebb`:

`platform/soc/linker/gcc/rom_symbol_gcc.txt`

The SDK source is published at <https://github.com/goodix-ble/GR5525.SDK>.
The imported file SHA-256 is:

`BD3721BDCF60399431B149D946E76AA65B1549B5ED21D14344C7892C0E0218AA`

The matching soft-float `libble_sdk.a` SHA-256 is:

`9F3A6FEB733D691962AA44039D26187ADC28ED6877FDA599208EB0EE7B450E5B`

CMake verifies both hashes during configuration. Updating either artifact
requires importing the ROM symbol table and BLE library from the same official
SDK release, then updating both expected hashes together. A hash mismatch is a
hard configuration error because using a mismatched ROM ABI can branch to an
incorrect on-chip address at runtime.
