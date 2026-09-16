/* gfxmmu_trigger - single-purpose Phase 8 firmware. Performs exactly one
 * read from the GFXMMU region (0x2500_0000-0x25FF_FFFF); per ES0596
 * SS2.2.17 this hangs the system on this device when the GFXMMU clock is
 * disabled, which it is here (never enabled). Set a breakpoint on the
 * marked line, arm trace, then continue once - see the campaign's Phase 8
 * protocol.
 */
#include "main.h"

int main(void)
{
  SCB_EnableICache();
  SCB_EnableDCache();

  HAL_Init();
  SystemClock_Config();

  volatile uint32_t *gfxmmu_addr = (volatile uint32_t *)0x25000000u;
  volatile uint32_t value = *gfxmmu_addr; /* breakpoint here for Phase 8 */
  (void)value;

  while (1)
  {
  }
}
