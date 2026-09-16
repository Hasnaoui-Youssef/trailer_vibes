/* W6 - deliberate buffer overflow workload: the tightest, most
 * branch-dense loop in this campaign (a decision every iteration, minimal
 * instructions between branches), run indefinitely, to fill the TMC's
 * 2 KB on-chip sink as fast as possible once the capture window is held
 * open past the buffer's capacity (Phase 3's "long" capture point).
 */
#include "main.h"

int main(void)
{
  SCB_EnableICache();
  SCB_EnableDCache();

  HAL_Init();
  SystemClock_Config();

  volatile uint32_t counter = 0;

  while (1)
  {
    counter++;
    if (counter & 1u)
    {
      counter += 3u;
    }
    else
    {
      counter += 5u;
    }
  }
}
