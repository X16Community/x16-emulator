import os
import unittest
from testbench import X16TestBench
from testbench import Status

class SelfTest(unittest.TestCase):
    e = None

    def __init__(self, *args, **kwargs):
        super(SelfTest, self).__init__(*args, **kwargs)
        
        emu_path = "../build/x16emu" if os.path.exists("../build/x16emu") else "../x16emu"
        self.e = X16TestBench(emu_path, ["-warp"])
        self.e.waitReady()

    def test_rombank(self):
        self.e.setRomBank(0)
        self.assertEqual(self.e.getMemory(1),0)

        self.e.setRomBank(10)
        self.assertEqual(self.e.getMemory(1),10)

    def test_rambank(self):
        self.e.setRamBank(0)
        self.assertEqual(self.e.getMemory(0),0)

        self.e.setRamBank(10)
        self.assertEqual(self.e.getMemory(0),10)

    def test_mem(self):
        #Set and retrieve value 0
        self.e.setMemory(0x6000, 0)
        self.assertEqual(self.e.getMemory(0x6000), 0)

        #Set and retrieve value 255
        self.e.setMemory(0x6000, 255)
        self.assertEqual(self.e.getMemory(0x6000), 255)

        #Set value -1, should cause an exception
        with self.assertRaises(Exception):
            self.e.setMemory(0x6000, -1)

        #Set value 256, should cause an exception
        with self.assertRaises(Exception):
            self.e.setMemory(0x6000, 256)

        #Use address -1, should cause an exception
        with self.assertRaises(Exception):
            self.e.setMemory(-1, 0)
        
        #Use address 10000, should cause an exception
        with self.assertRaises(Exception):
            self.e.setMemory(0x10000, 0)

    def test_fillmemory(self):
        self.e.fillMemory(0x6000, 0x61ff, 0x80)
        self.e.fillMemory(0x6001, 0x61fe, 0xff)
        
        self.assertEqual(self.e.getMemory(0x6000), 0x80)
        self.assertEqual(self.e.getMemory(0x6001), 0xff)
        self.assertEqual(self.e.getMemory(0x61fe), 0xff)
        self.assertEqual(self.e.getMemory(0x61ff), 0x80)

    def test_memblock(self):
        #Low RAM and page-crossing block read/write
        data = bytes((i * 19 + 7) & 0xff for i in range(64))
        self.e.setMemoryBlock(0x04e0, data)
        self.assertEqual(self.e.getMemoryBlock(0x04e0, len(data)), data)
        self.assertEqual([self.e.getMemory(0x04e0 + i) for i in range(len(data))], list(data))

        #Banked RAM across two banks
        bank1 = bytes([0x11, 0x22, 0x33, 0x44])
        bank2 = bytes([0xaa, 0xbb, 0xcc, 0xdd])
        self.e.setRamBank(1)
        self.e.setMemoryBlock(0xa000, bank1)
        self.e.setRamBank(2)
        self.e.setMemoryBlock(0xa000, bank2)
        self.e.setRamBank(1)
        self.assertEqual(self.e.getMemoryBlock(0xa000, 4), bank1)
        self.e.setRamBank(2)
        self.assertEqual(self.e.getMemoryBlock(0xa000, 4), bank2)

        #I/O-adjacent range ($9EF8..$9EFF)
        io_adj = bytes(range(0xf0, 0xf8))
        self.e.setMemoryBlock(0x9ef8, io_adj)
        self.assertEqual(self.e.getMemoryBlock(0x9ef8, 8), io_adj)

        #Out-of-range and wrapping past $FFFF should raise
        with self.assertRaises(Exception):
            self.e.getMemoryBlock(0xffff, 2)
        with self.assertRaises(Exception):
            self.e.setMemoryBlock(0xffff, b"\x01\x02")

    def test_vramblock(self):
        #Populate VRAM at $1B000 via VERA data port 0 with auto-increment = 1
        vram_data = bytes([0x41, 0x61, 0x42, 0x61, 0x43, 0x61, 0x44, 0x61])
        self.e.setMemory(0x9f25, 0x00)
        self.e.setMemory(0x9f20, 0x00)
        self.e.setMemory(0x9f21, 0xb0)
        self.e.setMemory(0x9f22, 0x11)
        for b in vram_data:
            self.e.setMemory(0x9f23, b)

        #Set distinct VERA register state and verify RQV does not disturb it
        self.e.setMemory(0x9f20, 0x34)
        self.e.setMemory(0x9f21, 0x12)
        self.e.setMemory(0x9f22, 0x20)
        self.e.setMemory(0x9f25, 0x01)
        self.assertEqual(self.e.getVramBlock(0x1b000, len(vram_data)), vram_data)
        self.assertEqual(self.e.getMemory(0x9f20), 0x34)
        self.assertEqual(self.e.getMemory(0x9f21), 0x12)
        self.assertEqual(self.e.getMemory(0x9f22), 0x20)
        self.assertEqual(self.e.getMemory(0x9f25), 0x01)

        #Wrapping past $1FFFF should raise
        with self.assertRaises(Exception):
            self.e.getVramBlock(0x1ffff, 2)

    def test_a(self):
        #Set and retrieve value 0
        self.e.setA(0)
        self.assertEqual(self.e.getA(), 0)

        #Set and retrieve value 255
        self.e.setA(255)
        self.assertEqual(self.e.getA(), 255)

        #Set value -1, should cause an exception
        with self.assertRaises(Exception):
            self.e.setA(-1)

        #Set value 256, should cause an exception
        with self.assertRaises(Exception):
            self.e.setA(256)

    def test_x(self):
        #Set and retrieve value 0
        self.e.setX(0)
        self.assertEqual(self.e.getX(), 0)

        #Set and retrieve value 255
        self.e.setX(255)
        self.assertEqual(self.e.getX(), 255)

        #Set value -1, should cause an exception
        with self.assertRaises(Exception):
            self.e.setX(-1)

        #Set value 256, should cause an exception
        with self.assertRaises(Exception):
            self.e.setX(256)

    def test_y(self):
        #Set and retrieve value 0
        self.e.setY(0)
        self.assertEqual(self.e.getY(), 0)

        #Set and retrieve value 255
        self.e.setY(255)
        self.assertEqual(self.e.getY(), 255)

        #Set value -1, should cause an exception
        with self.assertRaises(Exception):
            self.e.setY(-1)

        #Set value 256, should cause an exception
        with self.assertRaises(Exception):
            self.e.setY(256)

    def test_stackpointer(self):
        #Set and retrieve value 0
        self.e.setStackPointer(0)
        self.assertEqual(self.e.getStackPointer(), 0)

        #Set and retrieve value 255
        self.e.setStackPointer(255)
        self.assertEqual(self.e.getStackPointer(), 255)

        #Set value -1, should cause an exception
        with self.assertRaises(Exception):
            self.e.setStackPointer(-1)

        #Set value 256, should cause an exception
        with self.assertRaises(Exception):
            self.e.setStackPointer(256)

    def test_statusregister(self):
        #Set and retrieve value 0
        self.e.setStatus(0)
        self.assertEqual(self.e.getStatus(), 0)

        #Set and retrieve value 255
        self.e.setStatus(255)
        self.assertEqual(self.e.getStatus(), 255)

        #Set value -1, should cause an exception
        with self.assertRaises(Exception):
            self.e.setStatus(-1)

        #Set value 256, should cause an exception
        with self.assertRaises(Exception):
            self.e.setStatus(256)

    def test_carryflag(self):
        self.e.setStatus(self.e.getStatus() & (255-Status.C))

        self.e.setMemory(0x6000, 0x38)      #sec
        self.e.setMemory(0x6001, 0x60)      #rts
        self.e.run(0x6000)
        self.assertEqual(self.e.getStatus() & Status.C, Status.C)

        self.e.setMemory(0x6000, 0x18)      #clc
        self.e.run(0x6000)
        self.assertNotEqual(self.e.getStatus() & Status.C, Status.C)

    def test_zeroflag(self):
        #Clear zero flag
        self.e.setStatus(self.e.getStatus() & (255-Status.Z))

        #Run code that sets zero flag
        self.e.setMemory(0x6000, 0xa9)      #lda #0
        self.e.setMemory(0x6001, 0x00)
        self.e.setMemory(0x6002, 0x60)      #rts
        self.e.run(0x6000)
        self.assertEqual(self.e.getStatus() & Status.Z, Status.Z)

        #Run code that clears zero flag
        self.e.setMemory(0x6000, 0xa9)      #lda #1
        self.e.setMemory(0x6001, 0x01)
        self.e.setMemory(0x6002, 0x60)      #rts
        self.e.run(0x6000)
        self.assertNotEqual(self.e.getStatus() & Status.Z, Status.Z)

    def test_overflowflag(self):
        #Clear carry and overflow flags
        self.e.setStatus(self.e.getStatus() & (255-Status.C-Status.V))

        #Run code that doesn't cause overflow
        self.e.setMemory(0x6000, 0xa9)      #lda #1
        self.e.setMemory(0x6001, 0x01)
        self.e.setMemory(0x6002, 0x69)      #adc #1
        self.e.setMemory(0x6003, 0x01)
        self.e.setMemory(0x6004, 0x60)      #rts
        self.e.run(0x6000)
        self.assertNotEqual(self.e.getStatus() & Status.V, Status.V)

        #Run code that causes overflow
        self.e.setMemory(0x6000, 0xa9)      #lda #80
        self.e.setMemory(0x6001, 0x80)
        self.e.setMemory(0x6002, 0x69)      #adc #ff
        self.e.setMemory(0x6003, 0xff)
        self.e.setMemory(0x6004, 0x60)      #rts
        self.e.run(0x6000)
        self.assertEqual(self.e.getStatus() & Status.V, Status.V)

    def test_negativeflag(self):
        #Clear negative flag
        self.e.setStatus(self.e.getStatus() & (255-Status.N))

        #Run code that should set negative flag
        self.e.setMemory(0x6000, 0x38)      #sec
        self.e.setMemory(0x6001, 0xa9)      #lda #0
        self.e.setMemory(0x6002, 0x00)
        self.e.setMemory(0x6003, 0xe9)      #sbc #1
        self.e.setMemory(0x6004, 0x01)
        self.e.setMemory(0x6005, 0x60)      #rts
        self.e.run(0x6000)
        self.assertEqual(self.e.getStatus() & Status.N, Status.N)

        #Run code that should clear the negative flag
        self.e.setMemory(0x6000, 0x38)      #sec
        self.e.setMemory(0x6001, 0xa9)      #lda #ff
        self.e.setMemory(0x6002, 0xff)
        self.e.setMemory(0x6003, 0xe9)      #sbc #81
        self.e.setMemory(0x6004, 0x81)
        self.e.setMemory(0x6005, 0x60)      #rts
        self.e.run(0x6000)
        self.assertNotEqual(self.e.getStatus() & Status.N, Status.N)

    def test_run(self):
        #Use address -1, should cause an exception
        with self.assertRaises(Exception):
            self.e.run(-1)
        
        #Use address 10000, should cause an exception
        with self.assertRaises(Exception):
            self.e.run(0x10000)

if __name__ == '__main__':
    unittest.main()
