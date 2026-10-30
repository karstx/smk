kb_dir = meson.current_source_dir()

keyboards += {
    'genesis-thor-300' : {
        'dir' : kb_dir,
        'platform' : 'sh68f881',
        'icp_part' : 'sh68f881',  # the part name sinodude programs it as, over ICP
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
            'product_id' : '0x001f',
            'led_rows' : '6',
            'led_cols' : '19',
            'led_cx2' : '17',
            'led_cy2' : '5',
        },
    },
}
