#include "params.h"
#include "paramstore.h"
#include "paramjournal.h"
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

    static_assert(sizeof storedValues == ParamJournal::PayloadBytes, "parameter wire ABI");
    if (!ParamJournal::load<McuFlash>(&storedValues)) {
        // Fail closed: erased controls are invalid; do not overwrite damaged pages.
        memset(&storedValues, 0xff, sizeof storedValues);
    }
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
     * Alternate journal pages; retain the previous committed snapshot.
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
    if (!ParamJournal::save<McuFlash>(&snapshot)) return false;
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

// Fixed offset304 follows the76-vector table. Updaters reject unknown layouts.
extern const uint8_t artoo_storage_layout[16] __attribute__((section(".artoo_layout"),used)) =
    {'O','S','5','J','N','L','1','!',0x00,0xf0,0x03,0x08,172,0,1,0};
