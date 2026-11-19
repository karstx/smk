"""Minimal SH68F881 (8051) emulator for pre-flash testing of the ISY IGM3000 firmware.

Models just enough hardware to exercise the firmware:
  * 8051 core (all opcodes), IRAM/SFR/XRAM/code
  * SFR paging via INSCON.6 (page-1 registers: P5-P8, Timer3/4, USB block)
  * Timer2 interrupt (vector 0x2B) and USB interrupt (vector 0x5B)
  * port pins with pull-ups, read-modify-write vs pin reads
  * a fake bit-banged PixArt-style sensor on P2.4/P2.5/P2.6/P2.7
  * a fake USB host that talks to the EP0/EP1/EP2 registers and buffers
Timing is approximate (cycles ~ instruction length + branch/movx extras).
"""

FREQ = 12_000_000
T2_DIV = 6        # Timer2 counts at FREQ/6 = 2 MHz (measured on the IGM3000)
HOST_POLL = 12000 # host collects EP1/EP2 IN data once per ms (bInterval 1)

# SFR addresses that are banked by INSCON.6 (page 1 has its own storage)
PAGED = {0x80, 0x90, 0xA0, 0xB0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE9, 0xEA, 0xEB, 0xEC,
         0x88, 0x8C, 0x8D, 0xC8, 0xCC, 0xCD,
         0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 0xA5, 0xA6, 0xA7,
         0xC0, 0xD8, 0xE8, 0xF8}

ACC, B, PSW, SP, DPL, DPH = 0xE0, 0xF0, 0xD0, 0x81, 0x82, 0x83
INSCON, IEN0, IEN1, T2CON, RSTSTAT = 0x86, 0xA8, 0xA9, 0xC8, 0xB1
# page-1 USB registers
USBCON, USBIF1, USBIF2, USBIE1, USBIE2 = 0x99, 0xE8, 0xF8, 0x9A, 0x9B
EP0CON, EP1CON, EP2CON = 0x98, 0xC0, 0xD8
IEP0CNT, IEP1CNT, IEP2CNT, OEP0CNT = 0x9D, 0x9E, 0x9F, 0xA5
EP0_OUT, EP0_IN, EP1_IN, EP2_IN = 0x0A00, 0x0A08, 0x0A20, 0x0A70

PORT_CR = {0x80: 0xE1, 0x90: 0xE2, 0xA0: 0xE3, 0xB0: 0xE4, 0xC0: 0xE5}
PORT_PCR = {0x80: 0xE9, 0x90: 0xEA, 0xA0: 0xEB, 0xB0: 0xEC, 0xC0: 0xED}


class Halt(Exception):
    pass


def load_ihex(path):
    mem = bytearray(0x10000)
    used = []
    base = 0
    for line in open(path):
        line = line.strip()
        if not line.startswith(":"):
            continue
        raw = bytes.fromhex(line[1:])
        n, addr, typ = raw[0], raw[1] << 8 | raw[2], raw[3]
        data = raw[4:4 + n]
        if typ == 0:
            a = base + addr
            mem[a:a + n] = data
            used.append((a, a + n))
        elif typ == 4:
            base = (data[0] << 8 | data[1]) << 16
    return mem, used


class FakeSensor:
    """Bit-banged SPI slave: 4-wire (MOSI P2.5 / MISO P2.7) or 3-wire (SDIO P2.7)."""

    def __init__(self, kind):
        self.kind = kind  # 1, 2, 4, 11 or None
        pid = {1: 0x12, 2: 0x09, 4: 0x3B, 11: 0x43}.get(kind)
        self.regs = bytearray(128)
        if pid is not None:
            self.regs[0] = pid
        self.four_wire = kind == 2
        self.ncs = 1
        self.sck = 1
        self.state = "idle"
        self.bits = 0
        self.sh = 0
        self.waddr = 0
        self.out = []
        self.out_bits = 0
        self.drive = None  # None = not driving the data line, else 0/1
        self.writes = []   # (reg, value) log
        self.reads = []
        self.motion = None  # (dx, dy) pending

    def set_motion(self, dx, dy):
        self.motion = (dx, dy)

    def _read_values(self, addr):
        r = self.regs
        if self.kind is None:
            return [0xFF]
        if addr == 0x16 and self.kind == 11:
            if self.motion:
                dx, dy = self.motion
                self.motion = None
                return [0x80, 0x01, dx & 0xFF, (dx >> 8) & 0xFF, dy & 0xFF, (dy >> 8) & 0xFF]
            return [0x00, 0x01, 0, 0, 0, 0]
        if addr == 0x02 and self.kind in (1, 2, 4):
            if self.motion:
                dx, dy = self.motion
                self.motion = None
                r[3] = dx & 0xFF
                r[4] = dy & 0xFF
                r[0x0C] = ((dx >> 4) & 0xF0) | ((dy >> 8) & 0x0F)
                return [0x80]
            r[3] = r[4] = r[0x0C] = 0
            return [0x00]
        return [r[addr]]

    def update(self, ncs, sck, mosi_p25, sdio_p27):
        if ncs and not self.ncs:
            self.state = "idle"
            self.drive = None
        if not ncs and self.ncs:
            self.state = "addr"
            self.bits = 0
            self.sh = 0
            self.drive = None
        self.ncs = ncs
        if ncs or self.kind is None:
            self.sck = sck
            return
        if sck and not self.sck and self.state in ("addr", "wdata"):
            bit = mosi_p25 if self.four_wire else sdio_p27
            self.sh = ((self.sh << 1) | bit) & 0xFF
            self.bits += 1
            if self.bits == 8:
                self.bits = 0
                b = self.sh
                if self.state == "addr":
                    if b & 0x80:
                        self.state = "wdata"
                        self.waddr = b & 0x7F
                    else:
                        self.reads.append(b)
                        self.state = "rdata"
                        self.out = self._read_values(b)
                        self.out_bits = 0
                else:
                    self.regs[self.waddr] = b
                    self.writes.append((self.waddr, b))
                    self.state = "addr"
        elif not sck and self.sck and self.state == "rdata":
            if not self.out:
                self.out = [0]
            byte = self.out[0]
            self.drive = (byte >> (7 - self.out_bits)) & 1
            self.out_bits += 1
            if self.out_bits == 8:
                self.out_bits = 0
                self.out.pop(0)
        self.sck = sck


class CPU:
    def __init__(self, code, sensor_kind=None, idle_pc=None):
        self.code = code
        self.idle_pc = idle_pc  # main-loop 'nothing to do' branch, fast-forwarded
        self.iram = bytearray(256)
        self.sfr = bytearray(256)
        self.sfr1 = bytearray(256)
        self.xram = bytearray(0x10000)
        self.pc = 0
        self.cycles = 0
        self.t2_acc = 0
        self.next_poll = HOST_POLL
        self.isr_depth = 0
        self.sfr[SP] = 7
        for p in (0x80, 0x90, 0xA0, 0xB0, 0xC0):
            self.sfr[p] = 0xFF
            self.sfr1[p] = 0xFF
        self.buttons_low = set()  # (port_addr, page, bit) held low externally
        self.matrix_low = set()   # button bits 5..7: P2.1..P2.3 low only while P3.0 is low
        self.wheel = (1, 1)       # P0.7, P0.6 levels
        self.sensor = FakeSensor(sensor_kind)
        self.last_kick = 0
        self.max_kick_gap = 0
        self.ep1_reports = []
        self.ep2_reports = []
        self.trace_pc = None
        self.isp_hit = None
        # smk delay_iters() body: kick (75 b1 02) + 16 nops + djnz r6 -> fast-forwarded
        pat = bytes([0x75, 0xB1, 0x02]) + b"\x00" * 16 + bytes([0xDE])
        i = bytes(code).find(pat)
        self.delay_loop = i if i >= 0 else None
        self._build()

    # ---------------------------------------------------------------- memory
    def page(self):
        return (self.sfr[INSCON] >> 6) & 1

    def _sfr_store(self, a):
        return self.sfr1 if (a in PAGED and self.page()) else self.sfr

    def pin(self, port, bit, page=0):
        store = self.sfr1 if page else self.sfr
        latch = (store[port] >> bit) & 1
        cr = (store[PORT_CR.get(port, 0)] >> bit) & 1 if port in PORT_CR else 0
        if cr:
            return latch
        if (port, page, bit) in self.buttons_low:
            return 0
        if page == 0 and port == 0xA0 and bit in (1, 2, 3) and (bit + 4) in self.matrix_low:
            p30_driven_low = (self.sfr[0xE4] & 1) and not (self.sfr[0xB0] & 1)
            if p30_driven_low:
                return 0
        if page == 0 and port == 0x80 and bit in (6, 7):
            return self.wheel[0] if bit == 7 else self.wheel[1]
        if page == 0 and port == 0xA0 and bit == 7 and self.sensor.drive is not None:
            return self.sensor.drive
        return 1

    def read_port(self, port, page):
        v = 0
        for bit in range(8):
            v |= self.pin(port, bit, page) << bit
        return v

    def rd(self, a, rmw=False):
        if a < 0x80:
            return self.iram[a]
        if a == PSW:
            p = bin(self.sfr[ACC]).count("1") & 1
            return (self.sfr[PSW] & 0xFE) | p
        pg = self.page() if a in PAGED else 0
        if not rmw and a in (0x80, 0x90, 0xA0, 0xB0, 0xC0) and not (a == 0xC0 and pg):
            return self.read_port(a, pg)
        return (self.sfr1 if pg else self.sfr)[a]

    def wr(self, a, v):
        v &= 0xFF
        if a < 0x80:
            self.iram[a] = v
            return
        store = self._sfr_store(a)
        store[a] = v
        if a == RSTSTAT:
            gap = self.cycles - self.last_kick
            self.max_kick_gap = max(self.max_kick_gap, gap)
            self.last_kick = self.cycles
        if store is self.sfr and a in (0xA0, 0xE3):
            self._sensor_pins()


    def _sensor_pins(self):
        p = lambda b: self.pin(0xA0, b)
        self.sensor.update(p(4), p(6), p(5), p(7))

    def _take_ep(self, n):
        if n == 1:
            cnt = self.sfr1[IEP1CNT] & 0x1F
            self.ep1_reports.append(bytes(self.xram[EP1_IN:EP1_IN + cnt]))
            self.sfr1[EP1CON] &= ~0x04
            self.sfr1[USBIF2] |= 0x02
        else:
            cnt = self.sfr1[IEP2CNT] & 0x7F
            self.ep2_reports.append(bytes(self.xram[EP2_IN:EP2_IN + cnt]))
            self.sfr1[EP2CON] &= ~0x04
            self.sfr1[USBIF2] |= 0x04

    def rbit(self, b, rmw=False):
        if b < 0x80:
            return (self.iram[0x20 + (b >> 3)] >> (b & 7)) & 1
        return (self.rd(b & 0xF8, rmw) >> (b & 7)) & 1

    def wbit(self, b, val):
        if b < 0x80:
            a = 0x20 + (b >> 3)
            m = 1 << (b & 7)
            self.iram[a] = (self.iram[a] | m) if val else (self.iram[a] & ~m)
            return
        a = b & 0xF8
        cur = self.rd(a, rmw=True)
        m = 1 << (b & 7)
        self.wr(a, (cur | m) if val else (cur & ~m))

    # ---------------------------------------------------------------- helpers
    @property
    def A(self):
        return self.sfr[ACC]

    @A.setter
    def A(self, v):
        self.sfr[ACC] = v & 0xFF

    def C(self):
        return self.sfr[PSW] >> 7

    def setC(self, c):
        self.sfr[PSW] = (self.sfr[PSW] & 0x7F) | (0x80 if c else 0)

    def rn(self, n):
        return (self.sfr[PSW] & 0x18) + n

    def dptr(self):
        return self.sfr[DPH] << 8 | self.sfr[DPL]

    def set_dptr(self, v):
        self.sfr[DPH] = (v >> 8) & 0xFF
        self.sfr[DPL] = v & 0xFF

    def push(self, v):
        sp = (self.sfr[SP] + 1) & 0xFF
        self.sfr[SP] = sp
        self.iram[sp] = v & 0xFF

    def pop(self):
        sp = self.sfr[SP]
        v = self.iram[sp]
        self.sfr[SP] = (sp - 1) & 0xFF
        return v

    def fetch(self):
        v = self.code[self.pc]
        self.pc = (self.pc + 1) & 0xFFFF
        return v

    def rel(self):
        r = self.fetch()
        return (self.pc + (r - 256 if r > 127 else r)) & 0xFFFF

    def add(self, v, c=0):
        a = self.A
        s = a + v + c
        ac = ((a & 0xF) + (v & 0xF) + c) > 0xF
        ov = ((a ^ s) & (v ^ s) & 0x80) != 0
        psw = self.sfr[PSW] & 0x3B
        self.sfr[PSW] = psw | (0x80 if s > 0xFF else 0) | (0x40 if ac else 0) | (0x04 if ov else 0)
        self.A = s

    def subb(self, v):
        a = self.A
        c = self.C()
        d = a - v - c
        ac = ((a & 0xF) - (v & 0xF) - c) < 0
        ov = ((a ^ v) & (a ^ d) & 0x80) != 0
        psw = self.sfr[PSW] & 0x3B
        self.sfr[PSW] = psw | (0x80 if d < 0 else 0) | (0x40 if ac else 0) | (0x04 if ov else 0)
        self.A = d

    # operand helpers by low nibble (for the regular ALU groups)
    def src_val(self, op):
        lo = op & 0xF
        if lo == 4:
            return self.fetch()
        if lo == 5:
            return self.rd(self.fetch())
        if lo in (6, 7):
            return self.iram[self.iram[self.rn(lo - 6)]]
        return self.iram[self.rn(lo - 8)]

    # ---------------------------------------------------------------- execute
    def _build(self):
        pass

    def step(self):
        pc0 = self.pc
        if pc0 == 0x7F00:
            self.isp_hit = (self.A, self.sfr[B], (self.sfr[IEN0] >> 7) & 1, self.cycles)
            raise Halt("isp")
        op = self.fetch()
        cyc = 1
        hi, lo = op >> 4, op & 0xF

        if lo == 1:  # ajmp / acall
            a = ((self.pc + 1) & 0xF800) | ((op >> 5) << 8) | self.fetch()
            if op & 0x10:
                self.push(self.pc & 0xFF)
                self.push(self.pc >> 8)
            self.pc = a
            cyc = 3
        elif op in (0x24, 0x25, 0x26, 0x27) or 0x28 <= op <= 0x2F:
            self.add(self.src_val(op))
        elif op in (0x34, 0x35, 0x36, 0x37) or 0x38 <= op <= 0x3F:
            self.add(self.src_val(op), self.C())
        elif hi in (4, 5, 6) and lo >= 4:
            v = self.src_val(op)
            f = {4: lambda a, b: a | b, 5: lambda a, b: a & b, 6: lambda a, b: a ^ b}[hi]
            self.A = f(self.A, v)
        elif hi == 9 and lo >= 4:
            self.subb(self.src_val(op))
        elif hi == 0xE and lo >= 5:
            if lo == 5:
                self.A = self.rd(self.fetch())
            elif lo in (6, 7):
                self.A = self.iram[self.iram[self.rn(lo - 6)]]
            else:
                self.A = self.iram[self.rn(lo - 8)]
        elif hi == 0xF and lo >= 5:
            if lo == 5:
                self.wr(self.fetch(), self.A)
            elif lo in (6, 7):
                self.iram[self.iram[self.rn(lo - 6)]] = self.A
            else:
                self.iram[self.rn(lo - 8)] = self.A
        elif hi == 7 and lo >= 6:
            v = self.fetch()
            if lo in (6, 7):
                self.iram[self.iram[self.rn(lo - 6)]] = v
            else:
                self.iram[self.rn(lo - 8)] = v
        elif hi == 8 and lo >= 6:
            d = self.fetch()
            v = self.iram[self.iram[self.rn(lo - 6)]] if lo < 8 else self.iram[self.rn(lo - 8)]
            self.wr(d, v)
        elif hi == 0xA and lo >= 6:
            v = self.rd(self.fetch())
            if lo < 8:
                self.iram[self.iram[self.rn(lo - 6)]] = v
            else:
                self.iram[self.rn(lo - 8)] = v
        elif hi in (0, 1) and lo >= 4:  # inc / dec
            d = 1 if hi == 0 else -1
            if lo == 4:
                self.A = self.A + d
            elif lo == 5:
                a = self.fetch()
                self.wr(a, self.rd(a, rmw=True) + d)
            elif lo < 8:
                r = self.iram[self.rn(lo - 6)]
                self.iram[r] = (self.iram[r] + d) & 0xFF
            else:
                r = self.rn(lo - 8)
                self.iram[r] = (self.iram[r] + d) & 0xFF
        elif hi == 0xB and lo >= 4:  # cjne
            if lo == 4:
                a, b = self.A, self.fetch()
            elif lo == 5:
                a, b = self.A, self.rd(self.fetch())
            elif lo < 8:
                a, b = self.iram[self.iram[self.rn(lo - 6)]], self.fetch()
            else:
                a, b = self.iram[self.rn(lo - 8)], self.fetch()
            t = self.rel()
            self.setC(a < b)
            if a != b:
                self.pc = t
            cyc = 3
        elif hi == 0xC and lo >= 5:  # xch
            if lo == 5:
                a = self.fetch()
                v = self.rd(a, rmw=True)
                self.wr(a, self.A)
            elif lo < 8:
                r = self.iram[self.rn(lo - 6)]
                v = self.iram[r]
                self.iram[r] = self.A
            else:
                r = self.rn(lo - 8)
                v = self.iram[r]
                self.iram[r] = self.A
            self.A = v
        elif hi == 0xD and lo >= 8:  # djnz Rn
            r = self.rn(lo - 8)
            t = self.rel()
            self.iram[r] = (self.iram[r] - 1) & 0xFF
            if self.iram[r]:
                self.pc = t
            cyc = 3
        else:
            cyc = self._misc(op)
        self.cycles += cyc
        return cyc

    def _misc(self, op):
        if op == 0x00:
            return 1
        if op == 0x02:
            a = self.fetch() << 8
            self.pc = a | self.fetch()
            return 4
        if op == 0x03:
            a = self.A
            self.A = (a >> 1) | ((a & 1) << 7)
            return 1
        if op == 0x13:
            a = self.A
            c = self.C()
            self.setC(a & 1)
            self.A = (a >> 1) | (c << 7)
            return 1
        if op == 0x23:
            a = self.A
            self.A = ((a << 1) | (a >> 7)) & 0xFF
            return 1
        if op == 0x33:
            a = self.A
            c = self.C()
            self.setC(a >> 7)
            self.A = ((a << 1) | c) & 0xFF
            return 1
        if op == 0x10:  # jbc
            b = self.fetch()
            t = self.rel()
            if self.rbit(b, rmw=True):
                self.wbit(b, 0)
                self.pc = t
            return 4
        if op == 0x12:
            a = self.fetch() << 8
            a |= self.fetch()
            self.push(self.pc & 0xFF)
            self.push(self.pc >> 8)
            self.pc = a
            return 4
        if op in (0x20, 0x30):
            b = self.fetch()
            t = self.rel()
            if self.rbit(b) == (1 if op == 0x20 else 0):
                self.pc = t
            return 4
        if op in (0x22, 0x32):
            h = self.pop()
            l = self.pop()
            self.pc = h << 8 | l
            if op == 0x32:
                self.isr_depth = max(0, self.isr_depth - 1)
            return 4
        if op in (0x40, 0x50, 0x60, 0x70, 0x80):
            t = self.rel()
            take = {0x40: self.C() == 1, 0x50: self.C() == 0, 0x60: self.A == 0,
                    0x70: self.A != 0, 0x80: True}[op]
            if take:
                self.pc = t
            return 3
        if op in (0x42, 0x52, 0x62):
            a = self.fetch()
            v = self.rd(a, rmw=True)
            v = {0x42: v | self.A, 0x52: v & self.A, 0x62: v ^ self.A}[op]
            self.wr(a, v)
            return 2
        if op in (0x43, 0x53, 0x63):
            a = self.fetch()
            i = self.fetch()
            v = self.rd(a, rmw=True)
            v = {0x43: v | i, 0x53: v & i, 0x63: v ^ i}[op]
            self.wr(a, v)
            return 3
        if op == 0x72:
            self.setC(self.C() | self.rbit(self.fetch()))
            return 2
        if op == 0x82:
            self.setC(self.C() & self.rbit(self.fetch()))
            return 2
        if op == 0xA0:
            self.setC(self.C() | (1 - self.rbit(self.fetch())))
            return 2
        if op == 0xB0:
            self.setC(self.C() & (1 - self.rbit(self.fetch())))
            return 2
        if op == 0x73:
            self.pc = (self.A + self.dptr()) & 0xFFFF
            return 3
        if op == 0x74:
            self.A = self.fetch()
            return 2
        if op == 0x75:
            a = self.fetch()
            self.wr(a, self.fetch())
            return 3
        if op == 0x83:
            self.A = self.code[(self.A + self.pc) & 0xFFFF]
            return 3
        if op == 0x93:
            self.A = self.code[(self.A + self.dptr()) & 0xFFFF]
            return 3
        if op == 0x84:
            a, b = self.A, self.sfr[B]
            self.setC(0)
            psw = self.sfr[PSW] & ~0x04
            if b == 0:
                self.sfr[PSW] = psw | 0x04
            else:
                self.sfr[PSW] = psw
                self.A = a // b
                self.sfr[B] = a % b
            return 4
        if op == 0xA4:
            p = self.A * self.sfr[B]
            self.A = p & 0xFF
            self.sfr[B] = p >> 8
            self.setC(0)
            psw = self.sfr[PSW] & ~0x04
            self.sfr[PSW] = psw | (0x04 if p > 0xFF else 0)
            return 4
        if op == 0x85:
            s = self.fetch()
            d = self.fetch()
            self.wr(d, self.rd(s))
            return 3
        if op == 0x90:
            h = self.fetch()
            self.set_dptr(h << 8 | self.fetch())
            return 3
        if op == 0x92:
            self.wbit(self.fetch(), self.C())
            return 2
        if op == 0xA2:
            self.setC(self.rbit(self.fetch()))
            return 2
        if op == 0xA3:
            self.set_dptr((self.dptr() + 1) & 0xFFFF)
            return 1
        if op == 0xA5:
            raise RuntimeError(f"illegal opcode 0xA5 at {self.pc - 1:04x}")
        if op == 0xB2:
            b = self.fetch()
            self.wbit(b, 1 - self.rbit(b, rmw=True))
            return 2
        if op == 0xB3:
            self.setC(1 - self.C())
            return 1
        if op == 0xC0:
            self.push(self.rd(self.fetch(), rmw=True))
            return 2
        if op == 0xD0:
            a = self.fetch()
            self.wr(a, self.pop())
            return 2
        if op == 0xC2:
            self.wbit(self.fetch(), 0)
            return 2
        if op == 0xD2:
            self.wbit(self.fetch(), 1)
            return 2
        if op == 0xC3:
            self.setC(0)
            return 1
        if op == 0xD3:
            self.setC(1)
            return 1
        if op == 0xC4:
            a = self.A
            self.A = ((a << 4) | (a >> 4)) & 0xFF
            return 1
        if op == 0xD4:
            a = self.A
            c = self.C()
            if (a & 0xF) > 9 or (self.sfr[PSW] & 0x40):
                a += 6
            if (a >> 4) > 9 or c or a > 0xFF:
                a += 0x60
                c = 1
            self.A = a
            self.setC(c or a > 0xFF)
            return 1
        if op == 0xD5:
            a = self.fetch()
            t = self.rel()
            v = (self.rd(a, rmw=True) - 1) & 0xFF
            self.wr(a, v)
            if v:
                self.pc = t
            return 4
        if op in (0xD6, 0xD7):
            r = self.iram[self.rn(op - 0xD6)]
            a = self.A
            m = self.iram[r]
            self.A = (a & 0xF0) | (m & 0x0F)
            self.iram[r] = (m & 0xF0) | (a & 0x0F)
            return 1
        if op == 0xE0:
            self.A = self.xram[self.dptr()]
            return 2
        if op in (0xE2, 0xE3):
            self.A = self.xram[self.iram[self.rn(op - 0xE2)]]
            return 2
        if op == 0xE4:
            self.A = 0
            return 1
        if op == 0xF0:
            self.xram[self.dptr()] = self.A
            return 2
        if op in (0xF2, 0xF3):
            self.xram[self.iram[self.rn(op - 0xF2)]] = self.A
            return 2
        if op == 0xF4:
            self.A = ~self.A
            return 1
        raise RuntimeError(f"unhandled opcode {op:02x} at {self.pc - 1:04x}")

    # ---------------------------------------------------------------- interrupts / time
    def _interrupt(self, vector):
        self.push(self.pc & 0xFF)
        self.push(self.pc >> 8)
        self.pc = vector
        self.isr_depth += 1

    def t2_period(self):
        rc = self.sfr[0xCB] << 8 | self.sfr[0xCA]
        return (0x10000 - rc) * T2_DIV

    def _host_poll(self):
        if self.sfr1[EP1CON] & 0x04:
            self._take_ep(1)
        if self.sfr1[EP2CON] & 0x04:
            self._take_ep(2)

    def _skip_delay(self):
        """Run k whole delay iterations at once (24 cycles each), leaving the last one
        and any iteration that would cross a Timer2 tick to the real core."""
        r6 = self.rn(6)
        n = self.iram[r6] or 256
        k = n - 1
        if self.sfr[T2CON] & 0x04:
            k = min(k, max(0, (self.t2_period() - self.t2_acc) // 24 - 1), max(0, (self.next_poll - self.cycles) // 24 - 1))
        if k <= 0:
            return
        self.iram[r6] = (self.iram[r6] - k) & 0xFF
        self.cycles += 24 * k
        if self.sfr[T2CON] & 0x04:
            self.t2_acc += 24 * k
        # the skipped bodies each kicked the watchdog
        self.last_kick = self.cycles

    def usb_irq_pending(self):
        s1 = self.sfr1
        return (s1[USBIF1] & s1[USBIE1]) or (s1[USBIF2] & s1[USBIE2])

    def run(self, max_cycles, until=None, check_every=200):
        end = self.cycles + max_cycles
        n = 0
        dl = self.delay_loop
        idle = self.idle_pc
        while self.cycles < end:
            pc = self.pc
            if pc == dl:
                self._skip_delay()
            elif pc == idle and self.isr_depth == 0 and (self.sfr[T2CON] & 0x04) and not self.usb_irq_pending():
                skip = min(self.t2_period() - self.t2_acc, self.next_poll - self.cycles) - 64
                if skip > 0:
                    self.cycles += skip
                    self.t2_acc += skip
                    self.last_kick = self.cycles  # the spinning loop kicks every pass
            cyc = self.step()
            # Timer2
            if self.sfr[T2CON] & 0x04:
                self.t2_acc += cyc
                per = self.t2_period()
                if self.t2_acc >= per:
                    self.t2_acc -= per
                    self.sfr[T2CON] |= 0x80
            if self.cycles >= self.next_poll:
                self.next_poll += HOST_POLL
                self._host_poll()
            if self.isr_depth == 0 and (self.sfr[IEN0] & 0x80):
                if (self.sfr[IEN0] & 0x20) and (self.sfr[T2CON] & 0x80):
                    self._interrupt(0x2B)
                elif (self.sfr[IEN1] & 0x10) and self.usb_irq_pending():
                    self._interrupt(0x5B)
            n += 1
            if until is not None and n % check_every == 0 and until():
                return True
        return False

    def ms(self):
        return self.cycles * 1000 / FREQ


class Host:
    """Fake USB host driving the SH68F881 USB registers."""

    def __init__(self, cpu):
        self.cpu = cpu

    def _ep0con(self):
        return self.cpu.sfr1[EP0CON]

    def _wait_in(self, budget=2_000_000):
        c = self.cpu
        ok = c.run(budget, until=lambda: self._ep0con() & (0x04 | 0x08 | 0x02), check_every=20)
        con = self._ep0con()
        if con & 0x0A:
            return None  # STALL
        if not ok:
            raise TimeoutError("device did not answer on EP0")
        cnt = c.sfr1[IEP0CNT] & 0x0F
        data = bytes(c.xram[EP0_IN:EP0_IN + cnt])
        c.sfr1[EP0CON] &= ~0x04
        c.sfr1[USBIF2] |= 0x01  # IEP0IF
        return data

    def bus_reset(self):
        self.cpu.sfr1[USBIF1] |= 0x01
        self.cpu.run(200_000)

    def setup(self, rt, req, wvalue, windex, wlength, data_out=None):
        c = self.cpu
        c.sfr1[EP0CON] &= ~0x0A  # hardware clears stall on a new SETUP
        pkt = bytes([rt, req, wvalue & 0xFF, wvalue >> 8, windex & 0xFF, windex >> 8,
                     wlength & 0xFF, wlength >> 8])
        c.xram[EP0_OUT:EP0_OUT + 8] = pkt
        c.sfr1[OEP0CNT] = 8
        c.sfr1[EP0CON] &= ~0x01
        c.sfr1[USBIF1] |= 0x10  # SETUPIF
        if not c.run(2_000_000, until=lambda: not (c.sfr1[USBIF1] & 0x10) and c.isr_depth == 0, check_every=5):
            raise TimeoutError("SETUP not serviced (USB interrupt not enabled?)")
        if self._ep0con() & 0x0A:
            return None  # STALL
        if rt & 0x80:  # IN data stage
            got = b""
            while True:
                pkt = self._wait_in()
                if pkt is None:
                    return None
                got += pkt
                if len(pkt) < 8 or len(got) >= wlength:
                    break
            # status OUT (zero length)
            c.sfr1[OEP0CNT] = 0
            c.sfr1[USBIF2] |= 0x10
            c.run(50_000)
            return got
        if data_out is not None:
            ok = c.run(1_000_000, until=lambda: self._ep0con() & (0x01 | 0x0A), check_every=20)
            if self._ep0con() & 0x0A:
                return None
            c.xram[EP0_OUT:EP0_OUT + len(data_out)] = data_out
            c.sfr1[OEP0CNT] = len(data_out)
            c.sfr1[EP0CON] &= ~0x01
            c.sfr1[USBIF2] |= 0x10  # OEP0IF
        st = self._wait_in()  # status IN (zero length packet)
        if st is None:
            return None
        c.run(50_000)
        return b""

    def get_descriptor(self, typ, index=0, windex=0, length=255):
        return self.setup(0x80 if typ < 0x21 else 0x81, 0x06, typ << 8 | index, windex, length)

    def enumerate(self):
        self.bus_reset()
        dev = self.get_descriptor(1, length=64)
        self.setup(0x00, 0x05, 7, 0, 0)  # SET_ADDRESS 7
        dev = self.get_descriptor(1, length=18)
        cfg = self.get_descriptor(2, length=9)
        total = cfg[2] | cfg[3] << 8
        cfg = self.get_descriptor(2, length=total)
        strings = {}
        for i in (1, 2, 3):
            s = self.get_descriptor(3, i, 0x0409, 255)
            strings[i] = s[2:].decode("utf-16-le") if s else None
        self.setup(0x00, 0x09, 1, 0, 0)  # SET_CONFIGURATION 1
        self.setup(0x21, 0x0A, 0, 0, 0)  # SET_IDLE iface 0
        rep0 = self.get_descriptor(0x22, 0, 0, 512)
        self.setup(0x21, 0x0A, 0, 1, 0)
        rep1 = self.get_descriptor(0x22, 0, 1, 512)
        return dev, cfg, strings, rep0, rep1

    def get_feature(self, iface, rid, length):
        return self.setup(0xA1, 0x01, 0x0300 | rid, iface, length)

    def set_feature(self, iface, rid, data):
        return self.setup(0x21, 0x09, 0x0300 | rid, iface, len(data), data_out=bytes(data))
