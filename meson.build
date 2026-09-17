project('smk', 'c', version : 'alpha', meson_version : '>=1.1.0')

env = environment()
env.set('DEBUG', '1')

default_options = {
    'watchdog_enable' : '1',
    'nvm' : 'flash',
    'vendor_id' : '0x258a',
    'product_id' : '0xdead',
}

dir_base = meson.current_source_dir()

inc_base = [dir_base]
src_smk = []
src_libc = []
src_kb = []
src_user = []
platforms = {}
wireless_modules = {}
nvm_modules = {}
keyboards = {}

subdir('src')

cc_model_args = [
    '--Werror',
    '--std-c23',
    '-mmcs51',
    '--model-large',
    '--stack-auto',
    '--xram-loc',
    '0x0000',
    '--opt-code-speed',
    '--out-fmt-ihx',
]

cc_base_args = cc_model_args + ['-DSMK_VERSION=@0@'.format(meson.project_version())]

# The bootloader has no ISRs and no reentrancy, and has to fit between the
# application and its own entry stub, so it trades the application's memory
# model for the small one.
cc_model_boot_args = [
    '--Werror',
    '--std-c23',
    '-mmcs51',
    '--model-small',
    '--xram-loc',
    '0x0000',
    '--opt-code-size',
    '--out-fmt-ihx',
]

if get_option('nkro').enabled()
    cc_base_args += '-DNKRO_ENABLE=1'
endif

if get_option('isp').enabled()
    cc_base_args += '-DISP_ENABLE=1'
endif

if get_option('sleep').enabled()
    cc_base_args += '-DSLEEP_ENABLE=1'
endif

if get_option('buildtype') == 'debug'
    cc_base_args += '-DDEBUG=1'
    cc_base_args += '--debug'  # emit SDCC .cdb for source-level tests (tests/sim.py)

    debug_sink = get_option('debug_sink')
    if debug_sink == 'console' or debug_sink == 'both'
        cc_base_args += '-DDEBUG_SINK_CONSOLE=1'
    endif
    if debug_sink == 'uart' or debug_sink == 'both'
        cc_base_args += '-DDEBUG_SINK_UART=1'
    endif
endif

sdar_args = ['-rc']

usb_string_defines = {
    'manufacturer' : 'USB_STR_MANUFACTURER',
    'product' : 'USB_STR_PRODUCT',
    'serial' : 'USB_STR_SERIAL',
}

cc = find_program('sdcc', required : true)
sdar = find_program('sdar', required : true)
packihx = find_program('packihx', required : true)
sinowisp = find_program('sinowisp', required : true)

# Only the *_full_flash targets need it -- writing the bootloader means ICP,
# because neither bootloader will write its own region -- so a tree without it
# still builds everything.
sinodude = find_program('sinodude', required : false)
if not sinodude.found()
    message('sinodude not found: skipping the *_full_flash targets (ICP writes)')
endif

# SDCC's -M family is deps-only, so a wrapper writes the depfile ninja needs.
py = find_program('python3', required : true)
sdcc_compile = join_paths(meson.current_source_dir(), 'utils/sdcc_compile.py')

# Fails the build on an unclaimed interrupt vector, or on an ISR-reachable function
# sharing SDCC's static overlay with a main-loop one (project_sdcc_isr_overlay_collision).
check_interrupts = files('utils/check_interrupts.py')

# Resolves the bootloader entry chord to matrix positions out of a board's layout,
# and splices a built bootloader and application into one full-flash image.
boot_chord_gen = files('utils/boot_chord_gen.py')
boot_image_gen = files('utils/boot_image.py')

# Fails the build if the linked bootloader does not start where reset lands or
# put its entry stub where the application's isp_jump() goes.
boot_check = files('utils/boot_check.py')

# Host-side generator that bakes per-device LED animation geometry into ROM tables
# (see utils/led_geometry_gen.c). Compiled once with the host compiler at configure
# time so the generated headers exist on disk before any firmware source is built.
host_cc = find_program('cc', 'gcc', required : true)
gen_led_geometry = join_paths(meson.current_build_dir(), 'led_geometry_gen')
run_command(host_cc, '-O2', '-o', gen_led_geometry, join_paths(meson.current_source_dir(), 'utils/led_geometry_gen.c'), check : true)

cc_incs = []
foreach dir : inc_base
    cc_incs += '-I' + dir
endforeach

compiler = generator(
    py,
    output : '@BASENAME@.rel',
    # depfile + wrapper: rebuild a .rel whenever one of its #included headers changes
    # (plain SDCC gives no header tracking, so header edits are otherwise silent).
    depfile : '@BASENAME@.d',
    arguments : [sdcc_compile, '@DEPFILE@', cc.full_path()] + cc_base_args + cc_incs + ['-c', '@INPUT@', '-o', '@OUTPUT@'],
)

rel_user = compiler.process(src_user + src_kb)
lib_user = custom_target(
    'user.lib',
    input : rel_user,
    output : 'user.lib',
    command : [sdar, sdar_args, '@OUTPUT@', '@INPUT@'],
)

foreach keyboard, board : keyboards
    platform = board['platform']
    if not platforms.has_key(platform)
        error('unsupported platform: @0@ (keyboard @1@)'.format(platform, keyboard))
    endif

    spec = platforms[platform]
    options = default_options + board['options']
    bootloader_addr = spec['flash_size'] - spec['bootloader_size']
    marker_addr = bootloader_addr - spec['sector_size']

    platform_cc_args = [
        '--xram-size',
        '@0@'.format(spec['xram_size']),
        '-DFREQ_SYS=@0@'.format(spec['freq_sys']),
    ]

    if spec['settings_store'] == 'flash'
        settings_addr = marker_addr - spec['sector_size']
        platform_cc_args += [
            '--code-size',
            '@0@'.format(settings_addr),
            '-DFLASH_CFG_ADDR=@0@u'.format(settings_addr),
            '-DFLASH_CFG_SIZE=@0@u'.format(spec['sector_size']),
            '-DFLASH_MARKER_ADDR=@0@u'.format(marker_addr),
        ]
    else
        # settings live outside the program flash, so code may run up to the marker sector
        platform_cc_args += ['--code-size', '@0@'.format(marker_addr)]
    endif

    # The bootloader is one image per board rather than per layout: it scans the
    # matrix, not a keymap, and the chord resolves to the same positions either way.
    build_boot = get_option('boot').enabled() and spec.has_key('boot_core') and board.has_key('boot_src')

    if build_boot
        isp_entry_addr = spec['flash_size'] - 0x100

        layout_names = board['layouts'].keys()
        chord_layout = layout_names.contains('default') ? 'default' : layout_names[0]
        chord_header = '@0@_boot_chord.h'.format(keyboard)

        configure_file(
            output : chord_header,
            command : [py, boot_chord_gen, board['layouts'][chord_layout]['dir'] / 'layout.c', get_option('boot_chord')],
            capture : true,
        )

        boot_args = cc_model_boot_args + [
            '--xram-size',
            '@0@'.format(spec['xram_size']),
            '--code-loc',
            '@0@'.format(bootloader_addr),
            # keeps the bootloader proper below the ISP entry stub the linker puts at isp_entry_addr
            '--code-size',
            '@0@'.format(isp_entry_addr - bootloader_addr),
            '-DFREQ_SYS=@0@'.format(spec['freq_sys']),
            '-DWATCHDOG_ENABLE=@0@'.format(options['watchdog_enable']),
            '-DFLASH_FW_SIZE=@0@'.format(bootloader_addr),
            '-DFLASH_SECTOR_SIZE=@0@'.format(spec['sector_size']),
            '-DISP_PAGE_SIZE=@0@'.format(spec['isp_page_size']),
            '-DXRAM_SIZE=@0@'.format(spec['xram_size']),
            '-DEA_BIT_ASM=@0@'.format(spec['ea_bit']),
            '-DKEYBOARD_NAME="@0@"'.format(keyboard),
            '-DAPP_USB_ID="@0@:@1@"'.format(options['vendor_id'], options['product_id']),
            '-DBOOT_CHORD_HEADER="@0@"'.format(chord_header),
        ]

        boot_incs = []
        foreach dir : inc_base + inc_boot + [spec['dir'], board['dir'], meson.current_build_dir()]
            boot_incs += '-I' + dir
        endforeach

        boot_compiler = generator(
            py,
            output : '@BASENAME@.rel',
            depfile : '@BASENAME@.d',
            arguments : [sdcc_compile, '@DEPFILE@', cc.full_path()] + boot_args + boot_incs + ['-c', '@INPUT@', '-o', '@OUTPUT@'],
        )

        boot_rel = boot_compiler.process(
            src_boot + src_startup + src_delay + src_ldo + src_reset + spec['boot_core'] + board['boot_src'],
        )

        # an archive, so a board that defines a matrix hook itself wins over the default
        lib_boot_user = custom_target(
            '@0@_boot_user.lib'.format(keyboard),
            input : boot_compiler.process(src_user_matrix_defaults),
            output : '@0@_boot_user.lib'.format(keyboard),
            command : [sdar, sdar_args, '@OUTPUT@', '@INPUT@'],
        )

        ihx_boot = custom_target(
            '@0@_boot.ihx'.format(keyboard),
            input : boot_rel,
            output : '@0@_boot.ihx'.format(keyboard),
            depends : [lib_boot_user],
            command : [
                cc,
                boot_args,
                '-Wl-bBOOTISP=@0@'.format(isp_entry_addr),
                '-o',
                '@OUTPUT@',
                '@INPUT@',
                '-l' + lib_boot_user.full_path(),
            ],
        )

        # The .hex depends on this, so a misplaced entry stub breaks the build.
        boot_placement = custom_target(
            '@0@_boot-check'.format(keyboard),
            input : ihx_boot,
            output : '@0@_boot.placement-ok'.format(keyboard),
            command : [
                py,
                boot_check,
                '--map',
                meson.current_build_dir() / '@0@_boot.map'.format(keyboard),
                '--base',
                '@0@'.format(bootloader_addr),
                '--entry',
                '@0@'.format(isp_entry_addr),
                '--stamp',
                '@OUTPUT@',
            ],
        )

        hex_boot = custom_target(
            '@0@_boot.hex'.format(keyboard),
            input : ihx_boot,
            output : '@0@_boot.hex'.format(keyboard),
            capture : true,
            install : true,
            install_dir : 'firmware',
            depends : boot_placement,
            command : [packihx, '@INPUT@'],
        )

        message('target available: @0@_boot.hex'.format(keyboard))
    endif

    foreach layout, variant : board['layouts']
        src_main = src_smk + src_libc + spec['src']
        inc_dirs = inc_base + [spec['dir']]

        cc_args = cc_base_args + platform_cc_args + [
            '-DWATCHDOG_ENABLE=@0@'.format(options['watchdog_enable']),
            '-DUSB_VID=@0@'.format(options['vendor_id']),
            '-DUSB_PID=@0@'.format(options['product_id']),
            '-DKEYBOARD_NAME="@0@"'.format(keyboard),
            '-DLAYOUT_NAME="@0@"'.format(layout),
        ]

        foreach key, define : usb_string_defines
            value = get_option('usb_' + key)
            if value == ''
                value = options.get(key, '')
            endif
            if value != ''
                cc_args += '-D@0@="@1@"'.format(define, value)
            endif
        endforeach

        wireless = options.get('wireless', '')
        if wireless != ''
            if not wireless_modules.has_key(wireless)
                error('unsupported wireless module: @0@'.format(wireless))
            endif
            mod = wireless_modules[wireless]
            src_main += mod['src']
            inc_dirs += mod['inc']
            cc_args += mod['args']
        endif

        nvm = options.get('nvm', '')
        if nvm != ''
            if not nvm_modules.has_key(nvm)
                error('unsupported nvm: @0@'.format(nvm))
            endif
            mod = nvm_modules[nvm]
            src_main += mod['src']
            inc_dirs += mod['inc']
            cc_args += mod['args']
        endif

        src_main += board['src'] + variant['src']
        inc_dirs += [board['dir'], variant['dir']]

        prefix = '@0@_@1@_'.format(keyboard, layout)

        # LED boards bake their animation geometry into ROM tables at configure time:
        # a header with the matrix sizes (for the board's _Static_assert) and a source
        # file with the tables + user_led_* accessors the shared effect engine calls.
        geom_src = []
        if options.has_key('led_cols')
            configure_file(
                output : prefix + 'led_geometry.h',
                command : [gen_led_geometry, 'header', options['led_rows'], options['led_cols'], options['led_cx2'], options['led_cy2']],
                capture : true,
            )
            geom_src += configure_file(
                output : prefix + 'led_geometry.c',
                command : [gen_led_geometry, 'source', options['led_rows'], options['led_cols'], options['led_cx2'], options['led_cy2']],
                capture : true,
            )
            cc_args += '-DLED_GEOMETRY_HEADER="@0@led_geometry.h"'.format(prefix)
            cc_args += '-I' + meson.current_build_dir()
        endif

        cc_incs = []
        foreach dir : inc_dirs
            cc_incs += '-I' + dir
        endforeach

        compiler = generator(
            py,
            output : '@BASENAME@.rel',
            # See the user-lib compiler above: wrapper + depfile so ninja rebuilds a
            # .rel whenever one of its #included headers changes.
            depfile : '@BASENAME@.d',
            arguments : [sdcc_compile, '@DEPFILE@', cc.full_path()] + cc_args + cc_incs + ['-c', '@INPUT@', '-o', '@OUTPUT@'],
        )

        rel_main = compiler.process(src_main, geom_src)

        ihx_smk_target_name = '@0@smk.ihx'.format(prefix)
        ihx_smk = custom_target(
            ihx_smk_target_name,
            input : rel_main,
            output : ihx_smk_target_name,
            depends : [lib_user],
            command : [cc, cc_args, '-o', '@OUTPUT@', '@INPUT@', '-l' + lib_user.full_path()],
        )

        # The .hex depends on this, so a violation breaks the build.
        interrupt_check = custom_target(
            '@0@interrupt-check'.format(prefix),
            input : ihx_smk,
            output : '@0@smk.interrupts-ok'.format(prefix),
            command : [
                py,
                check_interrupts,
                '--src',
                spec['dir'],
                '--asmdir',
                meson.current_build_dir() / '@0@smk.ihx.p'.format(prefix),
                '--map',
                meson.current_build_dir() / '@0@smk.map'.format(prefix),
                '--stamp',
                '@OUTPUT@',
            ],
        )

        hex_smk_target_name = '@0@smk.hex'.format(prefix)
        hex_smk = custom_target(
            hex_smk_target_name,
            input : ihx_smk,
            output : hex_smk_target_name,
            capture : true,
            install : true,
            install_dir : 'firmware',
            depends : interrupt_check,
            command : [packihx, '@INPUT@'],
        )

        if build_boot
            hex_full = custom_target(
                '@0@full.hex'.format(prefix),
                input : [hex_smk, hex_boot],
                output : '@0@full.hex'.format(prefix),
                capture : true,
                install : true,
                install_dir : 'firmware',
                command : [
                    py,
                    boot_image_gen,
                    '@INPUT0@',
                    '@INPUT1@',
                    '@0@'.format(spec['flash_size']),
                    '@0@'.format(spec['bootloader_size']),
                ],
            )

            if sinodude.found() and board.has_key('icp_part')
                # Writes the whole code flash, stock bootloader included. Code
                # options are left alone; only ICP can put this image on a board.
                run_target(
                    '@0@full_flash'.format(prefix),
                    command : [
                        sinodude,
                        'write',
                        '--flash',
                        hex_full.full_path(),
                        '-c',
                        'sinodude-serial',
                        '-p',
                        board['icp_part'],
                        '--port',
                        get_option('icp_port'),
                    ],
                    depends : hex_full,
                )

                message('target available: @0@full.hex, flash: @0@full_flash'.format(prefix))
            else
                message('target available: @0@full.hex'.format(prefix))
            endif
        endif

        flash_target_name = '@0@flash'.format(prefix)
        run_target(
            flash_target_name,
            command : [sinowisp, 'write', '-d', keyboard, '--force', hex_smk.full_path()],  # TODO: try to remove the need for --force
            depends : hex_smk,
        )

        message('target available: @0@, flash: @1@'.format(hex_smk_target_name, flash_target_name))
    endforeach
endforeach

example_hexes = []

foreach example : sino51lib_examples
    foreach platform : example['parts']
        spec = platforms[platform]

        ex_args = cc_model_args + [
            '--xram-size',
            '@0@'.format(spec['xram_size']),
            '-DFREQ_SYS=@0@'.format(spec['freq_sys']),
            '--code-size',
            '@0@'.format(spec['flash_size'] - spec['bootloader_size'] - spec['sector_size']),
            '-DWATCHDOG_ENABLE=1',
        ] + example['args']

        ex_incs = ['-I' + dir_base, '-I' + spec['dir'], '-I' + dir_base / 'src/sino51lib']

        ex_prefix = 'ex-@0@-@1@'.format(example['name'], platform)

        ex_compiler = generator(
            py,
            output : '@BASENAME@.rel',
            depfile : '@BASENAME@.d',
            arguments : [sdcc_compile, '@DEPFILE@', cc.full_path()] + ex_args + ex_incs + ['-c', '@INPUT@', '-o', '@OUTPUT@'],
        )

        ex_src = example['src'] + spec['example_core']
        if example.has_key('extra')
            ex_src += spec[example['extra']]
        endif

        ex_rel = ex_compiler.process(ex_src)

        ex_ihx = custom_target(
            ex_prefix + '.ihx',
            input : ex_rel,
            output : ex_prefix + '.ihx',
            command : [cc, ex_args, '-o', '@OUTPUT@', '@INPUT@'],
        )

        example_hexes += custom_target(
            ex_prefix + '.hex',
            input : ex_ihx,
            output : ex_prefix + '.hex',
            capture : true,
            command : [packihx, '@INPUT@'],
        )
    endforeach
endforeach

alias_target('examples', example_hexes)

# Simulator-based firmware tests, run in the patched uCsim simulator (flake
# package ucsim-sh68f90). Discovers every tests/test_*.py so new test files are
# picked up automatically. Only registered when both the simulator and python3
# are available; the suite itself skips if no firmware .hex has been built yet.
sim = find_program('ucsim-sh68f90', required : false)
py = find_program('python3', required : false)
if sim.found() and py.found()
    test(
        'sim-tests',
        py,
        args : ['-m', 'unittest', 'discover', '-s', join_paths(dir_base, 'tests'), '-t', join_paths(dir_base, 'tests')],
        env : {'SMK_UCSIM' : sim.full_path()},
        # full suite is ~30s locally and most tests boot through real init;
        # bump well above the meson 30s default so slower CI runners pass.
        timeout : 300,
    )
endif
