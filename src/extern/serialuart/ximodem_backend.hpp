/*!
\file     ximodem_backend.hpp
\brief    IUartBackend backed by the in-process libximodem virtual modem. A serial UART for Commander X16 Emulator
\author   Jason Hill
\version  0.1
\date     September 2nd of 2026, by Jason Hill
\modified September 5th of 2026 – Pump-thread lifecycle, DTR/RTS bridging, XIMODEM_VERBOSE gate

This Serial library is used for communication of a physical serial device on the X16 emulator. Simulating
some aspects of the TL16C2550 for use on personal computers.

Description: Runs the Zimodem firmware loop on a dedicated pump thread; the TL16C2550 model's own
background thread exchanges bytes and modem signals with it through the thread-safe libximodem C API.
This gives the emulator a virtual modem with no physical serial port or external process.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE X CONSORTIUM BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

This is a licence-free software, it can be used by anyone who try to build a better world.
*/
#pragma once

#include "uart_backend.hpp"

#ifdef HAVE_XIMODEM
#include <ximodem.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>

class XimodemBackend : public IUartBackend {
    ximodem_t          *m = nullptr;
    std::thread          pump;
    std::atomic<bool>    run{false};
    std::string          dataDir;
    bool                 lastDTR = false;
    bool                 lastRTS = false;

    void pumpLoop() {
        while (run.load()) {
            ximodem_poll(m);
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }
public:
    explicit XimodemBackend(const char *arg) {
        // arg is "ximodem" or "ximodem:<datadir>"
        const char *colon = arg ? strchr(arg, ':') : nullptr;
        dataDir = colon ? std::string(colon + 1) : std::string("./ximodem-data");
    }
    ~XimodemBackend() override {
        closeDevice();
        if (m) { ximodem_destroy(m); m = nullptr; }
    }

    // The TL16C2550 model calls closeDevice()+openDevice() on every baud/format
    // reprogram. Tearing down the whole firmware each time would re-run NTP etc,
    // so we keep the libximodem instance alive across reconfigure: closeDevice()
    // only parks the pump thread, the destructor does the real teardown.
    int openDevice(const char *, unsigned int baud,
                   SerialDataBits, SerialParity, SerialStopBits) override {
        if (!m) {
            ximodem_config_t cfg;
            cfg.data_dir = dataDir.c_str();
            cfg.verbose  = getenv("XIMODEM_VERBOSE") ? 1 : 0;
            m = ximodem_create(&cfg);
            if (!m) return -2;
        }
        // Pass the real rate straight through -- 0 included. libximodem treats
        // 0 as "card not configured yet" and keeps the link muted until the CPU
        // programs the divisor to match the modem.
        ximodem_set_baud(m, baud);
        if (!run.load()) {
            run.store(true);
            pump = std::thread(&XimodemBackend::pumpLoop, this);
        }
        return 1;
    }
    void closeDevice() override {
        run.store(false);
        if (pump.joinable()) pump.join();
    }
    bool isDeviceOpen() override { return m != nullptr; }

    int writeBytes(const void *buf, unsigned int n) override {
        return m ? ximodem_write(m, (const uint8_t *)buf, (int)n) : -1;
    }
    int readBytes(void *buf, unsigned int max,
                  unsigned int timeout_ms, unsigned int sleep_us) override {
        if (!m) return -1;
        uint8_t *out = (uint8_t *)buf;
        unsigned int got = ximodem_read(m, out, (int)max);
        if (got > 0 || timeout_ms == 0) return (int)got;
        unsigned int waited = 0;
        while (got < max && waited < timeout_ms * 1000) {
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_us ? sleep_us : 100));
            waited += (sleep_us ? sleep_us : 100);
            unsigned int r = ximodem_read(m, out + got, (int)(max - got));
            got += r;
            if (r > 0) break;
        }
        return (int)got;
    }
    int available() override { return m ? ximodem_read_available(m) : 0; }

    bool DTR(bool s) override {
        lastDTR = s;
        if (m) ximodem_set_signal(m, XIMODEM_SIG_DTR, s ? 1 : 0);
        return s;
    }
    bool RTS(bool s) override {
        lastRTS = s;
        if (m) ximodem_set_signal(m, XIMODEM_SIG_RTS, s ? 1 : 0);
        return s;
    }
    bool isCTS() override { return m && ximodem_get_signal(m, XIMODEM_SIG_CTS); }
    bool isDSR() override { return m && ximodem_get_signal(m, XIMODEM_SIG_DSR); }
    bool isDCD() override { return m && ximodem_get_signal(m, XIMODEM_SIG_DCD); }
    bool isRI()  override { return m && ximodem_get_signal(m, XIMODEM_SIG_RI);  }
    bool isRTS() override { return lastRTS; }
    bool isDTR() override { return lastDTR; }
    bool isVirtualModem() const override { return true; }
};
#endif // HAVE_XIMODEM
