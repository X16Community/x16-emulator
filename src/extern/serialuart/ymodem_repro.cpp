/*!
\file     ymodem_repro.cpp
\brief    Standalone YMODEM reproduction harness for serialuartTL16C2550. A serial UART for Commander X16 Emulator
\author   Jason Hill
\version  0.1
\date     September 3rd of 2026, by Jason Hill
\modified September 5th of 2026 – Honour the model's RTS line; socket-buffer pacing stands in for 115200

This Serial library is used for communication of a physical serial device on the X16 emulator. Simulating
some aspects of the TL16C2550 for use on personal computers.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE X CONSORTIUM BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

This is a licence-free software, it can be used by anyone who try to build a better world.
*/
// ---------------------------------------------------------------------------
// Reproduce "YMODEM download hangs at zero" against the emulated TL16C2550,
// isolated from the x16 emulator, the ROM, XCOMM, and libximodem.
//
//   [ YMODEM sender thread ] --socketpair-- [ PairBackend ] <- serialuartTL16C2550
//                                                              ^  driven here by a
//                                                                 faithful port of
//                                                                 XCOMM's ymodem_step
//                                                                 register access
//                                                                 pattern (LSR poll
//                                                                 / RBR read / THR
//                                                                 write, FCR 0x87,
//                                                                 MCR 0x23, 115200).
//
// The sender honours the model's RTS line the way a real Zimodem's CTS-gated TX
// would, and keeps CTS asserted toward the model (X16->modem is only tiny ACKs
// during a download). Small socket buffers + optional pacing stand in for the
// 115200 wire.
//
// Build (from this directory):
//   g++ -std=c++17 -O1 -g -pthread ymodem_repro.cpp serialuartTL16C2550.cpp \
//       -o /tmp/ymodem_repro
// Run:
//   /tmp/ymodem_repro [fileKB] [--nopace] [--noflow]
// ---------------------------------------------------------------------------
#include "serialuartTL16C2550.hpp"
#include "uart_backend.hpp"

#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

// We inject the backend, so makeUartBackend() is never called -- stub it so we
// don't have to link uart_backend.cpp (serialib + libximodem).
IUartBackend *makeUartBackend(const char *) { return nullptr; }

// ---- config -------------------------------------------------------------------
static bool  g_pace = true;    // sender paces to ~115200
static bool  g_flow = true;    // sender honours model RTS
static int   g_rxByteUs  = 0;  // receiver: delay per RBR read  (6502 ymodem_step cost)
static int   g_rxBlockUs = 0;  // receiver: delay per completed block (screen redraw)

// ---- register offsets -------------------------------------------------------
enum { R_RBR=0, R_THR=0, R_IER=1, R_DLM=1, R_IIR=2, R_FCR=2, R_LCR=3,
       R_MCR=4, R_LSR=5, R_MSR=6, R_SCR=7 };

// ---- socketpair-backed backend --------------------------------------------
struct PairBackend : IUartBackend {
    int               fd  = -1;      // model's end of the socketpair
    std::atomic<bool> rts{false};    // last value the model drove (DTE RTS)
    std::atomic<bool> dtr{false};
    std::atomic<bool> ctsIn{true};   // modem -> DTE (peer keeps it asserted)
    std::atomic<bool> dsrIn{true};

    explicit PairBackend(int f) : fd(f) {}

    int openDevice(const char*, unsigned int, SerialDataBits, SerialParity,
                   SerialStopBits) override { return 1; }
    void closeDevice() override {}
    bool isDeviceOpen() override { return fd >= 0; }

    int writeBytes(const void *b, unsigned int n) override {
        ssize_t w = ::send(fd, b, n, MSG_DONTWAIT | MSG_NOSIGNAL);
        return (w == (ssize_t)n) ? 1 : -1;      // mirror serialib's all-or-nothing
    }
    int readBytes(void *b, unsigned int max, unsigned int, unsigned int) override {
        ssize_t r = ::recv(fd, b, max, MSG_DONTWAIT);
        return r > 0 ? (int)r : 0;
    }
    int available() override {
        int n = 0;
        return (::ioctl(fd, FIONREAD, &n) == 0) ? n : 0;
    }
    bool DTR(bool s) override { dtr = s; return s; }
    bool RTS(bool s) override { rts = s; return s; }
    // ctsFollowsRts: model a hookup where the modem doesn't drive CTS
    // independently -- it tracks our own RTS output (coupled adapter / 3-wire
    // cable / DCE that leaves CTS undriven).
    std::atomic<bool> ctsFollowsRts{false};
    bool isCTS() override {
        return ctsFollowsRts.load() ? rts.load() : ctsIn.load();
    }
    bool isDSR() override { return dsrIn.load(); }
    bool isRTS() override { return rts.load(); }
    bool isDTR() override { return dtr.load(); }
    bool isVirtualModem() const override { return false; }
};

// ---- CRC-16/XMODEM --------------------------------------------------------
static uint16_t crc16(const uint8_t *p, int n) {
    uint16_t c = 0;
    for (int i = 0; i < n; i++) {
        c ^= (uint16_t)p[i] << 8;
        for (int b = 0; b < 8; b++)
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

// ===========================================================================
// The "modem" side: a lockstep YMODEM sender on peerFd.
// ===========================================================================
static std::atomic<bool> g_senderDone{false};
static std::atomic<bool> g_senderStuck{false};
static std::atomic<int>  g_blocksAcked{0};

static int peerGet(int fd, int timeout_ms) {
    auto end = std::chrono::steady_clock::now() +
               std::chrono::milliseconds(timeout_ms);
    uint8_t c;
    for (;;) {
        ssize_t r = ::recv(fd, &c, 1, MSG_DONTWAIT);
        if (r == 1) return c;
        if (std::chrono::steady_clock::now() >= end) return -1;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
}

static void peerPut(int fd, PairBackend *be, const uint8_t *p, int n) {
    int off = 0;
    while (off < n) {
        if (g_flow) while (!be->rts.load()) std::this_thread::sleep_for(
                                       std::chrono::microseconds(150));
        ssize_t w = ::send(fd, p + off, n - off, MSG_NOSIGNAL);   // blocking
        if (w > 0) {
            off += (int)w;
            if (g_pace)                       // ~115200 baud => ~87 us / byte
                std::this_thread::sleep_for(std::chrono::microseconds(w * 87));
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }
}

static bool peerWaitAck(int fd, const char *what) {
    for (int tries = 0; tries < 20; tries++) {
        int b = peerGet(fd, 4000);
        if (b == 0x06) return true;                       // ACK
        if (b == 0x15) { fprintf(stderr, "[sender] NAK on %s\n", what); return false; }
        if (b < 0)     { fprintf(stderr, "[sender] STUCK waiting ACK for %s\n", what);
                         g_senderStuck = true; return false; }
        // ignore stray bytes ('C', etc.)
    }
    return false;
}

static void senderThread(int fd, PairBackend *be, int fileKB) {
    const int dataLen = fileKB * 1024;
    std::vector<uint8_t> file(dataLen);
    for (int i = 0; i < dataLen; i++) file[i] = (uint8_t)(i * 7 + (i >> 8));

    // wait for initial 'C'
    for (;;) { int b = peerGet(fd, 8000);
               if (b == 'C') break;
               if (b < 0) { fprintf(stderr, "[sender] never saw initial 'C'\n");
                            g_senderStuck = true; return; } }

    auto sendBlock = [&](int seq, const uint8_t *data, int len) {
        uint8_t pkt[3 + 1024 + 2];
        pkt[0] = (len == 1024) ? 0x02 /*STX*/ : 0x01 /*SOH*/;
        pkt[1] = (uint8_t)seq;
        pkt[2] = (uint8_t)~seq;
        memset(pkt + 3, 0, len);
        if (data) memcpy(pkt + 3, data, len);
        uint16_t c = crc16(pkt + 3, len);
        pkt[3 + len]     = (uint8_t)(c >> 8);
        pkt[3 + len + 1] = (uint8_t)(c & 0xFF);
        peerPut(fd, be, pkt, 3 + len + 2);
    };

    // block 0: filename + size
    {
        uint8_t b0[128]; memset(b0, 0, sizeof b0);
        int k = snprintf((char *)b0, sizeof b0, "repro.bin");
        k++;                                    // NUL
        snprintf((char *)b0 + k, sizeof b0 - k, "%d", dataLen);
        sendBlock(0, b0, 128);
    }
    if (!peerWaitAck(fd, "block0")) return;
    // receiver now sends another 'C'
    for (;;) { int b = peerGet(fd, 4000);
               if (b == 'C') break;
               if (b < 0) { fprintf(stderr, "[sender] no 'C' after block0 ACK\n");
                            g_senderStuck = true; return; } }

    // data blocks
    int seq = 1, off = 0;
    while (off < dataLen) {
        int len = (dataLen - off >= 1024) ? 1024 : (dataLen - off);
        // pad short final block to 1024 with zeros (YMODEM convention)
        int blk = (len == 1024) ? 1024 : 1024;
        uint8_t chunk[1024]; memset(chunk, 0x1A, sizeof chunk);
        memcpy(chunk, file.data() + off, len);
        for (int retry = 0; retry < 10; retry++) {
            sendBlock(seq & 0xFF, chunk, blk);
            int b = peerGet(fd, 4000);
            if (b == 0x06) break;               // ACK
            if (b == 0x15) { fprintf(stderr, "[sender] NAK block %d, resending\n", seq);
                             continue; }
            fprintf(stderr, "[sender] STUCK waiting ACK for block %d "
                            "(off=%d)\n", seq, off);
            g_senderStuck = true; return;
        }
        g_blocksAcked++;
        off += len;
        seq++;
    }

    // EOT handshake
    uint8_t eot = 0x04;
    peerPut(fd, be, &eot, 1);
    if (peerGet(fd, 4000) != 0x15) fprintf(stderr, "[sender] expected NAK after EOT\n");
    peerPut(fd, be, &eot, 1);
    if (!peerWaitAck(fd, "EOT")) return;
    for (;;) { int b = peerGet(fd, 4000);
               if (b == 'C') break;
               if (b < 0) break; }
    // final empty block 0
    { uint8_t z[128]; memset(z, 0, sizeof z); sendBlock(0, z, 128); }
    peerWaitAck(fd, "final block0");

    g_senderDone = true;
    fprintf(stderr, "[sender] DONE, %d blocks acked\n", g_blocksAcked.load());
}

// ===========================================================================
// The CPU side: XCOMM's ymodem_step register access pattern.
// ===========================================================================
static serialuartTL16C2550 uart;

static unsigned char rd(int a) { unsigned char v = 0; uart.addrread(&v, a); return v; }
static void          wr(unsigned char v, int a) { uart.addrwrite(&v, a); }

static bool byteAvail() {                    // XCOMM serial_byte_available_safe()
    unsigned char l = rd(R_LSR);
    if (l == 0xFF) return false;
    return (l & 0x01) != 0;
}
static bool canWrite() { return (rd(R_MSR) & 0x10) != 0; }  // XCOMM cts_rts path

static void txByte(unsigned char c) {        // XCOMM serial_write()
    long guard = 50000;
    while (!canWrite() && --guard) { }
    wr(c, R_THR);
}

static void applySettings() {               // XCOMM serial_apply_settings @115200
    wr(0x83, R_LCR);        // DLAB=1, 8N1
    wr(0x08, R_THR);        // DLL: 14745600/16/115200 = 8
    wr(0x00, R_DLM);        // DLM
    wr(0x03, R_LCR);        // DLAB=0  -> reconfigureSerial()
    wr(0x87, R_FCR);        // FIFO enable + reset, 8-byte RX trigger
    wr(0x23, R_MCR);        // DTR | RTS | AFE
}

int main(int argc, char **argv) {
    int fileKB = 8;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--nopace")) g_pace = false;
        else if (!strcmp(argv[i], "--noflow")) g_flow = false;
        else fileKB = atoi(argv[i]);
    }
    if (fileKB <= 0) fileKB = 8;
    if (getenv("REPRO_RX_BYTE_US"))  g_rxByteUs  = atoi(getenv("REPRO_RX_BYTE_US"));
    if (getenv("REPRO_RX_BLOCK_US")) g_rxBlockUs = atoi(getenv("REPRO_RX_BLOCK_US"));
    fprintf(stderr, "== ymodem_repro: %d KB, pace=%d flow=%d rxByteUs=%d rxBlockUs=%d ==\n",
            fileKB, g_pace, g_flow, g_rxByteUs, g_rxBlockUs);

    int sp[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) { perror("socketpair"); return 2; }
    // small buffers so the 16-byte FIFO + AFE flow control actually matter
    int buf = 2048;
    for (int e = 0; e < 2; e++) {
        setsockopt(sp[e], SOL_SOCKET, SO_RCVBUF, &buf, sizeof buf);
        setsockopt(sp[e], SOL_SOCKET, SO_SNDBUF, &buf, sizeof buf);
    }

    auto *be = new PairBackend(sp[0]);
    if (getenv("REPRO_NO_DSR")) be->dsrIn = false;
    if (getenv("REPRO_NO_CTS")) be->ctsIn = false;
    if (getenv("REPRO_CTS_FOLLOWS_RTS")) be->ctsFollowsRts = true;
    uart.injectBackend(be);
    if (uart.init((char *)"repro") != 0) { fprintf(stderr, "uart.init failed\n"); return 2; }

    std::thread sender(senderThread, sp[1], be, fileKB);

    applySettings();

    // ---- receiver state machine (mirrors XCOMM ymodem_step) ----
    enum { WAIT_MARKER, BNO, BINV, DATA, CRCHI, CRCLO };
    int  st = WAIT_MARKER;
    int  blkSize = 0, pos = 0;
    uint8_t blk[1040];
    int  blkNo = 0, blkInv = 0, crcHi = 0;
    int  expected = 1;
    long long fileBytes = 0, fileSize = -1;
    bool haveFile = false, activeFile = false;
    long long totalGot = 0;
    int  blocksDone = 0, naks = 0;

    txByte('C');
    auto lastProgress = std::chrono::steady_clock::now();
    int  lastReportBlocks = -1;

    for (;;) {
        if (g_senderDone.load() && st == WAIT_MARKER && !activeFile) break;
        if (g_senderStuck.load()) {
            fprintf(stderr, "\n!! sender reported STUCK -- the ACK/handshake "
                            "from the model is not arriving.\n");
            break;
        }
        if (!byteAvail()) {
            auto idle = std::chrono::steady_clock::now() - lastProgress;
            if (idle > std::chrono::seconds(6)) {
                fprintf(stderr,
                    "\n!! RECEIVER STALLED: no progress for 6s\n"
                    "   state=%d expected=%d blocksDone=%d naks=%d fileBytes=%lld/%lld\n"
                    "   LSR=$%02x MSR=$%02x IIR=$%02x MCR=$%02x  backend RTS=%d CTS=%d\n",
                    st, expected, blocksDone, naks, fileBytes, fileSize,
                    rd(R_LSR), rd(R_MSR), rd(R_IIR), rd(R_MCR),
                    (int)be->rts.load(), (int)be->isCTS());
                break;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }
        unsigned char b = (unsigned char)rd(R_RBR);
        if (g_rxByteUs) std::this_thread::sleep_for(std::chrono::microseconds(g_rxByteUs));
        lastProgress = std::chrono::steady_clock::now();

        switch (st) {
        case WAIT_MARKER:
            if      (b == 0x04) {                 // EOT
                if (activeFile) { txByte(0x15); /* NAK -> expect 2nd EOT */
                                  st = DATA; blkSize = -2; /* sentinel */ }
                else            { txByte(0x06); txByte('C'); }
            }
            else if (b == 0x01) { blkSize = 128;  pos = 0; st = BNO; }
            else if (b == 0x02) { blkSize = 1024; pos = 0; st = BNO; }
            // else ignore
            break;
        case DATA:
            if (blkSize == -2) {                  // waiting for 2nd EOT
                if (b == 0x04) { txByte(0x06); txByte('C'); }
                st = WAIT_MARKER;
                break;
            }
            blk[pos++] = b;
            if (pos >= blkSize) st = CRCHI;
            break;
        case BNO:  blkNo  = b; st = BINV; break;
        case BINV: blkInv = b; st = DATA; break;
        case CRCHI: crcHi = b; st = CRCLO; break;
        case CRCLO: {
            uint16_t rx = (uint16_t)((crcHi << 8) | b);
            uint16_t cc = crc16(blk, blkSize);
            st = WAIT_MARKER;
            if (((blkNo ^ blkInv) & 0xFF) != 0xFF || rx != cc) {
                naks++; txByte(0x15); break;       // NAK
            }
            if (!activeFile && blkNo == 0) {        // block 0: header
                const char *name = (const char *)blk;
                long long sz = atoll((const char *)blk + strlen(name) + 1);
                fileSize = sz;
                haveFile = true; activeFile = true; expected = 1; fileBytes = 0;
                fprintf(stderr, "[recv] block0: name='%s' size=%lld\n", name, sz);
                txByte(0x06); txByte('C');
            } else if (activeFile) {
                if ((blkNo & 0xFF) == (expected & 0xFF)) {
                    long long take = fileSize - fileBytes;
                    if (take > blkSize) take = blkSize;
                    if (take < 0) take = 0;
                    fileBytes += take; totalGot += take; expected++;
                    blocksDone++;
                    txByte(0x06);
                    if (g_rxBlockUs)     // 6502 redrawing the progress bar
                        std::this_thread::sleep_for(std::chrono::microseconds(g_rxBlockUs));
                } else {
                    txByte(0x06);                  // dup -> ACK again
                }
            } else {
                txByte(0x15);
            }
            break;
        }
        }

        if (blocksDone != lastReportBlocks && (blocksDone % 1 == 0)) {
            fprintf(stderr, "\r[recv] blocks=%d bytes=%lld/%lld naks=%d   ",
                    blocksDone, fileBytes, fileSize, naks);
            lastReportBlocks = blocksDone;
        }
    }

    fprintf(stderr, "\n---- result ----\n");
    fprintf(stderr, "  receiver: blocksDone=%d fileBytes=%lld/%lld naks=%d\n",
            blocksDone, fileBytes, fileSize, naks);
    fprintf(stderr, "  sender:   done=%d stuck=%d blocksAcked=%d\n",
            (int)g_senderDone.load(), (int)g_senderStuck.load(), g_blocksAcked.load());

    bool ok = g_senderDone.load() && fileBytes == fileSize && naks == 0;
    fprintf(stderr, "  => %s\n", ok ? "PASS" : "FAIL / HANG");

    // let threads unwind
    g_senderStuck = true;
    if (sender.joinable()) sender.detach();
    ::close(sp[0]); ::close(sp[1]);
    _exit(ok ? 0 : 1);
}
