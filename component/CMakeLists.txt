# Component libraries
add_subdirectory_ifdef(CONFIG_BLE_GR5525 ble/goodix)
add_subdirectory_ifdef(CONFIG_NVS nvs)
