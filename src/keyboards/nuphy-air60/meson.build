kb_dir = meson.current_source_dir()

keyboards += {
    'nuphy-air60' : {
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
            'vendor_id' : '0x05ac',
            'product_id' : '0x024f',
            'wireless' : 'bk3632',
            'led_rows' : '5',
            'led_cols' : '16',
            'led_cx2' : '13',
            'led_cy2' : '4',
        },
    },
}
