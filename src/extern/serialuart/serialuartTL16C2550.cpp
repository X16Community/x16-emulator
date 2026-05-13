/*!
\file     serialuartTL16C2550.cpp
\brief    Source file of the class serialuartTL16C2550. A serial UART for Commander X16 Emulator
\author   Jason Hill
\version  0.3
\date     September 18th of 2025, by Jason Hill
\modified March 2026 – Added 16-byte RX/TX FIFOs, threading, and full TL16C2550 interrupt system

This Serial library is used for communication of a physical serial device on the X16 emulator. Simulating
some aspects of the TL16C2550 for use on personal computers.

Description: This file simulates the Texas Instruments TL16C2550 UART as faithfully as an OS-level
serial port allows.  The implementation adds:

  - True 16-byte RX and TX FIFOs (std::deque, capacity-limited to UART_HW_FIFO_SIZE).
  - Per-character error flags (OE/PE/FE/BI) stored with each RX FIFO entry and exposed
    through LSR only when the flagged character is at the head of the FIFO.
  - Full TL16C2550 interrupt priority chain:
        1. Receiver Line Status   (OE/PE/FE/BI)     – highest priority
        2. Received Data Available / FIFO trigger
        3. Character Timeout      (FIFO idle 4 char-times)
        4. Transmitter Holding Register Empty
        5. Modem Status change                       – lowest priority
  - IIR correctly encodes the highest-priority pending source; bits 7:6 reflect
    FIFO-enabled state.
  - IRQ output gated by MCR OUT2 (bit 3), exposed via irqPending().
  - MSR delta bits (DCTS/DDSR) set on modem signal transitions, cleared on MSR read.
  - Thread-safe FIFO access: threadCycleUpdate() pushes RX / drains TX from a background
    thread; addrread() / addrwrite() called/run on the emulator thread.

On hardware, UART1 is connected to an ESP32 on the X16 Serial/Wifi Card. An ESP8266 or
ESP32 with appropriate firmware must be attached to a host COM port for the same function.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE X CONSORTIUM BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

This is a licence-free software, it can be used by anyone who try to build a better world.
*/

//#define DEBUGUART
//#define VERBOSEUART

#include "serialuartTL16C2550.hpp"

// ===========================================================================
// Constructor / Destructor
// ===========================================================================

serialuartTL16C2550::serialuartTL16C2550():
      IIR(IIR_NO_INT), IER(0), FCR(0), MCR(0), LCR(0), LSR(LSR_THRE | LSR_TEMT),
      MSR(0), scratchReg(0), DLSB(0x60), DMSB(0), requestedDivisor(0x60),
      loopvalue(0),
      irqRxLineStatus(false), irqRxDataReady(false), irqCharTimeout(false),
      irqThrEmpty(false), irqModemStatus(false),
      rxTimeoutTicks(0), rxTimeoutThreshold(40), ticksPerChar(4),
      prevCTS(false), prevDSR(false),
      quit(false), hasCTS(false), hasDSR(false), availCount(0),
      checksperbaud(5),	//At least (2)twice as fast as baud rate for no delays, 5 is plenty fast
	  fifo_level(1),
      baud_delay(std::chrono::nanoseconds((1000000000 / 9600) / 10))
{
	//Nothing to see here - see initilization list.
}

serialuartTL16C2550::~serialuartTL16C2550(){
    quit = true;
    if (t.joinable()) {
        t.join();
    }
    this->serialPort.closeDevice();
}

// ===========================================================================
// irqPending
//
// The TL16C2550 drives its IRQ pin low when any enabled interrupt source is
// active AND MCR bit 3 (OUT2) is set.
// ===========================================================================
bool serialuartTL16C2550::irqPending() const {
    if (!(MCR & MCR_OUT2))
        return false;                // IRQ output tri-stated when OUT2 = 0
    return (IIR & 0x01) == 0;       // IIR bit 0 = 0 means interrupt pending
}

// ===========================================================================
// updateIIR
//
// Rebuilds IIR from the five interrupt-pending flags and the IER mask.
// Must be called after any change to the flags, IER, FCR, or RX FIFO depth.
//
// Priority (TL16C2550 datasheet Table 3):
//   1 = RLS   – Receiver Line Status
//   2 = RDA   – Received Data Available / FIFO trigger
//   3 = CTI   – Character Timeout Indication (FIFO mode only)
//   4 = THRE  – Transmitter Holding Register Empty
//   5 = MSR   – Modem Status Register
// ===========================================================================
void serialuartTL16C2550::updateIIR(){
    unsigned char newIIR = IIR_NO_INT;  // default: no interrupt pending

    // Priority 1 – Receiver Line Status
    if ((IER & IER_ELSI) && irqRxLineStatus) {
        newIIR = IIR_RX_LINE_STAT;
    }
    // Priority 2 – Received Data Available (FIFO at or above trigger level)
    else if ((IER & IER_ERBFI) && irqRxDataReady) {
        newIIR = IIR_RX_DATA;
    }
    // Priority 3 – Character Timeout (FIFO mode, stale data)
    else if ((IER & IER_ERBFI) && irqCharTimeout) {
        newIIR = IIR_CHAR_TIMEOUT;
    }
    // Priority 4 – Transmitter Holding Register Empty
    else if ((IER & IER_ETBEI) && irqThrEmpty) {
        newIIR = IIR_THR_EMPTY;
    }
    // Priority 5 – Modem Status
    else if ((IER & IER_EDSSI) && irqModemStatus) {
        newIIR = IIR_MODEM_STATUS;
    }

    // Bits 7:6 indicate FIFO enabled (set to 11 when FCR bit 0 = 1)
    if (FCR & FCR_FIFO_EN) {
        newIIR |= IIR_FIFO_BITS;
    } else {
        newIIR &= ~IIR_FIFO_BITS;
    }

    IIR = newIIR;
}

// ===========================================================================
// computeLSR
//
// Builds the live LSR value from the current FIFO state plus the 'sticky'
// error bits that are held in the LSR member until LSR is read.
//
// The TL16C2550 reports per-character error flags (PE/FE/BI) only for the
// character currently at the head of the RX FIFO.  OE is set as soon as an
// overrun occurs and is also 'sticky' until LSR is read.
// ===========================================================================
unsigned char serialuartTL16C2550::computeLSR() const {
    // Start from the 'sticky' error bits (OE/PE/FE/BI survive until LSR read)
    unsigned char lsr = LSR & (LSR_OE | LSR_PE | LSR_FE | LSR_BI);

    // DR – data ready: at least one byte in RX FIFO
    if (!rxFifo.empty()) {
        lsr |= LSR_DR;
        // Per-character errors from the head entry
        lsr |= rxFifo.front().err & (LSR_PE | LSR_FE | LSR_BI);
    }

    // THRE – TX FIFO (or holding register) empty
    if (txFifo.empty()) {
        lsr |= LSR_THRE;
    }

    // TEMT – transmitter completely empty (FIFO + shift register)
    // We don't model the shift register separately; treat TEMT = THRE.
    if (txFifo.empty()) {
        lsr |= LSR_TEMT;
    }

    // RXFE (bit 7) – FIFO mode only: at least one error in any FIFO entry
    if (FCR & FCR_FIFO_EN) {
        for (const auto &entry : rxFifo) {
            if (entry.err) {
                lsr |= LSR_RXFE;
                break;
            }
        }
    }

    return lsr;
}

// ===========================================================================
// rxFIFOPush
//
// background threadCycleUpdate()
// Pushes one received byte into the RX FIFO.  If the FIFO is full an overrun
// is declared: LSR OE is set, the line-status interrupt is armed, and the new
// byte is discarded (matching TL16C2550 behaviour).
// ===========================================================================
void serialuartTL16C2550::rxFifoPush(unsigned char data, unsigned char errFlags){
    std::lock_guard<std::mutex> lock(rxMutex);

    int cap = (FCR & FCR_FIFO_EN) ? UART_HW_FIFO_SIZE : 1;

    if ((int)rxFifo.size() >= cap) {
        // Overrun: set OE in 'sticky' LSR bits
        LSR |= LSR_OE;
        irqRxLineStatus = true;
        updateIIR();
        return;     // discard the new byte per TL16C2550 datasheet
    }

    rxFifo.push_back({data, errFlags});

    // Any per-character error arms the line-status interrupt
    if (errFlags & (LSR_PE | LSR_FE | LSR_BI)) {
        irqRxLineStatus = true;
    }

    // Check RX trigger level for DataAvailable interrupt
    int trigger = (int)fifo_level;  // 1, 4, 8, or 14
    if ((int)rxFifo.size() >= trigger) {
        irqRxDataReady = true;
        irqCharTimeout = false;     // DataAvailable takes priority; reset timeout
        rxTimeoutTicks = 0;
    }

    // Any new byte resets the character-timeout counter
    rxTimeoutTicks = 0;
    irqCharTimeout = false;

    updateIIR();
}

// ===========================================================================
// rxFIFOPop
//
// background threadCycleUpdate()
// 
// Removes and returns the head entry.  Updates DR, RXFE, DataAvailable, and timeout
// interrupt state after the pop.
// ===========================================================================
bool serialuartTL16C2550::rxFifoPop(UartRxEntry &out){
    std::lock_guard<std::mutex> lock(rxMutex);

    if (rxFifo.empty())
        return false;

    out = rxFifo.front();
    rxFifo.pop_front();

    // If FIFO fell below trigger level, clear DataAvailable interrupt
    if ((int)rxFifo.size() < (int)fifo_level) {
        irqRxDataReady = false;
    }

    // If FIFO is now empty, clear timeout interrupt and reset counter
    if (rxFifo.empty()) {
        irqCharTimeout = false;
        rxTimeoutTicks = 0;
    }

    // Re-evaluate line-status interrupt:
    // irqRxLineStatus stays set until LSR is read 'sticky', but if the
    // head-character error was the only trigger and the head just changed,
    // the new head's errors will be reported on the next LSR read.

    updateIIR();
    return true;
}

// ===========================================================================
// rxFifoReset  (FCR bit 1)
// ===========================================================================
void serialuartTL16C2550::rxFifoReset(){
    std::lock_guard<std::mutex> lock(rxMutex);
    rxFifo.clear();
    irqRxDataReady = false;
    irqCharTimeout = false;
    rxTimeoutTicks = 0;
    // Clear DR and RXFE in the 'sticky' LSR; leave OE/PE/FE/BI until LSR read
    LSR &= ~LSR_DR;
    updateIIR();
}

// ===========================================================================
// txFifoReset  (FCR bit 2)
// ===========================================================================
void serialuartTL16C2550::txFifoReset(){
    {
        std::lock_guard<std::mutex> lock(txMutex);
        txFifo.clear();
    }
    // Empty TX → arm THRE interrupt
    irqThrEmpty = true;
    LSR |= (LSR_THRE | LSR_TEMT);
    updateIIR();
}

// ===========================================================================
// updateMSR
//
// Called by threadCycleUpdate() when CTS or DSR changes.  Sets the corresponding
// delta bits in MSR (DCTS = bit 0, DDSR = bit 1) and arms the modem-status
// interrupt.  The delta bits are 'sticky' until MSR is read.
// ===========================================================================
void serialuartTL16C2550::updateMSR(bool newCTS, bool newDSR){
    bool changed = false;

    if (newCTS != prevCTS) {
        MSR |= 0x01;   // DCTS
        changed = true;
        prevCTS = newCTS;
    }
    if (newDSR != prevDSR) {
        MSR |= 0x02;   // DDSR
        changed = true;
        prevDSR = newDSR;
    }

    // Update the current-state bits (bits 5:4 = DSR/CTS on TL16C2550)
    if (newCTS) MSR |=  0x20; else MSR &= ~0x20;
    if (newDSR) MSR |=  0x10; else MSR &= ~0x10;

    if (changed) {
        irqModemStatus = true;
        updateIIR();
    }
}

// ===========================================================================
// threadCycleUpdate  (background thread)
//
// Runs at roughly x2 the baud rate.  Tasks:
//   1. Drain the TX FIFO to the host serial port (respecting AFE/CTS).
//   2. Push available bytes from the host port into the RX FIFO.
//   3. Advance the character-timeout counter and fire the timeout interrupt
//      if the FIFO has been idle for RX_TIMEOUT_CHARS character-times.
//   4. Manage RTS for automatic flow control (MCR AFE bit).
//   5. Detect CTS/DSR transitions and update MSR/interrupt.
// ===========================================================================
void serialuartTL16C2550::threadCycleUpdate(){
    while (!this->quit) {

            // ----------------------------------------------------------------
            // 1. Refresh modem signal states and update MSR / IRQ
            // ----------------------------------------------------------------
            bool newCTS = this->serialPort.isCTS();
            bool newDSR = this->serialPort.isDSR();
            hasCTS = newCTS;
            hasDSR = newDSR;
            updateMSR(newCTS, newDSR);

            // ----------------------------------------------------------------
            // 2. TX: drain TX FIFO → host serial port
            //    In AFE mode, only send when CTS is asserted.
            // ----------------------------------------------------------------
            {
                std::lock_guard<std::mutex> lock(txMutex);
                if (!txFifo.empty()) {
                    bool allowSend = true;
                    if (MCR & MCR_AFE) {
                        allowSend = hasCTS;
                    }
                    if (allowSend) {
                        unsigned char txByte = txFifo.front();
                        txFifo.pop_front();

                        // Write to physical port
                        this->serialPort.writeBytes(&txByte, 1);

                        // If FIFO just became empty, arm THRE interrupt
                        if (txFifo.empty()) {
                            irqThrEmpty = true;
                            LSR |= (LSR_THRE | LSR_TEMT);
                            updateIIR();
                        }
                    }
                }
            }

            // ----------------------------------------------------------------
            // 3. RX: pump host port → RX FIFO
            //    Read up to (FIFO capacity – current occupancy) bytes so we
            //    never overflow the FIFO in one tick.
            // ----------------------------------------------------------------
            availCount = this->dataAvailable();
            if (availCount > 0) {
                int cap      = (FCR & FCR_FIFO_EN) ? UART_HW_FIFO_SIZE : 1;
                int occupied;
                {
                    std::lock_guard<std::mutex> lock(rxMutex);
                    occupied = (int)rxFifo.size();
                }
                int canReceive = cap - occupied;

                for (int i = 0; i < canReceive && i < availCount; ++i) {
                    unsigned char rxByte = 0;
                    int result = this->serialPort.readBytes(&rxByte, 1, 0, 0);
                    if (result <= 0)
                        break;
                    // Push with no error flags; physical parity/framing errors
                    // are surfaced by the host OS and could be added here if the
                    // OS API exposes them (platform-specific extension point).
                    rxFifoPush(rxByte, 0);
                }
            }

            // ----------------------------------------------------------------
            // 4. Character Timeout
            //    If the RX FIFO is not empty and no new byte has arrived for
            //    RX_TIMEOUT_CHARS character-times, assert the timeout interrupt.
            //    "One character-time" ≈ ticksPerChar background ticks.
            // ----------------------------------------------------------------
            {
                std::lock_guard<std::mutex> lock(rxMutex);
                if (!rxFifo.empty() && !irqRxDataReady) {
                    rxTimeoutTicks++;
                    if (rxTimeoutTicks >= rxTimeoutThreshold && !irqCharTimeout) {
                        irqCharTimeout = true;
                        updateIIR();
                    }
                }
                // If FIFO drained to empty between ticks, reset the counter.
                if (rxFifo.empty()) {
                    rxTimeoutTicks = 0;
                    irqCharTimeout = false;
                }
            }

            // ----------------------------------------------------------------
            // 5. AFE automatic RTS control
            //    Drop RTS when the RX FIFO reaches the trigger level so the
            //    sender pauses; raise it again once the FIFO drains below trigger.
            // ----------------------------------------------------------------
            if (MCR & MCR_AFE) {
                std::lock_guard<std::mutex> lock(rxMutex);
                if ((int)rxFifo.size() >= (int)fifo_level) {
                    if (serialPort.isRTS()) {
                        this->serialPort.clearRTS();
                    }
                } else {
                    if (!serialPort.isRTS()) {
                        this->serialPort.setRTS();
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::nanoseconds(500));
            if (this->quit)
                break;
    }
}

// ===========================================================================
// addrwrite - address write
//
// Called by the emulator when 6502 code writes to a UART register address.
// address bits 2:0 select the register; bit 3 (or higher) selects the channel
// on multi-channel devices – this class models one channel so we mask to 0x07.
// ===========================================================================
int serialuartTL16C2550::addrwrite(unsigned char *value, int address){
    int retVal = 0;

#ifdef DEBUGUART
    if (address != 0) {
        printf("WriteAddress: $%02x\tValue: $%02x", address, (int)(*value));
        printf("\t-----\tIER: $%02x   IIR: $%02x   MCR: $%02x   LSR: $%02x   MSR: $%02x"
               "   Scratch: $%02x   LCR: $%02x   DLSB: $%02x   DMSB: $%02x",
               (int)IER, (int)IIR, (int)MCR, (int)LSR, (int)MSR,
               (int)scratchReg, (int)LCR, (int)DLSB, (int)DMSB);
        printf("   Divisor: %d\tBaud: %d\n",
               requestedDivisor, baudCalculator(requestedDivisor));
    }
#endif

    bool LCRconfigDirty = false;

    switch (address & 0x07) {

        // -------------------------------------------------------------------
        // Offset 0 – Transmit Holding Register (DLAB=0) / DLL (DLAB=1)
        // -------------------------------------------------------------------
        case 0:
            if (LCR & 0x80 /* DLAB = 1 */) {
                DLSB   = *value;
                retVal = DLSB;
            } else {
                if (MCR & MCR_LOOPBACK) {
                    // Loopback: route the byte directly into the RX FIFO.
                    loopvalue = *value;
                    rxFifoPush(loopvalue, 0);
                    retVal = 1;
                } else {
                    // Push into TX FIFO (respect capacity)
                    {
                        std::lock_guard<std::mutex> lock(txMutex);
                        int cap = (FCR & FCR_FIFO_EN) ? UART_HW_FIFO_SIZE : 1;
                        if ((int)txFifo.size() < cap) {
                            txFifo.push_back(*value);
                            retVal = 1;
                        } else {
                            retVal = 0;   // TX FIFO full – byte dropped (caller should check THRE)
                        }
                    }
                    // Writing to THR clears the THRE interrupt
                    irqThrEmpty = false;
                    LSR &= ~(LSR_THRE | LSR_TEMT);
                    updateIIR();
                }
            }
            break;

        // -------------------------------------------------------------------
        // Offset 1 – Interrupt Enable Register (DLAB=0) / DLM (DLAB=1)
        // -------------------------------------------------------------------
        case 1:
            if (LCR & 0x80 /* DLAB = 1 */) {
                DMSB   = *value;
                retVal = DMSB;
            } else {
                IER = 0x0F & (*value);
                // If ETBEI was just enabled and TX FIFO is already empty,
                // immediately arm the THRE interrupt.
                if ((IER & IER_ETBEI) && txFifo.empty()) {
                    irqThrEmpty = true;
                }
                updateIIR();
                retVal = IER;
            }
            break;

        // -------------------------------------------------------------------
        // Offset 2 – FIFO Control Register (write-only)
        //
        // FCR is write-only on real hardware; the shadow copy is kept in FCR.
        // Bit 0: FIFO enable.  Bits 1/2: RX/TX FIFO reset.  Bits 7:6: RX trigger.
        // -------------------------------------------------------------------
        case 2: {
            unsigned char oldFCR = FCR;
            FCR = 0xCF & (*value);      // bits 4:3 are not used on TL16C2550

            // Disabling FIFOs also clears them (datasheet §9.3)
            if ((oldFCR & FCR_FIFO_EN) && !(FCR & FCR_FIFO_EN)) {
                rxFifoReset();
                txFifoReset();
            }

            // Explicit reset commands (can be issued even while enabling)
            if (FCR & FCR_RX_RESET) {
                rxFifoReset();
                FCR &= ~FCR_RX_RESET;   // self-clearing
            }
            if (FCR & FCR_TX_RESET) {
                txFifoReset();
                FCR &= ~FCR_TX_RESET;   // self-clearing
            }

            // Decode RX trigger level from bits 7:6
            switch (FCR & FCR_RX_TRIG) {
                case 0x00: fifo_level = 1;  break;
                case 0x40: fifo_level = 4;  break;
                case 0x80: fifo_level = 8;  break;
                case 0xC0: fifo_level = 14; break;
                default:   fifo_level = 1;  break;
            }

            // Compute character-timeout threshold in threadCycleUpdate ticks.
            // One character-time at checksperbaud ticks per baud event ≈
            // checksperbaud ticks.  Four character-times:
            ticksPerChar       = checksperbaud;
            rxTimeoutThreshold = RX_TIMEOUT_CHARS * ticksPerChar;

            updateIIR();
            retVal = FCR;
            break;
        }

        // -------------------------------------------------------------------
        // Offset 3 – Line Control Register
        // -------------------------------------------------------------------
        case 3:
            // Detect exit from Divisor Latch Access mode → baud rate change
            if (LCR & 0x80) {                           // currently DLAB=1
                if (!((*value) & 0x80)) {               // transitioning to DLAB=0
                    requestedDivisor  = DLSB;
                    requestedDivisor |= ((uint16_t)(DMSB) << 8);
                    LCRconfigDirty    = true;
                }
            }
            // Detect change in word/parity/stop bits
            if ((LCR & 0x7F) != ((*value) & 0x7F)) {
                LCRconfigDirty = true;
            }

            LCR = *value;

            if (LCRconfigDirty && !(LCR & 0x80)) {
                reconfigureSerial();
            }
            retVal = LCR;
            break;

        // -------------------------------------------------------------------
        // Offset 4 – Modem Control Register
        // -------------------------------------------------------------------
        case 4:
            if (*value != MCR) {
                MCR = *value;

                // DTR pin
                if (MCR & MCR_DTR) {
                    if (!serialPort.isDTR()) this->serialPort.DTR(true);
                } else {
                    if ( serialPort.isDTR()) this->serialPort.DTR(false);
                }

                // RTS pin (only when not in AFE mode; AFE manages it automatically)
                if (!(MCR & MCR_AFE)) {
                    if (MCR & MCR_RTS) {
                        if (!serialPort.isRTS()) this->serialPort.RTS(true);
                    } else {
                        if ( serialPort.isRTS()) this->serialPort.RTS(false);
                    }
                }

                // Loopback mode: when entering loopback the MSR input bits
                // are driven by the MCR output bits (OUT2→DCD, OUT1→RI,
                // RTS→CTS, DTR→DSR) – see TL16C2550 datasheet §7.6
                if (MCR & MCR_LOOPBACK) {
                    bool lbCTS = (MCR & MCR_RTS)  != 0;
                    bool lbDSR = (MCR & MCR_DTR)  != 0;
                    updateMSR(lbCTS, lbDSR);
                }

                // If ETBEI is enabled and TX is now empty, arm THRE
                if ((IER & IER_ETBEI) && txFifo.empty()) {
                    irqThrEmpty = true;
                    updateIIR();
                }
            }
            retVal = MCR;
            break;

        // -------------------------------------------------------------------
        // Offset 5 – Line Status Register (read-only)
        // -------------------------------------------------------------------
        case 5:
            retVal = -5;    // writes ignored
            break;

        // -------------------------------------------------------------------
        // Offset 6 – Modem Status Register (read-only)
        // -------------------------------------------------------------------
        case 6:
            retVal = -6;    // writes ignored
            break;

        // -------------------------------------------------------------------
        // Offset 7 – Scratch Register
        // -------------------------------------------------------------------
        case 7:
            scratchReg = *value;
            retVal     = 0;
            break;

        default:
            break;
    }
    return retVal;
}

// ===========================================================================
// addrread - Address Read
//
// Called by the emulator when 6502 code reads from a UART register address.
// ===========================================================================
int serialuartTL16C2550::addrread(unsigned char *value, int address){
    int retVal = -1;

#ifdef DEBUGUART
    if (address != 0) {
        printf("ReadAddress: $%02x", address);
        printf("\tFCR:$%02x\t\tIER: $%02x   IIR: $%02x   MCR: $%02x   LSR: $%02x"
               "   MSR: $%02x   Scratch: $%02x   LCR: $%02x   DLSB: $%02x   DMSB: $%02x",
               (int)FCR, (int)IER, (int)IIR, (int)MCR, (int)LSR, (int)MSR,
               (int)scratchReg, (int)LCR, (int)DLSB, (int)DMSB);
        printf("\n");
    }
#endif

    switch (address & 0x07) {

        // -------------------------------------------------------------------
        // Offset 0 – Receive Buffer Register (DLAB=0) / DLL (DLAB=1)
        // -------------------------------------------------------------------
        case 0:
            if (LCR & 0x80 /* DLAB = 1 */) {
                *value = DLSB;
                retVal = 1;
            } else {
                if (MCR & MCR_LOOPBACK) {
                    // In loopback the RX FIFO is used, so fall through to normal pop.
                    // (The loopvalue byte was already pushed into the FIFO on write.)
                }
                UartRxEntry entry{};
                if (rxFifoPop(entry)) {
                    *value = entry.data;
                    retVal = entry.data;
                    // Reading RBR clears the character-timeout interrupt
                    irqCharTimeout = false;
                    updateIIR();
                } else {
                    *value = 0;
                    retVal = -1;
                }
            }
            break;

        // -------------------------------------------------------------------
        // Offset 1 – Interrupt Enable Register (DLAB=0) / DLM (DLAB=1)
        // -------------------------------------------------------------------
        case 1:
            if (LCR & 0x80 /* DLAB = 1 */) {
                *value = DMSB;
                retVal = 1;
            } else {
                *value = IER;
                retVal = 1;
            }
            break;

        // -------------------------------------------------------------------
        // Offset 2 – Interrupt Identification Register (read-only)
        //
        // Reading IIR when THRE is the reported source clears the THRE interrupt
        // (it will re-arm automatically the next time the TX FIFO drains to empty).
        // -------------------------------------------------------------------
        case 2:
            // Refresh IIR before returning it
            updateIIR();
            *value = IIR;
            retVal = 1;

            // If THRE was the reported interrupt, clear it; it re-arms on next drain
            if ((IIR & 0x0E) == IIR_THR_EMPTY) {
                irqThrEmpty = false;
                updateIIR();
            }
            break;

        // -------------------------------------------------------------------
        // Offset 3 – Line Control Register
        // -------------------------------------------------------------------
        case 3:
            *value = LCR;
            retVal = 1;
            break;

        // -------------------------------------------------------------------
        // Offset 4 – Modem Control Register
        // -------------------------------------------------------------------
        case 4:
            *value = MCR & 0x1F;    // bits 7:5 are not implemented
            retVal = 1;
            break;

        // -------------------------------------------------------------------
        // Offset 5 – Line Status Register
        //
        // Reading LSR:
        //   • Reports live FIFO state (DR, THRE, TEMT, RXFE)
        //   • Clears the Receiver Line Status interrupt and 'sticky' OE/PE/FE/BI bits
        // -------------------------------------------------------------------
        case 5:
            *value = computeLSR();
            retVal = 1;
            // Clear 'sticky' error bits and the line-status interrupt
            LSR &= ~(LSR_OE | LSR_PE | LSR_FE | LSR_BI);
            irqRxLineStatus = false;
            updateIIR();
            break;

        // -------------------------------------------------------------------
        // Offset 6 – Modem Status Register
        //
        // Reading MSR:
        //   • Reports current CTS/DSR and delta bits
        //   • Clears delta bits (DCTS/DDSR/TERI/DDCD) and the modem-status interrupt
        // -------------------------------------------------------------------
        case 6:
            // Bits 7:4 are current modem input states (already maintained in MSR)
            // Bits 3:0 are delta bits – cleared on read
            *value = MSR;
            retVal = MSR;
            MSR   &= 0xF0;          // clear all delta bits
            irqModemStatus = false;
            updateIIR();
            break;

        // -------------------------------------------------------------------
        // Offset 7 – Scratch Register
        // -------------------------------------------------------------------
        case 7:
            *value = scratchReg;
            retVal = 1;
            break;

        default:
            retVal = -2;
            break;
    }
    return retVal;
}

// ===========================================================================
// reconfigureSerial
//
// Closes and re-opens the host serial port with the settings currently
// encoded in LCR and requestedDivisor. Restarts the threadCycleUpdate thread.
// ===========================================================================
void serialuartTL16C2550::reconfigureSerial(){
    SerialDataBits Databits;
    SerialStopBits Stopbits;
    SerialParity   Parity;
    int wordLength = LCR & 0x03;

    if (this->serialPort.isDeviceOpen()) {
        this->quit = true;
        if (t.joinable()) t.join();
        this->quit = false;
        this->serialPort.closeDevice();
    }

    // Data bits
    switch (wordLength) {
        case 0:  Databits = SERIAL_DATABITS_5; break;
        case 1:  Databits = SERIAL_DATABITS_6; break;
        case 2:  Databits = SERIAL_DATABITS_7; break;
        default: Databits = SERIAL_DATABITS_8; break;
    }

    // Stop bits
    if (LCR & 0x04) {
        Stopbits = (wordLength == 0) ? SERIAL_STOPBITS_1_5 : SERIAL_STOPBITS_2;
    } else {
        Stopbits = SERIAL_STOPBITS_1;
    }

    // Parity
    if (LCR & 0x08) {
        if (LCR & 0x20) {
            Parity = (LCR & 0x10) ? SERIAL_PARITY_SPACE : SERIAL_PARITY_MARK;
        } else {
            Parity = (LCR & 0x10) ? SERIAL_PARITY_EVEN : SERIAL_PARITY_ODD;
        }
    } else {
        Parity = SERIAL_PARITY_NONE;
    }

    unsigned int baudRate = baudCalculator(requestedDivisor);
    int errorOpening = this->serialPort.openDevice(path.c_str(), baudRate, Databits, Parity, Stopbits);
    this->serialPort.DTR(MCR & MCR_DTR);
    this->serialPort.RTS(MCR & MCR_AFE);   // AFE controls RTS dynamically

    if (errorOpening != 1) {
        std::cerr << errorOpening << ": Error opening: " << path << std::endl;
        switch (errorOpening) {
            case -1: std::cerr << "device not found\n";                  break;
            case -2: std::cerr << "error while opening the device\n";    break;
            case -3: std::cerr << "error while getting port parameters\n"; break;
            case -4: std::cerr << "Speed (Bauds) not recognized\n";      break;
            case -5: std::cerr << "error while writing port parameters\n"; break;
            case -6: std::cerr << "error while writing timeout parameters\n"; break;
            case -7: std::cerr << "Databits not recognized\n";           break;
            case -8: std::cerr << "Stopbits not recognized\n";           break;
            case -9: std::cerr << "Parity not recognized\n";             break;
            default: break;
        }
    } else {
        std::cerr << "LCR: $" << std::hex << (int)LCR << std::dec
                  << "\t     Baud: "  << baudRate
                  << "\tData bits: "  << 5 + (int)Databits
                  << "\tParity: "     << descParity(static_cast<int>(Parity))
                  << "\tStopbits: "   << descStopbits(static_cast<int>(Stopbits))
                  << "\tDivisor: "    << requestedDivisor
                  << std::endl;
    }

    // Update baud-rate timing for threadCycleUpdate
    baud_delay = std::chrono::nanoseconds((1000000000 / baudRate) / checksperbaud);

    // ticksPerChar: at checksperbaud ticks-per-baud-event,
    // one character ≈ (bits_per_char) ticks.  Use 10 for 8N1.
    ticksPerChar       = checksperbaud;
    rxTimeoutThreshold = RX_TIMEOUT_CHARS * ticksPerChar;

    // Restart the background thread
    t = std::thread(&serialuartTL16C2550::threadCycleUpdate, this);
}

// ===========================================================================
// initialization
// ===========================================================================
int serialuartTL16C2550::init(char *port){
    // Power-on defaults (for instance -> ROMTERM.PRG checks IER/LCR/MCR = 0, and scratch register write, to detect the card)
    IIR            = IIR_NO_INT;
    IER            = 0;
    FCR            = 0;
    MCR            = 0;
    LCR            = 0;
    LSR            = LSR_THRE | LSR_TEMT;
    MSR            = 0;
    DLSB           = 0x60;   // default on emulator launch/reset: 9600 baud with 14.7456 MHz crystal
    DMSB           = 0;
    requestedDivisor = 0x60;
    loopvalue      = 0;

    irqRxLineStatus = false;
    irqRxDataReady  = false;
    irqCharTimeout  = false;
    irqThrEmpty     = false;
    irqModemStatus  = false;

    rxTimeoutTicks      = 0;
    ticksPerChar        = checksperbaud;
    rxTimeoutThreshold  = RX_TIMEOUT_CHARS * ticksPerChar;
    prevCTS             = false;
    prevDSR             = false;
    hasCTS              = false;
    hasDSR              = false;
    availCount          = 0;
    fifo_level          = 1;

    {//Block Scope
        std::lock_guard<std::mutex> lk(rxMutex);
        rxFifo.clear();
    }//mutex released
    {//Block Scope
        std::lock_guard<std::mutex> lk(txMutex);
        txFifo.clear();
    }//mutex released

    if (this->serialPort.isDeviceOpen()) {
        this->quit = true;
        if (t.joinable()) t.join();
        this->quit = false;
        this->serialPort.closeDevice();
    }

    // Build the path string (Windows COM >9 requires \\.\\ prefix)
#if defined(_WIN32) || defined(_WIN64)
    path = "\\\\.\\";
#else
    path = "";
#endif
    path += (char *)port;

    std::cout << "Trying port: " << path << " - ";
    int errorOpening = this->serialPort.openDevice(path.c_str(), 9600);

    if (errorOpening != 1) {
        std::cout << (int)errorOpening << ": ";
        switch (errorOpening) {
            case -1: std::cerr << "device not found\n";                  break;
            case -2: std::cerr << "error while opening the device\n";    break;
            case -3: std::cerr << "error while getting port parameters\n"; break;
            case -4: std::cerr << "Speed (Bauds) not recognized\n";      break;
            case -5: std::cerr << "error while writing port parameters\n"; break;
            case -6: std::cerr << "error while writing timeout parameters\n"; break;
            case -7: std::cerr << "Databits not recognized\n";           break;
            case -8: std::cerr << "Stopbits not recognized\n";           break;
            case -9: std::cerr << "Parity not recognized\n";             break;
            default: std::cout << ": Error opening: " << path << std::endl; break;
        }
        return -1;
    }

    printf("Successful connection to %s\n", path.c_str());
    this->serialPort.clearDTR();
    this->serialPort.clearRTS();

    baud_delay  = std::chrono::nanoseconds((1000000000 / 9600) / checksperbaud);

    t = std::thread(&serialuartTL16C2550::threadCycleUpdate, this);
    return 0;
}

// ===========================================================================
// Low-level port helpers
// ===========================================================================

int serialuartTL16C2550::write(unsigned char *val){
    return this->serialPort.writeBytes(val, 1);
}

int serialuartTL16C2550::read(unsigned char *readVal){
    return this->serialPort.readBytes(readVal, 1, 1000, 1000);
}

int serialuartTL16C2550::dataAvailable(){
    return this->serialPort.available();
}

int serialuartTL16C2550::baudCalculator(uint16_t divisor){
    if (divisor > 0)
        return (OSC / divisor) / 16;
    return -1;
}

// ===========================================================================
// Diagnostic helpers
// ===========================================================================

std::string serialuartTL16C2550::descParity(int parity){
    switch (parity) {
        case 0: return "No Parity";
        case 1: return "Even Parity";
        case 2: return "Odd Parity";
        case 3: return "Mark Parity";
        case 4: return "Space Parity";
        default: return "Error";
    }
}

std::string serialuartTL16C2550::descStopbits(int stopbits){
    switch (stopbits) {
        case 0: return "1 bit";
        case 1: return "1.5 bits";
        case 2: return "2 bits";
        default: return "Error";
    }
}
