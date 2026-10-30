#ifndef ARTOO_DIAGNOSTICS_H
#define ARTOO_DIAGNOSTICS_H
#include <stdint.h>
#ifndef ARTOO_BENCH_NO_IDLE
#define ARTOO_BENCH_NO_IDLE 0
#endif
namespace Diagnostics {
static const unsigned ReplyBytes = 204;
static const unsigned MetricWords = 32;
void init();
void snapshot(uint8_t *out, uint16_t token, const uint32_t *uartStats);
void taskBegin(unsigned id);
void taskEnd(unsigned id);
void loop();
void heartbeatPosted();
void heartbeatServed();
void heap(uintptr_t position, bool failed);
}
extern "C" {
extern volatile uint32_t artoo_fault_active;
extern volatile uint32_t artoo_current_task;
extern volatile uint32_t artoo_fault_record[16];
void artoo_fault_halt() __attribute__((noreturn));
void artoo_capture_fault(uint32_t *sp, uint32_t excReturn, uint32_t ipsr) __attribute__((noreturn));
}
#endif
