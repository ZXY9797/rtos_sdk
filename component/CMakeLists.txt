# Build the pure calculation archives when a firmware target links them.
add_subdirectory(algo EXCLUDE_FROM_ALL)
add_subdirectory(motor EXCLUDE_FROM_ALL)
add_subdirectory(ipc)
add_subdirectory_ifdef(CONFIG_BLE_GR5525 ble/goodix)
add_subdirectory_ifdef(CONFIG_FOC motor/drive)
add_subdirectory_ifdef(CONFIG_LINK link)
add_subdirectory_ifdef(CONFIG_NVS nvs)
