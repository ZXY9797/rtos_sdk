"""CMake cache inputs that define the identity and contents of an SDK image."""


def image_cache_reset_args():
    # Keep compiler detection and build outputs. A later configure selects its
    # own product, image, inputs and linker options from its current arguments.
    keys = (
        "p", "t", "PROFILE", "BOARD", "BOARD_ROOT", "APPLICATION", "f", "FIRMWARE_TYPE",
        "CONFIG_DIR", "CONF_FILE", "EXTRA_CONF_FILE", "DTC_OVERLAY_FILE", "MODULE_ROOTS",
        "DTS_ROOT", "DTS_EXTRA_CPPFLAGS", "EXTRA_DTC_FLAGS", "BOOT_LAYOUT", "LINKER_SCRIPT",
        "CMAKE_EXE_LINKER_FLAGS", "LOADER_PAYLOAD_FILE", "SDK_APPLICATION_ID",
        "SDK_RESOURCE_BASELINE", "SDK_RTOS_BACKEND",
    )
    return [f"-U{key}" for key in keys]
