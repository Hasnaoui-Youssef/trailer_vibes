/* W1 - baseline nested branch, deterministic fixed-trip loop.
 * Intentionally the same control-flow shape as the existing
 * test_resources/stm32h7s3x_dummy capture behind Table 6.4, so this
 * workload's density figures are directly comparable to that existing
 * baseline under the new, systematic protocol.
 */
#include "main.h"

int main(void)
{
  SCB_EnableICache();
  SCB_EnableDCache();

  HAL_Init();
  SystemClock_Config();

  while (1)
  {
    int x = 0;
    for (int i = 0; i < 5; i++)
    {
      x += i * i * i + 4;
      if (x > 25)
      {
        if (x > 49)
        {
          x += 13;
        }
        else
        {
          x += 22;
        }
      }
    }
  }
}
