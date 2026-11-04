inc_base += meson.current_source_dir()

src_smk += files(
    'host.c',
    'usb.c',
    'matrix.c',
    'report.c',
    'keyboard.c',
    'console.c',
    'debug_sink.c',
    'settings.c',
    'led_effect.c',
    'sleep.c',
    'tick.c',
)

if get_option('via').enabled()
    src_smk += files(
        'dynamic_keymap.c',
        'raw_hid.c',
        'via.c',
    )
endif
