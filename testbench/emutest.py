import os
import unittest
from testbench import X16TestBench

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def emulator_path():
    """The x16emu to test: $X16EMU, else the one built by `make` in ./build."""
    return os.environ.get("X16EMU", os.path.join(ROOT, "build", "x16emu"))

def rom_path():
    """The ROM to boot with: $X16_ROM, else ./rom.bin."""
    return os.environ.get("X16_ROM", os.path.join(ROOT, "rom.bin"))

class EmulatorTestCase(unittest.TestCase):
    """
    Base class: every test gets its own emulator in testbench mode as self.e.
    Tests are skipped (with the reason) if the emulator or the ROM can't be found.
    """
    options = ["-warp"]

    def setUp(self):
        if not os.path.isfile(emulator_path()) or not os.access(emulator_path(), os.X_OK):
            self.skipTest("emulator not found: " + emulator_path() + " (build it or set X16EMU)")
        if not os.path.isfile(rom_path()):
            self.skipTest("ROM not found: " + rom_path() + " (set X16_ROM)")

        self.e = X16TestBench(emulator_path(), ["-rom", rom_path()] + self.options)
        self.addCleanup(self.e.close)
        self.e.waitReady()

    def runCode(self, code, address=0x6000):
        """Writes the bytes of code to address and runs them (end them with RTS, $60)."""
        for i, byte in enumerate(code):
            self.e.setMemory(address + i, byte)
        self.e.run(address)
