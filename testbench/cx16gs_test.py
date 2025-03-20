import unittest
from testbench import X16TestBench
from testbench import Status

class CX16GSTest(unittest.TestCase):
    e = None

    def __init__(self, *args, **kwargs):
        super(CX16GSTest, self).__init__(*args, **kwargs)
        
        self.e = X16TestBench("../x16emu", ["-c816"])
        self.e.waitReady()

    def test_uppermemory(self):
        # Arrange
        self.e.setMemoryL(0x01, 0x0000, 0x01)
        self.e.setMemoryL(0x02, 0x0000, 0x02)
        self.e.setMemoryL(0x03, 0x0000, 0x03)

        # Act

        # Assert
        self.assertEqual(self.e.getMemoryL(0x01, 0x0000), 0x01)
        self.assertEqual(self.e.getMemoryL(0x02, 0x0000), 0x02)
        self.assertEqual(self.e.getMemoryL(0x03, 0x0000), 0x03)        

    def test_LDA_immediate(self):
        # Arrange
        testValue = 0x34
        self.e.setMemory(0x8000, 0xa9)  # lda #$34
        self.e.setMemory(0x8001, testValue)        
        self.e.setMemory(0x8002, 0x60)  # rts
        
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)

    def test_LDA_absolute(self):
        # Arrange        
        testValue = 0xef
        self.e.setMemory(0x8000, 0xad)  # lda $9000
        self.e.setMemory(0x8001, 0x00)        
        self.e.setMemory(0x8002, 0x90)        
        self.e.setMemory(0x8003, 0x60)  # rts

        self.e.setMemory(0x9000, testValue)
        
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)  

    def test_LDA_absolute_long(self):
        # Arrange        
        testValue = 0xcf        
        self.e.setMemory(0x8000, 0xaf)  # lda $019000
        self.e.setMemory(0x8001, 0x00)        
        self.e.setMemory(0x8002, 0x90)        
        self.e.setMemory(0x8003, 0x01)        
        self.e.setMemory(0x8004, 0x60)  # rts

        self.e.setMemoryL(0x01, 0x9000, testValue)
        
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)        

    def test_LDA_direct_page(self):
        # Arrange        
        testValue = 0x1f        
        self.e.setMemory(0x8000, 0xa5)  # lda $05
        self.e.setMemory(0x8001, 0x05)                
        self.e.setMemory(0x8002, 0x60)  # rts

        self.e.setMemory(0x0005, testValue)
        
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)    

    def test_LDA_direct_page_indirect(self):
        # Arrange        
        testValue = 0x2f        
        self.e.setMemory(0x8000, 0xb2)  # lda ($04)
        self.e.setMemory(0x8001, 0x04)                
        self.e.setMemory(0x8002, 0x60)  # rts

        self.e.setMemory(0x0004, 0X00)
        self.e.setMemory(0x0005, 0X90)
        self.e.setMemory(0x9000, testValue)
        
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)   

    def test_LDA_direct_page_indirect_long(self):
        # Arrange        
        testValue = 0x4f        
        self.e.setMemory(0x8000, 0xa7)  # lda [$04]
        self.e.setMemory(0x8001, 0x04)                
        self.e.setMemory(0x8002, 0x60)  # rts

        self.e.setMemory(0x0004, 0X00)
        self.e.setMemory(0x0005, 0X90)
        self.e.setMemory(0x0006, 0X01)
        self.e.setMemoryL(0x01, 0x9000, testValue)
        
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)   

    def test_LDA_absolute_X(self):
        # Arrange        
        testValue = 0x4f     
        testFailValue = 0x12   
        self.e.setMemory(0x8000, 0xa2)  # ldx #$01
        self.e.setMemory(0x8001, 0x01)                
        self.e.setMemory(0x8002, 0xbd)  # lda $9000,x
        self.e.setMemory(0x8003, 0x00)
        self.e.setMemory(0x8004, 0x90)
        self.e.setMemory(0x8005, 0x60)  # rts

        self.e.setMemory(0x9000, testFailValue)
        self.e.setMemory(0x9001, testValue)
                
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)                                  
        self.assertNotEqual(self.e.getA(), testFailValue)                                  

    def test_LDA_absolute_X_long(self):
        # Arrange        
        testValue = 0x4f     
        testFailValue = 0x12   
        self.e.setMemory(0x8000, 0xa2)  # ldx #$01
        self.e.setMemory(0x8001, 0x01)                
        self.e.setMemory(0x8002, 0xbf)  # lda $019000,x
        self.e.setMemory(0x8003, 0x00)
        self.e.setMemory(0x8004, 0x90)
        self.e.setMemory(0x8005, 0x01)
        self.e.setMemory(0x8006, 0x60)  # rts

        self.e.setMemoryL(0x01, 0x9000, testFailValue)
        self.e.setMemoryL(0x01, 0x9001, testValue)
                
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)                                  
        self.assertNotEqual(self.e.getA(), testFailValue)                                  

    def test_LDA_absolute_Y(self):
        # Arrange        
        testValue = 0x4f     
        testFailValue = 0x12   
        self.e.setMemory(0x8000, 0xa0)  # ldy #$01
        self.e.setMemory(0x8001, 0x01)                
        self.e.setMemory(0x8002, 0xb9)  # lda $9000,y
        self.e.setMemory(0x8003, 0x00)
        self.e.setMemory(0x8004, 0x90)
        self.e.setMemory(0x8005, 0x60)  # rts

        self.e.setMemory(0x9000, testFailValue)
        self.e.setMemory(0x9001, testValue)
                
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)                                  
        self.assertNotEqual(self.e.getA(), testFailValue)                
        
if __name__ == '__main__':
    unittest.main()