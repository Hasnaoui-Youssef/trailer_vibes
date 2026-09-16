/* W7 - fault-handler root-cause case study.
 *
 * `process` writes one byte past the end of `record.buffer` on every call
 * (a classic off-by-one stray write). `record.buffer` sits immediately
 * before `record.on_complete` in memory, so the stray write lands on the
 * low byte of that function pointer. The corrupting byte is always 0x00,
 * which clears bit 0 of the pointer - branching to a non-Thumb address via
 * an indirect call is an architecturally guaranteed UsageFault (INVSTATE)
 * on Cortex-M, regardless of what is actually stored at the target
 * address, so the fault is reliable on every pass rather than dependent on
 * what garbage the corrupted pointer happens to hold.
 *
 * This is deliberately the class of defect Chapter 1 motivates trace with:
 * at the halt, the fault status registers and the corrupted pointer value
 * are visible, but the store instruction that corrupted it is not, unless
 * an execution record reaches back far enough to show it.
 */
#include "main.h"

typedef void (*action_fn)(void);

typedef struct
{
  uint8_t buffer[8];
  action_fn on_complete;
} record_t;

static record_t record;

static void SafeAction(void)
{
  volatile uint32_t marker = 0xC0FFEEu;
  (void)marker;
}

static void Process(const uint8_t *data, int len)
{
  for (int i = 0; i <= len; i++)
  {
    record.buffer[i] = data[i % len];
  }
}

int main(void)
{
  SCB_EnableICache();
  SCB_EnableDCache();

  HAL_Init();
  SystemClock_Config();

  static const uint8_t payload[8] = {0x00, 2, 3, 4, 5, 6, 7, 8};

  while (1)
  {
    record.on_complete = SafeAction;
    Process(payload, 8);
    record.on_complete();
  }
}
