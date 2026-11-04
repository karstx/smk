kb_dir = meson.current_source_dir()

keyboards += {
    'irok-al87pro' : {
        'dir' : kb_dir,
        'platform' : 'sh68f90',
        'src' : files(
            'user_init.c',
            'user_matrix.c',
            'user_sleep.c',
            'kb.c',
            'byk916_bk3632_link.c',
        ),
        'boot_src' : files('user_matrix.c', 'user_init.c'),
        'layouts' : {
            'default' : {
                'dir' : kb_dir / 'layouts/default',
                'src' : files(
                    'layouts/default/layout.c',
                    'layouts/default/indicators.c',
                ),
            },
        },
        'options' : {
            # Experimental PID pending confirmation of the keyboard's stock USB IDs.
            'vendor_id' : '0x1209',
            'product_id' : '0x9160',
            'manufacturer' : 'IROK',
            'product' : 'IROK AL87 Pro',
            'dynamic_keymap_layers' : '2',
            'dynamic_keymap_sectors' : '1',
            'dynamic_keymap_storage_version' : '2',
            'dynamic_macro_count' : '16',
            'compact_nkro_ep1' : '1',
            'fast_led_scan' : '1',
            'via_custom_rgb' : '1',
            'per_key_rgb' : '1',
            'byk916_bk3632_link' : '1',
            'via' : '1',
            'led_rows' : '6',
            'led_cols' : '17',
            'led_cx2' : '16',
            'led_cy2' : '5',
        },
    },
}
