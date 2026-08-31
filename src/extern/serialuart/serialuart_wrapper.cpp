/*!
\file     serialuart_wrapper.cpp
\brief    Sourc file to wrap the class serialuartTL16C2550 for use in C.
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


#include "serialuart_wrapper.h"
#include "serialuartTL16C2550.hpp"

serialuartTL16C2550Handle uartCreate() {
    return static_cast<serialuartTL16C2550Handle>(new serialuartTL16C2550());
}

bool uart_irqPending( serialuartTL16C2550Handle uart){
	serialuartTL16C2550* obj = static_cast<serialuartTL16C2550*>(uart);
	
    if (obj != NULL) { 
        return obj->irqPending();
    }
    
	return false;
}

int uart_init( serialuartTL16C2550Handle uart, char *value ) {

    serialuartTL16C2550* obj = static_cast<serialuartTL16C2550*>(uart);
    if (obj != NULL) { 
        return obj->init(value);
    }
    
    return -1;
}

int uart_addrwrite( serialuartTL16C2550Handle uart, unsigned char *value, int address ) {

    serialuartTL16C2550* obj = static_cast<serialuartTL16C2550*>(uart);
    if (obj !=NULL) {
        return obj->addrwrite(value, address);
    }
    
    return -1;
}

int uart_addrread( serialuartTL16C2550Handle uart, unsigned char *value, int address ) {

    serialuartTL16C2550* obj = static_cast<serialuartTL16C2550*>(uart);
    if (obj!=NULL) {
        return obj->addrread(value, address);
    }
    return -1;
}

void uart_destroy(serialuartTL16C2550Handle uart) {

    serialuartTL16C2550* obj = static_cast<serialuartTL16C2550*>(uart);
    delete obj;
	obj = NULL;
	
}
