import unittest
from emutest import EmulatorTestCase
from testbench import Status

class CpuTest(EmulatorTestCase):
    """65C02 behaviour that programs rely on."""

    def test_decimal_adc(self):
        self.runCode([0xf8,              # sed
                      0x18,              # clc
                      0xa9, 0x19,        # lda #$19
                      0x69, 0x28,        # adc #$28
                      0x60])
        self.assertEqual(self.e.getA(), 0x47)
        self.assertEqual(self.e.getStatus() & Status.C, 0)

    def test_decimal_adc_carry_and_zero(self):
        # the 65C02 (unlike the 6502) sets Z from the decimal result
        self.runCode([0xf8, 0x18,
                      0xa9, 0x99,        # lda #$99
                      0x69, 0x01,        # adc #$01
                      0x60])
        self.assertEqual(self.e.getA(), 0x00)
        self.assertNotEqual(self.e.getStatus() & Status.C, 0)
        self.assertNotEqual(self.e.getStatus() & Status.Z, 0)

    def test_decimal_sbc(self):
        self.runCode([0xf8,
                      0x38,              # sec
                      0xa9, 0x46,        # lda #$46
                      0xe9, 0x12,        # sbc #$12
                      0x60])
        self.assertEqual(self.e.getA(), 0x34)
        self.assertNotEqual(self.e.getStatus() & Status.C, 0)

    def test_jsr_rts(self):
        sp = self.e.getStackPointer()
        self.runCode([0x20, 0x06, 0x60,  # 6000 jsr $6006
                      0xa2, 0x05,        # 6003 ldx #5
                      0x60,              # 6005 rts
                      0xa9, 0x07,        # 6006 lda #7
                      0x60])             # 6008 rts
        self.assertEqual(self.e.getA(), 7)
        self.assertEqual(self.e.getX(), 5)
        self.assertEqual(self.e.getStackPointer(), sp)

    def test_push_pull_order(self):
        # PHX/PHY/PLX/PLY exist only on the 65C02
        self.e.setA(1)
        self.e.setX(2)
        self.e.setY(3)
        sp = self.e.getStackPointer()
        self.runCode([0x48, 0xda, 0x5a,  # pha phx phy
                      0xa9, 0x00, 0xa2, 0x00, 0xa0, 0x00,
                      0x68, 0xfa, 0x7a,  # pla plx ply
                      0x60])
        self.assertEqual(self.e.getA(), 3)
        self.assertEqual(self.e.getX(), 2)
        self.assertEqual(self.e.getY(), 1)
        self.assertEqual(self.e.getStackPointer(), sp)

    def test_stz_and_bra(self):
        self.e.setMemory(0x6100, 0xff)
        self.e.setA(0x55)
        self.runCode([0x9c, 0x00, 0x61,  # stz $6100
                      0x80, 0x02,        # bra +2
                      0xa9, 0xff,        # lda #$ff (skipped)
                      0x60])
        self.assertEqual(self.e.getMemory(0x6100), 0)
        self.assertEqual(self.e.getA(), 0x55)

    def test_tsb_sets_bits_and_z_from_and(self):
        self.e.setMemory(0x6100, 0xf0)
        self.e.setA(0x0f)
        self.runCode([0x0c, 0x00, 0x61,  # tsb $6100
                      0x60])
        self.assertEqual(self.e.getMemory(0x6100), 0xff)
        self.assertNotEqual(self.e.getStatus() & Status.Z, 0)   # 0x0f & 0xf0 == 0

    def test_trb_clears_bits_and_z_from_and(self):
        self.e.setMemory(0x6100, 0xff)
        self.e.setA(0x0f)
        self.runCode([0x1c, 0x00, 0x61,  # trb $6100
                      0x60])
        self.assertEqual(self.e.getMemory(0x6100), 0xf0)
        self.assertEqual(self.e.getStatus() & Status.Z, 0)      # 0x0f & 0xff != 0

    def test_indirect_indexed_y(self):
        self.e.setMemory(0x30, 0x00)
        self.e.setMemory(0x31, 0x61)
        self.e.setMemory(0x6100, 0xee)   # must not be read
        self.e.setMemory(0x6103, 0x99)
        self.e.setY(3)
        self.runCode([0xb1, 0x30,        # lda ($30),y
                      0x60])
        self.assertEqual(self.e.getA(), 0x99)

    def test_zeropage_indirect_without_index(self):
        # lda (zp) is a 65C02 addition
        self.e.setMemory(0x30, 0x00)
        self.e.setMemory(0x31, 0x61)
        self.e.setMemory(0x6100, 0x42)
        self.e.setMemory(0x6103, 0xee)   # what is read if Y is added by mistake
        self.e.setY(3)
        self.runCode([0xb2, 0x30,        # lda ($30)
                      0x60])
        self.assertEqual(self.e.getA(), 0x42)

class UndocumentedNopTest(EmulatorTestCase):
    """
    $5C, $DC and $FC are 3-byte NOPs on the hardware (the 65C02 reads the absolute operand;
    https://github.com/X16Community/x16-emulator/issues/299). The emulator treats them as
    1 byte, so the operand is executed as code. Remove @expectedFailure when that is fixed.
    """

    def assertSkipsTwoOperandBytes(self, opcode):
        # lda #$11 is the operand; if it is skipped, the rts behind it ends the run with A = 0
        self.e.setA(0)
        self.runCode([opcode, 0xa9, 0x11, 0x60])
        self.assertEqual(self.e.getA(), 0)

    @unittest.expectedFailure
    def test_5c_is_three_bytes(self):
        self.assertSkipsTwoOperandBytes(0x5c)

    @unittest.expectedFailure
    def test_dc_is_three_bytes(self):
        self.assertSkipsTwoOperandBytes(0xdc)

    @unittest.expectedFailure
    def test_fc_is_three_bytes(self):
        self.assertSkipsTwoOperandBytes(0xfc)

class BankedRamTest(EmulatorTestCase):

    def test_code_runs_from_selected_bank(self):
        self.e.setRamBank(3)
        self.runCode([0xa9, 0x77, 0x60], 0xa000)      # lda #$77, rts
        self.e.setRamBank(4)
        self.runCode([0xa9, 0x88, 0x60], 0xa000)      # lda #$88, rts

        self.e.setRamBank(3)
        self.e.run(0xa000)
        self.assertEqual(self.e.getA(), 0x77)
        self.e.setRamBank(4)
        self.e.run(0xa000)
        self.assertEqual(self.e.getA(), 0x88)

if __name__ == '__main__':
    unittest.main()
