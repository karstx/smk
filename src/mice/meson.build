# Mice are standalone applications on sino51lib: they share the platform layer
# (clock, USB hardware, ISP jump, delays) but not the keyboard core.
mice += {
    'isy-igm3000' : {
        'platform' : 'sh68f881',
        'dir' : meson.current_source_dir() / 'isy-igm3000',
        'src' : files(
            'isy-igm3000/input.c',
            'isy-igm3000/led.c',
            'isy-igm3000/main.c',
            'isy-igm3000/sensor.c',
            'isy-igm3000/usb.c',
        ),
        # same VID:PID as the stock firmware, so sinowisp finds it unchanged
        'vendor_id' : '0x258a',
        'product_id' : '0x1007',
    },
}
