#if !defined(CONFIG_EXTERNAL_MODULE_PROBE)
#error External Kconfig did not reach the external component
#endif
int external_module_probe() { return 42; }
