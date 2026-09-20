if(CONFIG_MCUBOOT)
  zephyr_library()
  zephyr_library_sources(boot_watchdog.c)
endif()
