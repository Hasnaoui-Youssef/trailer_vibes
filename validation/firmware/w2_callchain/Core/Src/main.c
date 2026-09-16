/* W2 - deep call chain, mixed inlined/non-inlined, stresses function-block
 * grouping and inline-frame attribution across several call depths.
 */
#include "main.h"

static inline uint32_t leaf_inline(uint32_t x)
{
  return (x * 3u) + 1u;
}

static uint32_t level4(uint32_t x)
{
  return leaf_inline(x) ^ 0x5A5Au;
}

static uint32_t level3(uint32_t x)
{
  return level4(x) + level4(x >> 1);
}

static uint32_t level2(uint32_t x)
{
  uint32_t acc = 0;
  for (uint32_t i = 0; i < 3; i++)
  {
    acc += level3(x + i);
  }
  return acc;
}

static uint32_t level1(uint32_t x)
{
  return level2(x) - level2(x >> 2);
}

int main(void)
{
  SCB_EnableICache();
  SCB_EnableDCache();

  HAL_Init();
  SystemClock_Config();

  volatile uint32_t result = 0;

  while (1)
  {
    for (uint32_t seed = 0; seed < 8; seed++)
    {
      result = level1(result + seed);
    }
  }
}
