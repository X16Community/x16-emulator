import unittest
from testbench import X16TestBench
from testbench import Status

class CX16GSTest(unittest.TestCase):
    e = None

    pc = 0

    def setPC(self, pc):
        self.pc = pc

    def nextPC(self):
        r = self.pc
        self.pc += 1        
        return r

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

    def test_SEP_30(self):
        # Arrange     
        start = 0x8000
        self.setPC(start)   

        self.e.setMemory(self.nextPC(), 0x18)   # clc
        self.e.setMemory(self.nextPC(), 0xfb)   # xbe
        self.e.setMemory(self.nextPC(), 0xe2)   # sep #$30 - A, X, Y all 8- bit
        self.e.setMemory(self.nextPC(), 0x30)
        self.e.setMemory(self.nextPC(), 0x08)   # php
        self.e.setMemory(self.nextPC(), 0x68)   # pla 
        self.e.setMemory(self.nextPC(), 0x8d)   # sta $9000
        self.e.setMemory(self.nextPC(), 0x00)
        self.e.setMemory(self.nextPC(), 0x90)       
        self.e.setMemory(self.nextPC(), 0x60)   # rts
                
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getMemory(0x9000), 0x31)              

    def test_REP_30(self):
        # Arrange     
        start = 0x8000
        self.setPC(start)     

        self.e.setMemory(self.nextPC(), 0x18)   # clc
        self.e.setMemory(self.nextPC(), 0xfb)   # xbe
        self.e.setMemory(self.nextPC(), 0xc2)   # rep #$30 - A, X, Y all 16- bit
        self.e.setMemory(self.nextPC(), 0x30)
        self.e.setMemory(self.nextPC(), 0x08)   # php
        self.e.setMemory(self.nextPC(), 0xe2)   # sep #$30 - A, X, Y all 8- bit
        self.e.setMemory(self.nextPC(), 0x30)
        self.e.setMemory(self.nextPC(), 0x68)   # pla        
        self.e.setMemory(self.nextPC(), 0x8d)   # sta $9000
        self.e.setMemory(self.nextPC(), 0x00)
        self.e.setMemory(self.nextPC(), 0x90)
        self.e.setMemory(self.nextPC(), 0x60)   # rts
                
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getMemory(0x9000), 0x01)                       

    def test_LDA_immediate(self):
        # Arrange
        testValue = 0x34    

        start = 0x8000
        self.setPC(start)
        
        self.e.setMemory(self.nextPC(), 0x18)   # clc
        self.e.setMemory(self.nextPC(), 0xfb)   # xbe    
        self.e.setMemory(self.nextPC(), 0xe2)   # sep #$30 - A, X, Y all 8- bit
        self.e.setMemory(self.nextPC(), 0x30)
        self.e.setMemory(self.nextPC(), 0xa9)   # lda #$34
        self.e.setMemory(self.nextPC(), testValue)        
        self.e.setMemory(self.nextPC(), 0x60)   # rts
        
        # Act
        self.e.run(start)

        # Assert
        self.assertEqual(self.e.getA(), testValue)      

    def test_LDA_immediate_16(self):
        # Arrange
        start = 0x8000
        self.setPC(start)        

        self.e.setMemory(self.nextPC(), 0x18)   # clc
        self.e.setMemory(self.nextPC(), 0xfb)   # xbe
        self.e.setMemory(self.nextPC(), 0xC2)   # rep #$30 - A, X, Y all 16- bit
        self.e.setMemory(self.nextPC(), 0x30)
        self.e.setMemory(self.nextPC(), 0xa9)   # lda #$1234
        self.e.setMemory(self.nextPC(), 0x34)        
        self.e.setMemory(self.nextPC(), 0x12)        
        self.e.setMemory(self.nextPC(), 0x60)   # rts
        
        # Act
        self.e.run(start)

        # Assert
        self.assertEqual(self.e.getA_long(), 0x1234)          

    def test_LDA_absolute(self):
        # Arrange     
        testValue = 0xef

        start = 0x8000
        self.setPC(start)   

        self.e.setMemory(self.nextPC(), 0xe2)   # sep #$30 - A, X, Y all 8- bit
        self.e.setMemory(self.nextPC(), 0x30)
        self.e.setMemory(self.nextPC(), 0xad)   # lda $9000
        self.e.setMemory(self.nextPC(), 0x00)        
        self.e.setMemory(self.nextPC(), 0x90)        
        self.e.setMemory(self.nextPC(), 0x60)   # rts

        self.e.setMemory(0x9000, testValue)
        
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)  

    def test_LDA_absolute_long(self):
        # Arrange        
        testValue = 0xcf        
        
        start = 0x8000
        self.setPC(start)
        
        self.e.setMemory(self.nextPC(), 0xe2)   # sep #$30 - A, X, Y all 8- bit
        self.e.setMemory(self.nextPC(), 0x30)
        self.e.setMemory(self.nextPC(), 0xaf)   # lda $019000
        self.e.setMemory(self.nextPC(), 0x00)        
        self.e.setMemory(self.nextPC(), 0x90)        
        self.e.setMemory(self.nextPC(), 0x01)        
        self.e.setMemory(self.nextPC(), 0x60)   # rts

        self.e.setMemoryL(0x01, 0x9000, testValue)
        
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)        

    def test_LDA_direct_page(self):
        # Arrange    
        start = 0x8000
        self.setPC(start)    
        testValue = 0x1f       
        self.e.setMemory(self.nextPC(), 0xe2)   # sep #$30 - A, X, Y all 8- bit
        self.e.setMemory(self.nextPC(), 0x30) 
        self.e.setMemory(self.nextPC(), 0xa5)   # lda $05
        self.e.setMemory(self.nextPC(), 0x05)                
        self.e.setMemory(self.nextPC(), 0x60)   # rts

        self.e.setMemory(0x0005, testValue)
        
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)    

    def test_LDA_direct_page_indirect(self):
        # Arrange        
        start = 0x8000
        self.setPC(start)
        testValue = 0x2f        
        self.e.setMemory(self.nextPC(), 0xe2)   # sep #$30 - A, X, Y all 8- bit
        self.e.setMemory(self.nextPC(), 0x30)
        self.e.setMemory(self.nextPC(), 0xb2)   # lda ($04)
        self.e.setMemory(self.nextPC(), 0x04)                
        self.e.setMemory(self.nextPC(), 0x60)   # rts

        self.e.setMemory(0x0004, 0X00)
        self.e.setMemory(0x0005, 0X90)
        self.e.setMemory(0x9000, testValue)
        
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)   

    def test_LDA_direct_page_indirect_long(self):
        start = 0x8000
        self.setPC(start)        
        testValue = 0x4f        
        self.e.setMemory(self.nextPC(), 0xe2)   # sep #$30 - A, X, Y all 8- bit
        self.e.setMemory(self.nextPC(), 0x30)
        self.e.setMemory(self.nextPC(), 0xa7)   # lda [$04]
        self.e.setMemory(self.nextPC(), 0x04)                
        self.e.setMemory(self.nextPC(), 0x60)   # rts

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
        start = 0x8000
        self.setPC(start)
        testValue = 0x4f     
        testFailValue = 0x12   
        self.e.setMemory(self.nextPC(), 0xe2)   # sep #$30 - A, X, Y all 8- bit
        self.e.setMemory(self.nextPC(), 0x30)        
        self.e.setMemory(self.nextPC(), 0xa2)   # ldx #$01
        self.e.setMemory(self.nextPC(), 0x01)                
        self.e.setMemory(self.nextPC(), 0xbd)   # lda $9000,x
        self.e.setMemory(self.nextPC(), 0x00)
        self.e.setMemory(self.nextPC(), 0x90)
        self.e.setMemory(self.nextPC(), 0x60)   # rts

        self.e.setMemory(0x9000, testFailValue)
        self.e.setMemory(0x9001, testValue)
                
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)                                  
        self.assertNotEqual(self.e.getA(), testFailValue)                                  

    def test_LDA_absolute_X_long(self):
        start = 0x8000
        self.setPC(start)        
        testValue = 0x4f     
        testFailValue = 0x12   
        self.e.setMemory(self.nextPC(), 0xe2)   # sep #$30 - A, X, Y all 8- bit
        self.e.setMemory(self.nextPC(), 0x30)        
        self.e.setMemory(self.nextPC(), 0xa2)   # ldx #$01
        self.e.setMemory(self.nextPC(), 0x01)                
        self.e.setMemory(self.nextPC(), 0xbf)   # lda $019000,x
        self.e.setMemory(self.nextPC(), 0x00)
        self.e.setMemory(self.nextPC(), 0x90)
        self.e.setMemory(self.nextPC(), 0x01)
        self.e.setMemory(self.nextPC(), 0x60)   # rts

        self.e.setMemoryL(0x01, 0x9000, testFailValue)
        self.e.setMemoryL(0x01, 0x9001, testValue)
                
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)                                  
        self.assertNotEqual(self.e.getA(), testFailValue)                                  

    def test_LDA_absolute_Y(self):
        # Arrange        
        start = 0x8000
        self.setPC(start)
        testValue = 0x4f     
        testFailValue = 0x12   
        self.e.setMemory(self.nextPC(), 0xe2)   # sep #$30 - A, X, Y all 8- bit
        self.e.setMemory(self.nextPC(), 0x30)        
        self.e.setMemory(self.nextPC(), 0xa0)   # ldy #$01
        self.e.setMemory(self.nextPC(), 0x01)                
        self.e.setMemory(self.nextPC(), 0xb9)   # lda $9000,y
        self.e.setMemory(self.nextPC(), 0x00)
        self.e.setMemory(self.nextPC(), 0x90)
        self.e.setMemory(self.nextPC(), 0x60)   # rts

        self.e.setMemory(0x9000, testFailValue)
        self.e.setMemory(0x9001, testValue)
                
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)                                  
        self.assertNotEqual(self.e.getA(), testFailValue)                

    def test_LDA_direct_page_X(self):
        # Arrange        
        start = 0x8000
        self.setPC(start)
        testValue = 0x4f     
        testFailValue = 0x12   
        self.e.setMemory(self.nextPC(), 0xe2)   # sep #$30 - A, X, Y all 8- bit
        self.e.setMemory(self.nextPC(), 0x30)        
        self.e.setMemory(self.nextPC(), 0xa2)   # ldx #$01
        self.e.setMemory(self.nextPC(), 0x01)                
        self.e.setMemory(self.nextPC(), 0xb5)   # lda $05,x
        self.e.setMemory(self.nextPC(), 0x05)        
        self.e.setMemory(self.nextPC(), 0x60)   # rts

        self.e.setMemory(0x0005, testFailValue)
        self.e.setMemory(0x0006, testValue)
                
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)                                  
        self.assertNotEqual(self.e.getA(), testFailValue)                        

    def test_LDA_direct_page_indirect_X(self):
        # Arrange        
        start = 0x8000
        self.setPC(start)
        testValue = 0x4f             
        self.e.setMemory(self.nextPC(), 0xe2)   # sep #$30 - A, X, Y all 8- bit
        self.e.setMemory(self.nextPC(), 0x30)        
        self.e.setMemory(self.nextPC(), 0xa2)   # ldx #$01
        self.e.setMemory(self.nextPC(), 0x01)                
        self.e.setMemory(self.nextPC(), 0xa1)   # lda ($05,x)
        self.e.setMemory(self.nextPC(), 0x05)        
        self.e.setMemory(self.nextPC(), 0x60)   # rts

        self.e.setMemory(0x0006, 0x00)
        self.e.setMemory(0x0007, 0x90)
        self.e.setMemory(0x9000, testValue)
                
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)   

    def test_LDA_direct_page_indirect_indexed_Y(self):
        # Arrange        
        start = 0x8000
        self.setPC(start)
        testValue = 0x4f     
        testFailValue = 0x12           
        self.e.setMemory(self.nextPC(), 0xe2)   # sep #$30 - A, X, Y all 8- bit
        self.e.setMemory(self.nextPC(), 0x30)        
        self.e.setMemory(self.nextPC(), 0xa0)   # ldy #$01
        self.e.setMemory(self.nextPC(), 0x01)                
        self.e.setMemory(self.nextPC(), 0xb1)   # lda ($05), y
        self.e.setMemory(self.nextPC(), 0x05)        
        self.e.setMemory(self.nextPC(), 0x60)   # rts

        self.e.setMemory(0x0005, 0x00)
        self.e.setMemory(0x0006, 0x90)
        self.e.setMemory(0x9000, testFailValue)
        self.e.setMemory(0x9001, testValue)
                
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)  
        self.assertNotEqual(self.e.getA(), testFailValue)                                                         

    def test_LDA_direct_page_indirect_indexed_Y_long(self):
        # Arrange        
        start = 0x8000
        self.setPC(start)
        testValue = 0x4f     
        testFailValue = 0x12           
        self.e.setMemory(self.nextPC(), 0xe2)   # sep #$30 - A, X, Y all 8- bit
        self.e.setMemory(self.nextPC(), 0x30)        
        self.e.setMemory(self.nextPC(), 0xa0)   # ldy #$01
        self.e.setMemory(self.nextPC(), 0x01)                
        self.e.setMemory(self.nextPC(), 0xb7)   # lda [$05], y
        self.e.setMemory(self.nextPC(), 0x05)        
        self.e.setMemory(self.nextPC(), 0x60)   # rts

        self.e.setMemory(0x0005, 0x00)
        self.e.setMemory(0x0006, 0x90)
        self.e.setMemory(0x0007, 0x01)
        self.e.setMemoryL(0x01, 0x9000, testFailValue)
        self.e.setMemoryL(0x01, 0x9001, testValue)
                
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)  
        self.assertNotEqual(self.e.getA(), testFailValue)         

    def test_LDA_stack_relative(self):
        # Arrange        
        start = 0x8000
        self.setPC(start)
        testValue1 = 0x4f             
        testValue2 = 0xdd     
        self.e.setMemory(self.nextPC(), 0xe2)   # sep #$30 - A, X, Y all 8- bit
        self.e.setMemory(self.nextPC(), 0x30)        
        self.e.setMemory(self.nextPC(), 0xa0)   # ldy #testValue1
        self.e.setMemory(self.nextPC(), testValue1)                
        self.e.setMemory(self.nextPC(), 0x5a)   # phy
        self.e.setMemory(self.nextPC(), 0xa0)   # ldy #testValue2
        self.e.setMemory(self.nextPC(), testValue2)                
        self.e.setMemory(self.nextPC(), 0x5a)   # phy
        self.e.setMemory(self.nextPC(), 0xa3)   # lda 1, S      - The last thing pushed
        self.e.setMemory(self.nextPC(), 0x01)
        self.e.setMemory(self.nextPC(), 0xaa)   # tax
        self.e.setMemory(self.nextPC(), 0xa3)   # lda 2, S      - The 2nd to last thing pushed
        self.e.setMemory(self.nextPC(), 0x02)        
        self.e.setMemory(self.nextPC(), 0x7a)   # ply
        self.e.setMemory(self.nextPC(), 0x7a)   # ply
        self.e.setMemory(self.nextPC(), 0x60)   # rts
                        
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue1)  
        self.assertEqual(self.e.getX(), testValue2)  

    def test_LDA_stack_relative_Y(self):
        # Arrange        
        start = 0x8000
        self.setPC(start)
        testValue = 0x4f             
        testFailValue = 0xdd     
        self.e.setMemory(self.nextPC(), 0xe2)   # sep #$30 - A, X, Y all 8- bit
        self.e.setMemory(self.nextPC(), 0x30)        
        self.e.setMemory(self.nextPC(), 0xa0)   # ldy #$90
        self.e.setMemory(self.nextPC(), 0x90)                
        self.e.setMemory(self.nextPC(), 0x5a)   # phy
        self.e.setMemory(self.nextPC(), 0xa0)   # ldy #00
        self.e.setMemory(self.nextPC(), 0x00)                
        self.e.setMemory(self.nextPC(), 0x5a)   # phy
        self.e.setMemory(self.nextPC(), 0xa0)   # ldy #$90
        self.e.setMemory(self.nextPC(), 0x90)                
        self.e.setMemory(self.nextPC(), 0x5a)   # phy
        self.e.setMemory(self.nextPC(), 0xa0)   # ldy #80
        self.e.setMemory(self.nextPC(), 0x80)                
        self.e.setMemory(self.nextPC(), 0x5a)   # phy        
        self.e.setMemory(self.nextPC(), 0xa0)   # ldy #01
        self.e.setMemory(self.nextPC(), 0x01)            
        self.e.setMemory(self.nextPC(), 0xb3)   # lda (3, S), y - The last thing pushed is the pointer.  Push int HL (not LH)
        self.e.setMemory(self.nextPC(), 0x03)        
        self.e.setMemory(self.nextPC(), 0x7a)   # ply
        self.e.setMemory(self.nextPC(), 0x7a)   # ply
        self.e.setMemory(self.nextPC(), 0x7a)   # ply
        self.e.setMemory(self.nextPC(), 0x7a)   # ply
        self.e.setMemory(self.nextPC(), 0x60)   # rts
        
        
        self.e.setMemory(0x9000, testFailValue)
        self.e.setMemory(0x9001, testValue)
        self.e.setMemory(0x9080, testFailValue)
        self.e.setMemory(0x9081, testFailValue)
                        
        # Act
        self.e.run(0x8000)

        # Assert
        self.assertEqual(self.e.getA(), testValue)  
        self.assertNotEqual(self.e.getA(), testFailValue)  

if __name__ == '__main__':
    unittest.main()