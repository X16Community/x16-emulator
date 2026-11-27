/*!
\file     uart_backend.cpp
\brief    Backend factory for serialuartTL16C2550. A serial UART for Commander X16 Emulator
\author   Jason Hill
\version  0.1
\date     August 30th of 2026, by Jason Hill
\modified September 5th of 2026 – Route "-uart ximodem" to the in-process libximodem backend

This Serial library is used for communication of a physical serial device on the X16 emulator. Simulating
some aspects of the TL16C2550 for use on personal computers.

Description: Chooses the IUartBackend for a given -uart path: an in-process libximodem virtual modem
for "ximodem" / "ximodem:<opts>", otherwise a physical host serial port (SerialLibBackend).

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE X CONSORTIUM BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

This is a licence-free software, it can be used by anyone who try to build a better world.
*/
#include "uart_backend.hpp"
#include "serialib_backend.hpp"
#include "ximodem_backend.hpp"
#include <cstring>
#include <cstdio>

IUartBackend *makeUartBackend(const char *path)
{
    if (path && (strcmp(path, "ximodem") == 0 || strncmp(path, "ximodem:", 8) == 0)) {
#ifdef HAVE_XIMODEM
        fprintf(stderr, "UART: using in-process libximodem virtual modem\n");
        return new XimodemBackend(path);
#else
        fprintf(stderr, "UART: -uart %s requested but this build has no libximodem "
                        "(configure with -DXIMODEM_DIR=...)\n", path);
        return nullptr;
#endif
    }
    return new SerialLibBackend();
}
