inc_base += meson.current_source_dir()

src_bb_i2c = files('bb_i2c.c')

src_spi = files('spi.c')
src_twi = files('twi.c')

src_pwm = files('pwm.c')
src_pca = files('pca.c')

src_flash = files('flash.c')

src_startup = files('startup.c')
src_delay = files('delay.c')
src_uart = files('uart.c')
src_ldo = files('ldo.c')
src_reset = files('reset.c')
src_isp = files('isp.c')

src_libc_files = ['startup.c', 'delay.c', 'uart.c']

if get_option('isp').enabled()
    src_libc_files += 'isp.c'
endif

src_libc_files += ['ldo.c', 'reset.c', 'diag.c', 'interrupts.c']
src_libc += files(src_libc_files)

subdir('sh68f90')
subdir('sh68f89')
subdir('sh68f881')

subdir('boot')

subdir('examples')
