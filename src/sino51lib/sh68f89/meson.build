src_sh68f89 = files(
    'clock.c',
    'diag_info.c',
    'usbhw.c',
    'peripherals.c',
    'power.c',
    'extint.c',
    'systick.c',
) + src_pca

if get_option('buildtype') == 'debug'
    src_sh68f89 += src_flash
endif

platforms += {
    'sh68f89' : {
        'dir' : meson.current_source_dir(),
        'example_core' : files('clock.c', 'peripherals.c'),
        'example_usbhw' : files('usbhw.c'),
        'example_systick' : files('systick.c'),
        'example_power' : files('power.c', 'extint.c'),
        'boot_core' : src_flash + files('clock.c', 'peripherals.c'),
        'src' : src_sh68f89,
        'flash_size' : 65536,
        'bootloader_size' : 4096,
        'sector_size' : 1024,
        'isp_page_size' : 2048,
        'ea_bit' : 0xaf,
        'xram_size' : 2816,
        'freq_sys' : 24000000,
        'settings_store' : 'external',
    },
}
