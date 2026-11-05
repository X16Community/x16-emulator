"""
Runs the testbench suite for ctest.

Exit code: 0 = passed, 1 = failed, 77 = every test was skipped (no emulator or ROM; ctest
SKIP_RETURN_CODE). A failure is never reported as a skip, even if some tests were skipped.
"""
import sys
import unittest

SKIPPED = 77

def main():
    suite = unittest.defaultTestLoader.loadTestsFromNames(["selftest", "example", "cpu_test"])
    result = unittest.TextTestRunner(verbosity=1).run(suite)

    if not result.wasSuccessful():
        return 1
    if result.testsRun > 0 and len(result.skipped) == result.testsRun:
        return SKIPPED
    return 0

if __name__ == "__main__":
    sys.exit(main())
