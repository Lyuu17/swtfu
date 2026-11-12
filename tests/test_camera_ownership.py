"""Camera helper ownership, supported-image signatures and x86 address wrapping."""
import unittest
import pefile
from swtfu.tfu1.fps import camera_smoothing as fix


class Memory:
    def __init__(self, root):
        self.data = {root: fix.CODE}
        self.data.update({site: fix.hook(site, original, root+index*fix.STRIDE)
                          for index, (site, original, _, _) in enumerate(fix.SITES)})

    def read(self, address, size):
        return self.data[address][:size]


class Ownership(unittest.TestCase):
    def test_supported_image(self):
        pe = pefile.PE("analysis/SWTFU.decoded.exe")
        for address, original, _, _ in fix.SITES:
            self.assertEqual(pe.get_data(address-pe.OPTIONAL_HEADER.ImageBase,len(original)),original)

    def test_low_and_high_x86_addresses(self):
        for root in (0x10000000,0xF1000000):
            self.assertEqual(fix.validate(Memory(root))[0],root)

    def test_changed_helper(self):
        memory = Memory(0x10000000)
        memory.data[0x10000000] = bytes(len(fix.CODE))
        with self.assertRaisesRegex(RuntimeError,"Unrecognized"):
            fix.validate(memory)

    def test_partial_install(self):
        memory = Memory(0x10000000)
        memory.data[fix.SITES[2][0]] = fix.SITES[2][1]
        self.assertEqual(fix.validate(memory)[0],0x10000000)


if __name__ == "__main__":
    unittest.main()
