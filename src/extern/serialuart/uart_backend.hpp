/*!
\file     uart_backend.hpp
\brief    Backend interface for serialuartTL16C2550. A serial UART for Commander X16 Emulator
\author   Jason Hill
\version  0.1
\date     September 2nd of 2026, by Jason Hill
\modified September 5th of 2026 – Split the byte pipe out of serialuartTL16C2550 behind IUartBackend

This Serial library is used for communication of a physical serial device on the X16 emulator. Simulating
some aspects of the TL16C2550 for use on personal computers.

Description: The TL16C2550 model needs a byte pipe plus modem-control lines. Historically that was
always a physical host serial port (serialib). This interface lets the same model drive either a real
port (SerialLibBackend) or an in-process virtual modem such as libximodem (XimodemBackend), chosen by
the -uart path. The method set and semantics mirror the serialib subset the model already used, so
SerialLibBackend is a thin pass-through.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE X CONSORTIUM BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

This is a licence-free software, it can be used by anyone who try to build a better world.
*/
#pragma once

#include "serialib/serialib.h"   // SerialDataBits / SerialParity / SerialStopBits
#include <cstddef>

class IUartBackend {
public:
    virtual ~IUartBackend() {}

    // Returns 1 on success (serialib convention), negative on error.
    virtual int  openDevice(const char *path, unsigned int baud,
                            SerialDataBits  dataBits = SERIAL_DATABITS_8,
                            SerialParity    parity   = SERIAL_PARITY_NONE,
                            SerialStopBits  stopBits = SERIAL_STOPBITS_1) = 0;
    virtual void closeDevice() = 0;
    virtual bool isDeviceOpen() = 0;

    virtual int  writeBytes(const void *buffer, unsigned int nBytes) = 0;
    virtual int  readBytes (void *buffer, unsigned int maxBytes,
                            unsigned int timeout_ms = 0,
                            unsigned int sleep_us = 100) = 0;
    virtual int  available() = 0;
    virtual char flushReceiver() { return 0; }

    // Modem control lines (DTE point of view).
    virtual bool DTR(bool status) = 0;
    virtual bool RTS(bool status) = 0;
    virtual bool setDTR()   { return DTR(true);  }
    virtual bool clearDTR() { return DTR(false); }
    virtual bool setRTS()   { return RTS(true);  }
    virtual bool clearRTS() { return RTS(false); }

    virtual bool isCTS() = 0;   // modem -> DTE
    virtual bool isDSR() = 0;   // modem -> DTE
    virtual bool isDCD() { return false; }
    virtual bool isRI()  { return false; }
    virtual bool isRTS() = 0;   // last value we drove
    virtual bool isDTR() = 0;   // last value we drove

    // A virtual modem models a real async serial link: it wants the *actual*
    // programmed bit rate (0 = "the CPU has not set the card's divisor yet"),
    // not a placeholder, so it can mute traffic on a baud mismatch. A physical
    // port always needs a real speed to open.
    virtual bool isVirtualModem() const { return false; }
};

// Picks a backend from the -uart argument:
//   "ximodem" or "ximodem:<datadir>"  -> XimodemBackend (if built with HAVE_XIMODEM)
//   anything else                     -> SerialLibBackend (host serial port path)
IUartBackend *makeUartBackend(const char *path);
