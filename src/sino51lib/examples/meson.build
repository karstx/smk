
sino51lib_examples = [
    {
        'name' : 'gpio_blink',
        'parts' : ['sh68f90', 'sh68f89', 'sh68f881'],
        'src' : files('gpio_blink/main.c') + src_startup + src_delay + src_ldo + src_reset,
        'args' : [],
    },
    {
        'name' : 'uart_hello',
        'parts' : ['sh68f90', 'sh68f89', 'sh68f881'],
        'src' : files('uart_hello/main.c') + src_startup + src_delay + src_ldo + src_uart,
        'args' : ['-DDEBUG_SINK_UART=1'],
    },
    {
        'name' : 'spi_xfer',
        'parts' : ['sh68f90', 'sh68f89', 'sh68f881'],
        'src' : files('spi_xfer/main.c') + src_startup + src_delay + src_ldo + src_spi,
        'args' : ['-I' + meson.current_source_dir() / 'spi_xfer'],
    },
    {
        'name' : 'twi_probe',
        'parts' : ['sh68f89'],
        'src' : files('twi_probe/main.c') + src_startup + src_delay + src_ldo + src_twi,
        'args' : ['-I' + meson.current_source_dir() / 'twi_probe'],
    },
    {
        'name' : 'pwm_duty',
        'parts' : ['sh68f90'],
        'src' : files('pwm_duty/main.c') + src_startup + src_delay + src_ldo + src_pwm,
        'args' : [],
    },
    {
        'name' : 'pca_duty',
        'parts' : ['sh68f89'],
        'src' : files('pca_duty/main.c') + src_startup + src_delay + src_ldo + src_pca,
        'args' : [],
    },
    {
        'name' : 'usb_minimal',
        'parts' : ['sh68f90', 'sh68f89', 'sh68f881'],
        'src' : files('usb_minimal/main.c') + src_startup + src_delay + src_ldo,
        'extra' : 'example_usbhw',
        'args' : ['-I' + meson.current_source_dir() / 'usb_minimal'],
    },
    {
        'name' : 'systick_tick',
        'parts' : ['sh68f90', 'sh68f89', 'sh68f881'],
        'src' : files('systick_tick/main.c') + src_startup + src_delay + src_ldo,
        'extra' : 'example_systick',
        'args' : ['-I' + meson.current_source_dir() / 'systick_tick'],
    },
    {
        'name' : 'bb_i2c_probe',
        'parts' : ['sh68f90', 'sh68f89', 'sh68f881'],
        'src' : files('bb_i2c_probe/main.c') + src_startup + src_delay + src_ldo + src_bb_i2c,
        'args' : ['-I' + meson.current_source_dir() / 'bb_i2c_probe'],
    },
    {
        'name' : 'isp_powerdown',
        'parts' : ['sh68f90', 'sh68f89', 'sh68f881'],
        'src' : files('isp_powerdown/main.c') + src_startup + src_delay + src_ldo + src_isp,
        'extra' : 'example_power',
        'args' : ['-I' + meson.current_source_dir() / 'isp_powerdown'],
    },
    {
        'name' : 'flash_info',
        'parts' : ['sh68f90', 'sh68f89', 'sh68f881'],
        'src' : files('flash_info/main.c') + src_startup + src_delay + src_ldo + src_flash,
        'args' : [],
    },
]

# Built by the bootloader loop rather than the one above: a bootloader is linked
# at the top of flash, around the ISP entry stub, not at 0x0000.
sino51lib_boot_examples = [
    {
        'name' : 'boot_minimal',
        'parts' : ['sh68f90', 'sh68f89', 'sh68f881'],
        'src' : files('boot_minimal/entry.c'),
        'args' : [],
    },
]
