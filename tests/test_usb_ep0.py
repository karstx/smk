import unittest

from devices import UcsimSession
from sim import (
    Sim, get_descriptor,
    DESC_DEVICE, DESC_CONFIGURATION, DESC_STRING,
    set_address, set_configuration, get_configuration,
    hid_set_idle, hid_get_idle, set_report_output, set_report_isp_setup,
)

SIM = Sim()

SETUPIF, RESMIF, OW, OVERIF = 0x10, 0x04, 0x20, 0x40
OEP0IF = 0x10
EP0CON, EP1CON = 0x97, 0x99
OEP0RDY, OEP0STL, IEP0STL = 0x01, 0x02, 0x08
IEP1STL, IEP1DTG = 0x08, 0x80


def setUpModule():
    reason = SIM.available()
    if reason:
        raise unittest.SkipTest(reason)


def get_status(recipient, index=0):
    return [0x80 | recipient, 0x00, 0x00, 0x00, index & 0xFF, 0x00, 0x02, 0x00]


def feature(set_, recipient, selector, index):
    return [recipient, 0x03 if set_ else 0x01, selector, 0x00, index & 0xFF, 0x00, 0x00, 0x00]


def set_report_feature(report_id, iface=1, length=6):
    return [0x21, 0x09, report_id, 0x03, iface, 0x00, length & 0xFF, 0x00]


class Session:
    def __init__(self):
        self.u = UcsimSession(SIM.firmware)
        for c in SIM._boot_to_post_init():
            self.u.cmd(c)
        self.u.brk(SIM.SLED_END)

    def _irq(self, usbif1=0, usbif2=0, out=None):
        if out is not None:
            self.u.cmd(SIM._set_xram(SIM.EP0_OUT_BUF, list(out) + [0] * (8 - len(out))))
        if usbif2:
            self.u.set_sfr(SIM.USBIF2, usbif2)
        if usbif1:
            self.u.set_sfr(SIM.USBIF1, usbif1)
        self.u.cmd("pc 0x9000")
        self.u.run()
        return self

    def setup(self, pkt, also_usbif1=0, also_usbif2=0, right_after_out=False):
        if right_after_out:
            self.u.set_sfr(EP0CON, self.u.get_sfr(EP0CON) & ~OEP0RDY)
            also_usbif2 |= OEP0IF
        ep0con = self.u.get_sfr(EP0CON)
        self.u.cmd(SIM._set_xram(SIM.EP0_OUT_BUF, pkt))
        if not ep0con & OEP0RDY:
            also_usbif1 |= OW
        self.u.set_sfr(EP0CON, ep0con & ~(OEP0RDY | OEP0STL | IEP0STL))
        return self._irq(usbif1=SETUPIF | also_usbif1, usbif2=also_usbif2)

    def deferred_setup(self, pkt):
        self.u.set_sfr(EP0CON, self.u.get_sfr(EP0CON) & ~OEP0RDY)
        self.u.cmd(SIM._set_xram(SIM.EP0_OUT_BUF, pkt))
        return self._irq(usbif1=OW | OVERIF, usbif2=OEP0IF)

    def announce_setup(self):
        latched = self.u.get_sfr(SIM.USBIF1) & (OW | OVERIF)
        self.u.set_sfr(EP0CON, self.u.get_sfr(EP0CON) & ~(OEP0RDY | OEP0STL | IEP0STL))
        return self._irq(usbif1=SETUPIF | latched)

    def out(self, data=()):
        ep0con = self.u.get_sfr(EP0CON)
        usbif1 = self.u.get_sfr(SIM.USBIF1)
        assert ep0con & OEP0RDY, f"the host's OUT would be NAKed: EP0CON=0x{ep0con:02x}"
        assert not usbif1 & OW, f"the host's OUT would be NAKed: OW latched, USBIF1=0x{usbif1:02x}"
        self.u.set_sfr(EP0CON, ep0con & ~OEP0RDY)
        self.u.set_sfr(SIM.OEP0CNT, len(data))
        return self._irq(usbif2=OEP0IF, out=data)

    def read(self, pkt):
        return self.setup(pkt).out()

    def poke_sfr(self, addr, val):
        self.u.set_sfr(addr, val)
        return self

    def finish(self, *sfrs):
        self.regs = {a: self.u.get_sfr(a) for a in sfrs}
        self.u.close()
        self.output = self.u.stderr_text()
        self.packets = SIM.in_packets(self.output)
        return self

    def sfr(self, addr):
        return self.regs[addr]

    def lengths(self):
        return [len(p) for p in self.packets]


class TestControlReads(unittest.TestCase):
    def test_status_stage_arms_nothing(self):
        s = Session().read(get_descriptor(DESC_DEVICE, length=64)).read(get_configuration()).finish()
        self.assertEqual(s.lengths(), [8, 8, 2, 1], s.output)

    def test_reply_truncated_to_wlength_on_packet_boundary(self):
        s = Session().read(get_descriptor(DESC_CONFIGURATION, length=16)).read(get_configuration()).finish()
        self.assertEqual(s.lengths(), [8, 8, 1], s.output)

    def test_wlength_zero_is_a_status_only_transfer(self):
        s = Session().setup(get_descriptor(DESC_DEVICE, length=0)).read(get_configuration()).finish()
        self.assertEqual(s.lengths(), [0, 1], s.output)

    def test_descriptor_bytes_intact_across_back_to_back_reads(self):
        s = Session().read(get_descriptor(DESC_DEVICE, length=64)).read(get_descriptor(DESC_DEVICE, length=64)).finish()
        self.assertEqual(s.lengths(), [8, 8, 2, 8, 8, 2], s.output)
        first, second = s.packets[:3], s.packets[3:]
        self.assertEqual(first, second)


class TestStaleEvents(unittest.TestCase):
    def test_stale_out_completion_does_not_reset_new_reply(self):
        ref = Session().read(get_descriptor(DESC_DEVICE, length=64)).finish().packets
        s = Session().setup(get_descriptor(DESC_DEVICE, length=64), also_usbif2=OEP0IF).out().finish()
        self.assertEqual(s.packets, ref, s.output)

    def test_fast_host_setup_after_status_out(self):
        s = (Session().setup(get_descriptor(DESC_DEVICE, length=64)).out()
             .setup(get_descriptor(DESC_STRING, index=2, length=255), right_after_out=True).out()
             .read(get_configuration())
             .finish())
        self.assertEqual(s.lengths(), [8, 8, 2, 8, 8, 8, 2, 1], s.output)

    def test_deferred_setup_is_not_discarded(self):
        s = Session().setup(get_descriptor(DESC_DEVICE, length=64)).deferred_setup(get_configuration())
        latched = s.u.get_sfr(SIM.USBIF1) & (OW | OVERIF)
        s = s.announce_setup().out().finish()
        self.assertEqual(latched, OW | OVERIF, "the latches of a stored SETUP were cleared before it was announced")
        self.assertEqual(s.lengths(), [8, 8, 2, 1], s.output)

    def test_setup_with_other_usbif1_flags_is_handled(self):
        s = Session().setup(get_configuration(), also_usbif1=RESMIF).out().finish()
        self.assertEqual(s.lengths(), [1], s.output)


class TestNoDataAndWrites(unittest.TestCase):
    def test_no_data_request_arms_exactly_one_status_packet(self):
        s = Session().setup(hid_set_idle(0x10)).read(hid_get_idle()).finish()
        self.assertEqual(s.packets, [[], [0x10]], s.output)

    def test_led_report_acks_status_once(self):
        s = Session().setup(set_report_output(length=1)).out([0x02]).read(get_configuration()).finish()
        self.assertEqual(s.lengths(), [0, 1], s.output)

    def test_short_led_data_stage_stalls(self):
        s = Session().setup(set_report_output(length=1)).out([]).finish(EP0CON)
        self.assertEqual(s.packets, [], s.output)
        self.assertEqual(s.sfr(EP0CON) & (IEP0STL | OEP0STL), IEP0STL | OEP0STL, s.output)

    def test_led_report_with_wrong_length_stalls(self):
        s = Session().setup(set_report_output(length=2)).finish(EP0CON)
        self.assertEqual(s.packets, [], s.output)
        self.assertEqual(s.sfr(EP0CON) & (IEP0STL | OEP0STL | OEP0RDY), IEP0STL | OEP0STL | OEP0RDY, s.output)

    def test_isp_report_acks_status(self):
        s = Session().setup(set_report_isp_setup()).out([0x05, 0x75, 0, 0, 0]).finish()
        self.assertEqual(s.packets, [[]], s.output)

    def test_unknown_feature_report_stalls(self):
        s = Session().setup(set_report_feature(0x42)).finish(EP0CON)
        self.assertEqual(s.packets, [], s.output)
        self.assertEqual(s.sfr(EP0CON) & (IEP0STL | OEP0STL | OEP0RDY), IEP0STL | OEP0STL | OEP0RDY, s.output)


class TestStalls(unittest.TestCase):
    def test_rejected_set_configuration_arms_nothing(self):
        s = Session().setup(set_address(0x2A)).setup(set_configuration(5)).finish(EP0CON)
        self.assertEqual(s.lengths(), [0], s.output)
        self.assertEqual(s.sfr(EP0CON) & (IEP0STL | OEP0STL | OEP0RDY), IEP0STL | OEP0STL | OEP0RDY, s.output)

    def test_set_configuration_before_address_stalls(self):
        s = Session().setup(set_configuration(1)).finish(EP0CON)
        self.assertEqual(s.packets, [], s.output)

    def test_unsupported_descriptor_stalls(self):
        s = Session().setup(get_descriptor(0x06, length=10)).finish(EP0CON)
        self.assertEqual(s.packets, [], s.output)
        self.assertEqual(s.sfr(EP0CON) & (IEP0STL | OEP0STL | OEP0RDY), IEP0STL | OEP0STL | OEP0RDY, s.output)


class TestEndpointRequests(unittest.TestCase):
    def configured(self):
        return Session().setup(set_address(0x2A)).setup(set_configuration(1))

    def test_get_status_endpoint0(self):
        s = Session().read(get_status(0x02, 0x80)).finish()
        self.assertEqual(s.packets, [[0, 0]], s.output)

    def test_halt_roundtrip_on_ep1(self):
        s = (self.configured()
             .setup(feature(True, 0x02, 0, 0x81))
             .read(get_status(0x02, 0x81))
             .setup(feature(False, 0x02, 0, 0x81))
             .read(get_status(0x02, 0x81))
             .finish(EP1CON))
        self.assertEqual(s.packets, [[], [], [], [1, 0], [], [0, 0]], s.output)
        self.assertEqual(s.sfr(EP1CON) & (IEP1STL | IEP1DTG), 0, s.output)

    def test_set_configuration_resets_ep1_toggle(self):
        s = (Session().setup(set_address(0x2A))
             .poke_sfr(EP1CON, IEP1DTG)
             .setup(set_configuration(1))
             .finish(EP1CON))
        self.assertEqual(s.sfr(EP1CON) & IEP1DTG, 0, s.output)

    def test_endpoint_requests_before_configuration_stall(self):
        s = Session().setup(get_status(0x02, 0x81)).finish()
        self.assertEqual(s.packets, [], s.output)


if __name__ == "__main__":
    unittest.main()
