#include "params.h"
#include "paramstore.h"
#include "flightmanager.h"

#include <string.h>
#include "stm32/mcuflash.h"

Params Params::sys;

void Params::load()
{
    /*
     * Read the params object out of flash.
     * Uninitialized bytes will be set to 0xff.
     */

    memcpy(&storedValues, reinterpret_cast<const void*>(ParamsPageAddr), sizeof(storedValues));
    // Old initialized labels may fill their field without a NUL. Leave
    // erased configs untouched so ButtonManager still supplies its defaults.
    for (unsigned i = 0; i < arraysize(storedValues.buttonConfigs); ++i) {
        ButtonFunction::Config &cfg = storedValues.buttonConfigs[i];
        if (isInitialized(&cfg, sizeof cfg)) cfg.descriptor[sizeof cfg.descriptor - 1] = '\0';
    }
    // Preserve invalid stored mappings for diagnostics. Inputs locks out
    // control reports until an explicitly valid configuration is applied.

}

bool Params::save()
{
    /*
     * Write our stored values to storage.
     * In order to write even one new value, we must erase the entire page.
     *
     * Should only be called on a periodic basis by fiftyHzWork()
     * or during shutdown.
     */

    StoredValues snapshot;
    uint32_t savedGeneration;
    {
        InterruptGuard guard;
        if (!pending.dirty) return true;
        snapshot = storedValues;
        savedGeneration = pending.generation;
    }
    if (!ParamStore::write<McuFlash>(ParamsPageAddr, &snapshot, sizeof snapshot)) return false;
    {
        InterruptGuard guard;
        // A new ISR edit during the transaction must remain pending.
        pending.complete(savedGeneration, true);
    }
    return true;
}

void Params::periodicWork()
{
    /*
     * Called periodically in task context.
     * Sync our params if needed every SYNC_MILLIS.
     */

    if (FlightManager::instance.armed()) {
        // We never want to sync when the vehicle is armed,
        // since the process of writing to internal flash can stall
        // the CPU long enough to drop bytes over the UART.
        return;
    }

    const SysTime::Ticks now = SysTime::now();
    if (now > syncDeadline) {
        if (!save()) {
            DBG(("error syncing params\n"));
        }
        syncDeadline = now + SysTime::msTicks(SYNC_MILLIS);
    }
}

bool Params::isInitialized(const void *p, unsigned len)
{
    /*
     * For each param element, we consider it to be initialized
     * if any of its bytes have been programmed to a value other
     * than the uninitialized value of 0xff;
     */

    // must be half-word aligned
    ASSERT((len & 0x1) == 0);
    if (len & 0x1) {
        return false;
    }

    const uint16_t *bp = static_cast<const uint16_t *>(p);
    for ( ; len > 0; bp++, len -= sizeof(uint16_t)) {
        if (*bp != 0xffff) {
            return true;
        }
    }

    // all bytes were uninitialized
    return false;
}
