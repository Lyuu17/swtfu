"""Input-helper signatures, ownership and partial recovery validation."""
import unittest
import pefile
from swtfu.tfu1.fps import input_timing as fix


class Memory:
    def __init__(self,root):
        self.data={root:fix.CODE,**dict(fix.SOURCES)}
        self.data.update({site:fix.hook(site,original,root+index*fix.STRIDE)
                          for index,(site,original,_,_) in enumerate(fix.SITES)})

    def read(self,address,size):
        return self.data[address][:size]


class Ownership(unittest.TestCase):
    def test_supported_signatures(self):
        pe=pefile.PE('analysis/SWTFU.decoded.exe')
        for site in (*fix.SITES,*fix.SOURCES):
            address,original=site[:2]
            self.assertEqual(pe.get_data(address-pe.OPTIONAL_HEADER.ImageBase,len(original)),original)
        self.assertEqual(pe.get_data(0xF51B04-pe.OPTIONAL_HEADER.ImageBase,4),__import__('struct').pack('<f',1/30))

    def test_low_high_addresses(self):
        for root in (0x10000000,0xF1000000):
            self.assertEqual(fix.validate(Memory(root))[0],root)

    def test_changed_payload(self):
        memory=Memory(0x10000000);memory.data[0x10000000]=bytes(len(fix.CODE))
        with self.assertRaisesRegex(RuntimeError,'Unrecognized'):
            fix.validate(memory)

    def test_changed_accessor(self):
        memory=Memory(0x10000000);memory.data[fix.SOURCES[1][0]]=bytes(10)
        with self.assertRaisesRegex(RuntimeError,'Unsupported raw mouse'):
            fix.validate(memory)

    def test_partial_install(self):
        memory=Memory(0x10000000);memory.data[fix.SITES[2][0]]=fix.SITES[2][1]
        self.assertEqual(fix.validate(memory)[0],0x10000000)


if __name__=='__main__':
    unittest.main()
