# SoC integration

Implemented SoC families are GD32F50x, Goodix GR5525x and STM32H7x. A catalog
entry must have a matching SoC Kconfig/CMake backend; there is no STM32F4
backend in this SDK.

The merged board/image DTS selects the SoC through its compatible property.
The EDT model, `soc.yml` and SoC Kconfig select the backend and architecture
settings. Public device access uses generated traits and SDK drivers.

Product hardware lives in `app/product/<product>/config/` and is shared by
App and Boot. Each image owns its overlays and Kconfig fragments.
See `doc/BOARD_CONFIGURATION.md`.
Vendor SDKs and hardware resources are preserved for future integration, even
when they have no current build consumer. Protected roots are recorded in
`tools/source-policy.json`; licensing and source digests are recorded in
`tools/dependencies.json`. Available source files do not imply a supported target.
