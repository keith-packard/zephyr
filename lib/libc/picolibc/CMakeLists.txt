# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(libc-hooks.c)

if(CONFIG_PICOLIBC AND NOT CONFIG_PICOLIBC_USE_MODULE)

  # Use picolibc provided with the toolchain

  zephyr_compile_options(-D_POSIX_C_SOURCE=200809 --specs=picolibc.specs)
  zephyr_link_libraries(-T/dev/null --specs=picolibc.specs c -lgcc)
  if(CONFIG_PICOLIBC_IO_FLOAT)
    zephyr_compile_options(-DPICOLIBC_DOUBLE_PRINTF_SCANF)
    zephyr_link_libraries(-DPICOLIBC_DOUBLE_PRINTF_SCANF)
  else()
    zephyr_compile_options(-DPICOLIBC_INTEGER_PRINTF_SCANF)
    zephyr_link_libraries(-DPICOLIBC_INTEGER_PRINTF_SCANF)
  endif()

endif()
