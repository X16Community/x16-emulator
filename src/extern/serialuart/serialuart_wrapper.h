/*!
\file     serialuart_wrapper.h
\brief    Header file to wrap the class serialuartTL16C2550 for use in C.
\author   Jason Hill
\version  0.1
\date     September 18th of 2025, by Jason Hill
\modified none

This Serial library is used for communication of a physical serial device on the X16 emulator. Simulating
some aspects of the TL16C2550 for use on personal computers.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE X CONSORTIUM BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

This is a licence-free software, it can be used by anyone who try to build a better world.
*/

#ifdef __cplusplus
extern "C" {
#endif

// Pointer for the C++ Class Instance
typedef void* serialuartTL16C2550Handle;

// Constructor-like function
serialuartTL16C2550Handle uartCreate();

// Method-calling function
int uart_init( serialuartTL16C2550Handle, char*);
int uart_addrwrite( serialuartTL16C2550Handle , unsigned char*, int);
int uart_addrread( serialuartTL16C2550Handle , unsigned char*, int);
bool uart_irqPending( serialuartTL16C2550Handle uart);


// Destructor-like function
void uart_destroy( serialuartTL16C2550Handle );

#ifdef __cplusplus
}
#endif
