inc_base += meson.current_source_dir()

# defaults for the matrix hooks a board may leave out; the bootloader scans the
# matrix too, so it links the same fallbacks out of its own archive
src_user_matrix_defaults = files(
    'user_matrix_sinks_off.c',
    'user_matrix_scan_pre.c',
    'user_matrix_scan_post.c',
)

src_user += src_user_matrix_defaults

src_user += files(
    'user_init.c',
    'layout_process_record.c',
    'indicators_start.c',
    'indicators_pre_update.c',
    'indicators_update_step.c',
    'indicators_post_update.c',
    'indicators_init.c',
    'indicators_render.c',
    'indicators_apply_defaults.c',
    'indicators_validate_settings.c',
    'indicators_pwm_enable.c',
    'indicators_pwm_disable.c',
    'user_led_radial.c',
    'user_led_axis_x.c',
    'user_led_axis_y.c',
    'settings_save_pre.c',
    'settings_save_post.c',
)
