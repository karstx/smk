kb_dir = meson.current_source_dir()

keyboards += {
    'eyooso-z11' : {
        'dir' : kb_dir,
        'platform' : 'sh68f90',
        'src' : files(
            'user_matrix.c',
            'user_init.c',
            'user_sleep.c',
            'kb.c',
        ),
        'boot_src' : files('user_matrix.c', 'user_init.c'),
        'layouts' : {
            'default' : {
                'dir' : kb_dir / 'layouts/default',
                'src' : files(
                    'layouts/default/indicators.c',
                    'layouts/default/layout.c',
                ),
            },
        },
        'options' : {
            'vendor_id' : '0x258a',
            'product_id' : '0x002a',
            'led_rows' : '5',
            'led_cols' : '14',
            'led_cx2' : '13',
            'led_cy2' : '4',
        },
    },
}
