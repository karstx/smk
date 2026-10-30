kb_dir = meson.current_source_dir()

keyboards += {
    'irok-fe75' : {
        'dir' : kb_dir,
        'platform' : 'sh68f90',
        'src' : files(
            'user_matrix.c',
            'user_init.c',
            'user_sleep.c',
            'kb.c',
        ),
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
            'product_id' : '0x00fc',
            'wireless' : 'bk3632',
            'led_rows' : '6',
            'led_cols' : '16',
            'led_cx2' : '15',
            'led_cy2' : '5',
        },
    },
}
