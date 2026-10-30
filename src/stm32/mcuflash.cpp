#include "mcuflash.h"

// Option bytes are outside the supported parameter transaction. Never change
// protection/watchdog mode as a side effect of persistence or recovery.
bool McuFlash::eraseOptionBytes(bool) { return false; }
bool McuFlash::setOptionByte(OptionByte, uint16_t) { return false; }

bool McuFlash::programHalfWord(uint16_t value, uint32_t address)
{
    return parameterOperation(address,value,false);
}
bool McuFlash::erasePage(uint32_t address)
{
    return parameterOperation(address,0xffff,true);
}
