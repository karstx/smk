#!/usr/bin/env python3
"""Talk to the ISY IGM3000 firmware's diag/command feature reports over hidraw.

Linux only; needs read/write access to the mouse's hidraw node (root or a udev rule).

  igm3000-diag.py                 print diag once
  igm3000-diag.py watch           print diag whenever buttons/wheel/DPI/LED change
  igm3000-diag.py led R G B       force the RGB channels (0-255 each), e.g. 255 0 0
  igm3000-diag.py led off         clear the LED override
  igm3000-diag.py dpi N           set DPI level 0..5
  igm3000-diag.py mode N          LED mode 0=rainbow 1=breathe 2=static 3=off
"""
import fcntl
import glob
import os
import sys
import time


def ioc(nr, size):
    return (3 << 30) | (size << 16) | (ord("H") << 8) | nr


def find_node():
    for d in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        try:
            uevent = open(os.path.join(d, "device/uevent")).read()
        except OSError:
            continue
        if "0000258A:00001007" not in uevent.upper():
            continue
        iface_dir = os.path.basename(os.path.dirname(os.path.realpath(os.path.join(d, "device"))))
        if iface_dir.endswith(".1"):  # e.g. 1-4:1.1 = configuration 1, interface 1
            return "/dev/" + os.path.basename(d)
    sys.exit("IGM3000 interface 1 not found (is the SMK firmware running?)")


def get_diag(fd):
    buf = bytearray(18)
    buf[0] = 7
    fcntl.ioctl(fd, ioc(0x07, len(buf)), buf, True)
    return buf


def send_cmd(fd, *args):
    buf = bytearray(8)
    buf[0] = 8
    for i, a in enumerate(args):
        buf[1 + i] = a & 0xFF
    fcntl.ioctl(fd, ioc(0x06, len(buf)), buf, True)


def show(d):
    flags = d[10]
    s8 = lambda v: v - 256 if v > 127 else v
    return (f"sensor type={d[1]} pid=0x{d[2]:02x} | buttons raw={d[3]:08b} stable={d[4]:08b} | "
            f"wheel={s8(d[5])} | dpi level={d[6]} code={d[7]} | led mode={d[8]} override={d[9]} | "
            f"usb cfg={flags & 1} boot={flags >> 3 & 1} | fw=v{d[11]} | motion events={d[12] | d[13] << 8} "
            f"last dx={s8(d[14])} dy={s8(d[15])} | ms={d[16] | d[17] << 8}")


def main():
    node = find_node()
    fd = os.open(node, os.O_RDWR)
    a = sys.argv[1:]
    if not a:
        print(node, show(get_diag(fd)))
    elif a[0] == "watch":
        last = None
        while True:
            d = get_diag(fd)
            key = bytes(d[3:9])  # buttons, wheel, dpi, led mode only
            if key != last:
                print(time.strftime('%H:%M:%S'), show(d), flush=True)
                last = key
            time.sleep(0.05)
    elif a[0] == "led" and a[1:] == ["off"]:
        send_cmd(fd, 0)
    elif a[0] == "led":
        send_cmd(fd, 1, int(a[1]), int(a[2]), int(a[3]))
    elif a[0] == "dpi":
        send_cmd(fd, 2, int(a[1]))
    elif a[0] == "mode":
        send_cmd(fd, 3, int(a[1]))
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
