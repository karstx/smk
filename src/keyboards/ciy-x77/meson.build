kb_dir = meson.current_source_dir()

keyboards += {
    'ciy-x77' : {
        'dir' : kb_dir,
        'platform' : 'sh68f89',
        'icp_part' : 'sh68f89',  # the part name sinodude programs it as, over ICP
        'src' : files(
            'indicators.c',
            'user_matrix.c',
            'user_init.c',
            'user_sleep.c',
            'kb.c',
        ),
        'boot_src' : files('user_matrix.c', 'user_init.c'),
        'layouts' : {
            'default' : {
                'dir' : kb_dir / 'layouts/default',
                'src' : files('layouts/default/layout.c'),
            },
            'iso' : {
                'dir' : kb_dir / 'layouts/iso',
                'src' : files('layouts/iso/layout.c'),
            },
        },
        'options' : {
            'vendor_id' : '0x258a',
            'product_id' : '0x0016',
            'nvm' : '24cxx',
            # the stock bootloader offsets every page byte, and flashing tools carry that for this board
            'boot_stock_scramble' : '1',
            'led_rows' : '6',
            'led_cols' : '18',
            'led_cx2' : '17',
            'led_cy2' : '5',
        },
    },
}
