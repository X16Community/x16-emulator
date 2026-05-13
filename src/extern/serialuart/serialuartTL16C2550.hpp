/*!
\file     serialuartTL16C2550.hpp
\brief    Header file of the class serialuartTL16C2550. A serial UART for Commander X16 Emulator
\author   Jason Hill
\version  0.3
\date     September 18th of 2025, by Jason Hill
\modified March 2026 – Added 16-byte RX/TX FIFOs, threading, and full TL16C2550 interrupt system

This Serial library is used for communication of a physical serial device on the X16 emulator. Simulating
some aspects of the TL16C2550 for use on personal computers.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE X CONSORTIUM BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

This is a licence-free software, it can be used by anyone who try to build a better world.
*/

#pragma once

#include <string>
#include <iostream>
#include <cstdint>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include "serialib/serialib.h"

// ---------------------------------------------------------------------------
// Hardware FIFO capacity for the TL16C2550
// ---------------------------------------------------------------------------
static constexpr int UART_HW_FIFO_SIZE = 16;

// ---------------------------------------------------------------------------
// RX FIFO entry: each byte in the FIFO carries its own per-character error
// flags exactly as the TL16C2550 stores them.  The flags occupy LSR bits
// 1-4 (OE/PE/FE/BI) and are reported in LSR only when that character is at
// the head of the FIFO.
// ---------------------------------------------------------------------------
struct UartRxEntry {
    unsigned char data;
    unsigned char err;   // LSR error bits for this character: OE|PE|FE|BI
};

// ---------------------------------------------------------------------------
// IIR interrupt-type codes (bits 3:1).  Bit 0 = 0 means interrupt pending.
// ---------------------------------------------------------------------------
static constexpr unsigned char IIR_NO_INT        = 0x01; // no interrupt pending
static constexpr unsigned char IIR_MODEM_STATUS  = 0x00; // priority 5 (lowest)
static constexpr unsigned char IIR_THR_EMPTY     = 0x02; // priority 4
static constexpr unsigned char IIR_RX_DATA       = 0x04; // priority 2
static constexpr unsigned char IIR_RX_LINE_STAT  = 0x06; // priority 1 (highest)
static constexpr unsigned char IIR_CHAR_TIMEOUT  = 0x0C; // priority 3 (FIFO mode only)
static constexpr unsigned char IIR_FIFO_BITS     = 0xC0; // bits 7:6 when FIFOs enabled

// ---------------------------------------------------------------------------
// LSR bit masks
// ---------------------------------------------------------------------------
static constexpr unsigned char LSR_DR   = 0x01; // data ready
static constexpr unsigned char LSR_OE   = 0x02; // overrun error
static constexpr unsigned char LSR_PE   = 0x04; // parity error
static constexpr unsigned char LSR_FE   = 0x08; // framing error
static constexpr unsigned char LSR_BI   = 0x10; // break interrupt
static constexpr unsigned char LSR_THRE = 0x20; // transmitter holding register empty
static constexpr unsigned char LSR_TEMT = 0x40; // transmitter empty (shift reg + holding)
static constexpr unsigned char LSR_RXFE = 0x80; // error in RX FIFO (FIFO mode only)

// ---------------------------------------------------------------------------
// IER bit masks
// ---------------------------------------------------------------------------
static constexpr unsigned char IER_ERBFI = 0x01; // enable RX data available interrupt
static constexpr unsigned char IER_ETBEI = 0x02; // enable TX holding register empty interrupt
static constexpr unsigned char IER_ELSI  = 0x04; // enable receiver line status interrupt
static constexpr unsigned char IER_EDSSI = 0x08; // enable modem status interrupt

// ---------------------------------------------------------------------------
// FCR bit masks
// ---------------------------------------------------------------------------
static constexpr unsigned char FCR_FIFO_EN  = 0x01;
static constexpr unsigned char FCR_RX_RESET = 0x02;
static constexpr unsigned char FCR_TX_RESET = 0x04;
static constexpr unsigned char FCR_RX_TRIG  = 0xC0; // bits 7:6 select trigger level

// ---------------------------------------------------------------------------
// MCR bit masks
// ---------------------------------------------------------------------------
static constexpr unsigned char MCR_DTR      = 0x01;
static constexpr unsigned char MCR_RTS      = 0x02;
static constexpr unsigned char MCR_OUT1     = 0x04;
static constexpr unsigned char MCR_OUT2     = 0x08; // gates IRQ output to system
static constexpr unsigned char MCR_LOOPBACK = 0x10;
static constexpr unsigned char MCR_AFE      = 0x20; // Auto Flow-control Enable

// Number of character-times of RX FIFO inactivity before timeout interrupt
static constexpr int RX_TIMEOUT_CHARS = 4;

class serialuartTL16C2550 {
public:
     serialuartTL16C2550();
    ~serialuartTL16C2550();

    int  init(char*);
    int  addrwrite(unsigned char *value, int address);
    int  addrread (unsigned char *value, int address);

    // Returns true when the UART is asserting its IRQ output line.
    // MCR bit 3 (OUT2) must be set for IRQ to be driven.
    // The X16 emulator will poll this to know whether to assert the CPU's IRQ pin.
    bool irqPending() const;

private:
    // ---- TL16C2550 Addressable Registers ----------------------------------------
    unsigned char IIR;          // Interrupt Identification Register (read-only)
    unsigned char IER;          // Interrupt Enable Register
    unsigned char FCR;          // FIFO Control Register (write-only; shadow kept here)
    unsigned char MCR;          // Modem Control Register
    unsigned char LCR;          // Line Control Register
    unsigned char LSR;          // Line Status Register (partially computed on read)
    unsigned char MSR;          // Modem Status Register
    unsigned char scratchReg;   // Scratch Register (no side effects)
    unsigned char DLSB;         // Divisor Latch – Least Significant Byte (DLAB=1)
    unsigned char DMSB;         // Divisor Latch – Most Significant Byte  (DLAB=1)
    uint16_t      requestedDivisor;

    // Loopback data register: THR writes -> here; RBR reads <- here in loopback mode
    unsigned char loopvalue;

    // ---- 16-Byte RX FIFO --------------------------------------------------------
    // Stores received characters together with their per-character error flags.
    // Protected by rxMutex because threadCycleUpdate() pushes from a background thread
    // while addrread() pops from the emulator thread.
    std::deque<UartRxEntry> rxFifo;
    std::mutex              rxMutex;

    // ---- 16-Byte TX FIFO --------------------------------------------------------
    // The emulator thread pushes via addrwrite(); threadCycleUpdate() drains to the host
    // serial port.
    std::deque<unsigned char> txFifo;
    std::mutex                txMutex;

    // ---- Interrupt Pending Flags -------------------------------------------------
    // Each flag is set by the event that causes the condition and cleared when the
    // condition is resolved (see TL16C2550 datasheet Table 3 for clearing actions).
    bool irqRxLineStatus;  // OE/PE/FE/BI in head of FIFO; cleared by LSR read
    bool irqRxDataReady;   // FIFO at or above trigger level; cleared when FIFO falls below
    bool irqCharTimeout;   // FIFO non-empty, idle > 4 char-times; cleared by RBR read
    bool irqThrEmpty;      // TX FIFO empty; cleared by THR write or IIR read
    bool irqModemStatus;   // CTS/DSR/RI/DCD changed; cleared by MSR read

    // ---- Character-Timeout Tracking ---------------------------------------------
    // threadCycleUpdate increments rxTimeoutTicks every tick when RX FIFO is non-empty
    // and no new byte arrived.  When it reaches rxTimeoutThreshold the timeout
    // interrupt fires.  Any new RX byte resets the counter.
    int rxTimeoutTicks;
    int rxTimeoutThreshold; // = RX_TIMEOUT_CHARS * ticks_per_char, updated on baud change

    // Ticks per character at the current baud/format (used to compute threshold)
    int ticksPerChar;

    // ---- Modem Signal Tracking --------------------------------------------------
    // Previous states of CTS/DSR, so we can detect transitions for MSR delta bits.
    bool prevCTS;
    bool prevDSR;

    // ---- Physical Serial Port ---------------------------------------------------
    serialib serialPort;

    // Crystal Oscillator Speed on Texelec Serial/Wifi Card
    static const int OSC = 14745600;
    std::string path;

    // ---- Private Helper Methods -------------------------------------------------
    void          reconfigureSerial();
    int           write(unsigned char*);
    int           read (unsigned char*);
    int           dataAvailable();
    int           baudCalculator(uint16_t);

    // Rebuild IIR from the five interrupt-pending flags and IER.
    // Call whenever any flag or IER changes.
    void          updateIIR();

    // Push a received byte (plus error flags) into rxFifo.
    // Handles overrun: if FIFO is full, sets OE and fires line-status interrupt.
    void          rxFifoPush(unsigned char data, unsigned char errFlags = 0);

    // Pop the head entry from rxFifo.  Returns false if empty.
    bool          rxFifoPop(UartRxEntry &out);

    // Reset (flush) the RX FIFO and clear related interrupt state (FCR bit 1).
    void          rxFifoReset();

    // Reset (flush) the TX FIFO and arm THRE interrupt (FCR bit 2).
    void          txFifoReset();

    // Recompute the LSR byte from live FIFO state.
    // Sticky error bits (OE/PE/FE/BI) are held in the LSR member until LSR is read.
    unsigned char computeLSR() const;

    // Update MSR delta bits when CTS/DSR transitions are detected.
    void          updateMSR(bool newCTS, bool newDSR);

    // ---- Background Thread ------------------------------------------------------
    std::thread t;
    bool        quit;
    void        threadCycleUpdate();  // thread body

    // Cached signal states (set by threadCycleUpdate, read by addrwrite/addrread)
    bool hasCTS;
    bool hasDSR;
    int  availCount;        // host OS bytes available in its own serial buffer

    //Baud-rate timing
	//checks-per-baud: How often the thread will scan the UART, at least twice as fast as baud rate, for no delays
	//see initilization list for class.
    int           checksperbaud;
    unsigned char fifo_level;   // RX trigger level decoded from FCR (1/4/8/14)
    std::chrono::steady_clock::duration   baud_delay;

    // Diagnostic helpers
    std::string descParity(int);
    std::string descStopbits(int);
};
