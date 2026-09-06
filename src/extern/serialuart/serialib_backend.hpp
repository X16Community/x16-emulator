/*!
\file     serialib_backend.hpp
\brief    IUartBackend backed by a physical host serial port (serialib). A serial UART for Commander X16 Emulator
\author   Jason Hill
\version  0.1
\date     August 30th of 2026, by Jason Hill
\modified September 5th of 2026 – Extracted from serialuartTL16C2550 as an IUartBackend implementation

This Serial library is used for communication of a physical serial device on the X16 emulator. Simulating
some aspects of the TL16C2550 for use on personal computers.

Description: A thin pass-through IUartBackend that forwards every call to a serialib port object, so the
TL16C2550 model talks to a real OS serial device exactly as it did before the backend split.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE X CONSORTIUM BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

This is a licence-free software, it can be used by anyone who try to build a better world.
*/
#pragma once

#include "uart_backend.hpp"

class SerialLibBackend : public IUartBackend {
    serialib port;
public:
    int openDevice(const char *path, unsigned int baud,
                   SerialDataBits db, SerialParity par, SerialStopBits sb) override {
        return port.openDevice(path, baud, db, par, sb);
    }
    void closeDevice() override { port.closeDevice(); }
    bool isDeviceOpen() override { return port.isDeviceOpen(); }

    int writeBytes(const void *buf, unsigned int n) override { return port.writeBytes(buf, n); }
    int readBytes(void *buf, unsigned int max, unsigned int t_ms, unsigned int s_us) override {
        return port.readBytes(buf, max, t_ms, s_us);
    }
    int  available() override { return port.available(); }
    char flushReceiver() override { return port.flushReceiver(); }

    bool DTR(bool s) override { return port.DTR(s); }
    bool RTS(bool s) override { return port.RTS(s); }
    bool isCTS() override { return port.isCTS(); }
    bool isDSR() override { return port.isDSR(); }
    bool isDCD() override { return port.isDCD(); }
    bool isRI()  override { return port.isRI();  }
    bool isRTS() override { return port.isRTS(); }
    bool isDTR() override { return port.isDTR(); }
};
