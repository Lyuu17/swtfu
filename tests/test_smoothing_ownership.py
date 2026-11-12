"""Reject foreign helpers and support valid x86 rel32 wraparound."""
import unittest
from swtfu.tfu1.fps import physics_smoothing as fix


class Memory:
    def __init__(self, root):
        self.data = {root: fix.CODE}
        self.data.update({address: fix.hook(address, original, root + index * 64)
                          for index, (address, original, _) in enumerate(fix.SITES)})

    def read(self, address, size):
        return self.data[address][:size]


class OwnershipTests(unittest.TestCase):
    def test_low_and_high_x86_addresses(self):
        for root in (0x10000000, 0xF1000000):
            with self.subTest(root=root):
                self.assertEqual(fix.validate(Memory(root))[0], root)

    def test_modified_helper_is_rejected(self):
        memory = Memory(0x10000000)
        memory.data[0x10000000] = b"\x90" * len(fix.CODE)
        with self.assertRaisesRegex(RuntimeError, "Unrecognized"):
            fix.validate(memory)

    def test_partial_install_is_recognized(self):
        memory = Memory(0x10000000)
        site, original, _ = fix.SITES[1]
        memory.data[site] = original
        self.assertEqual(fix.validate(memory)[0], 0x10000000)


if __name__ == "__main__":
    unittest.main()
