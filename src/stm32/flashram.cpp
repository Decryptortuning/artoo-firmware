#include "mcuflash.h"
#include "../diagnostics.h"
#include "../diagnosticrecord.h"
#include <stdint.h>

#define RAM_CODE __attribute__((section(".ramfunc"),noinline,noclone))
// The table is private to a synchronous flash interval. PRIMASK blocks ordinary
// IRQs; NMI/HardFault vectors also execute exclusively in SRAM.
static uint32_t flashVectors[128] __attribute__((aligned(512)));

extern "C" RAM_CODE __attribute__((noreturn)) void artoo_flash_terminal(uint32_t stage)
{
    volatile uint32_t *record = artoo_fault_record;
    record[0]=0;
    record[1]=0x00010040;
    for (unsigned i=2;i<15;++i) record[i]=0;
    record[12]=artoo_current_task;
    record[13]=2; record[14]=stage;
    uint32_t hash=2166136261u;
    for (unsigned i=1;i<15;++i)
        for (unsigned b=0;b<4;++b) { hash^=(record[i]>>(8*b))&255u; hash*=16777619u; }
    record[15]=hash;
    asm volatile("dmb" ::: "memory");
    record[0]=0x41525446;
    asm volatile(".global artoo_flash_contained\nartoo_flash_contained:" ::: "memory");
    // Containment, not recovery: no return into busy flash, no reset or retry.
    // If the experimental IWDG is running, deliberately prevent reset while
    // flash outcome is uncertain. This is the only non-progress feed path.
    for (;;) {
        *reinterpret_cast<volatile uint32_t*>(0x40003000u)=0xaaaau;
        asm volatile("nop");
    }
}
extern "C" RAM_CODE __attribute__((noreturn)) void artoo_flash_exception()
{
    artoo_flash_terminal(12);
}

extern "C" RAM_CODE bool artoo_flash_operation(uint32_t address,uint16_t value,bool erase)
{
    volatile uint32_t *sr=reinterpret_cast<volatile uint32_t*>(0x4002200cu);
    volatile uint32_t *cr=reinterpret_cast<volatile uint32_t*>(0x40022010u);
    volatile uint32_t *ar=reinterpret_cast<volatile uint32_t*>(0x40022014u);
    volatile uint32_t *vtor=reinterpret_cast<volatile uint32_t*>(0xe000ed08u);
    uint32_t mask;
    asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(mask) :: "memory");
    uint32_t oldVtor=*vtor;
    *vtor=reinterpret_cast<uint32_t>(flashVectors);
    asm volatile("dsb\n\tisb" ::: "memory");
    // Iteration bounds independent of stopped SysTick. Generous at72MHz;
    // electrical latency/LSI/UART loss require later hardware measurement.
    unsigned budget=1u<<24;
    while (*sr & 1u) if (!--budget) artoo_flash_terminal(11);
    bool ok=(*sr & 0x14u)==0;
    if (ok) {
        if (erase) { *cr |= 2u; *ar=address; *cr |= 64u; }
        else { *cr |= 1u; *reinterpret_cast<volatile uint16_t*>(address)=value; }
        budget=1u<<24;
        while (*sr & 1u) if (!--budget) artoo_flash_terminal(11);
        ok=(*sr & 0x14u)==0;
        *cr &= ~(erase ? 2u:1u);
    }
    *vtor=oldVtor;
    asm volatile("dsb\n\tisb\n\tmsr primask, %0" :: "r"(mask) : "memory");
    return ok;
}

bool McuFlash::parameterOperation(uint32_t address,uint16_t value,bool erase)
{
    if (address<END_ADDR-2*PAGE_SIZE || address>=END_ADDR || (address&1u) ||
        (erase && address%PAGE_SIZE) || !(RCC.CR&1u)) return false;
    // All call sites are foreground. No SRAM vector points to flash.
    for (unsigned i=0;i<128;++i) flashVectors[i]=reinterpret_cast<uint32_t>(&artoo_flash_exception);
    return artoo_flash_operation(address,value,erase);
}
