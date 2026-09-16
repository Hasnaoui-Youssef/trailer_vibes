/* W3 - branch-dense workload: a bubble sort over a fixed 16-element array,
 * re-seeded from a const template every pass so the branch pattern (which
 * comparisons take the swap) repeats identically each iteration. Anchors
 * the low end of the instructions/byte spread: most executed instructions
 * here are a compare immediately followed by a conditional branch.
 */
#include "main.h"

static const uint16_t kTemplate[16] = {
    13, 2, 19, 7, 1, 25, 3, 42, 8, 15, 6, 30, 11, 4, 22, 9,
};

static uint16_t data[16];

static void BubbleSort(uint16_t *arr, int n)
{
  for (int i = 0; i < n - 1; i++)
  {
    for (int j = 0; j < n - i - 1; j++)
    {
      if (arr[j] > arr[j + 1])
      {
        uint16_t tmp = arr[j];
        arr[j] = arr[j + 1];
        arr[j + 1] = tmp;
      }
    }
  }
}

int main(void)
{
  SCB_EnableICache();
  SCB_EnableDCache();

  HAL_Init();
  SystemClock_Config();

  while (1)
  {
    for (int i = 0; i < 16; i++)
    {
      data[i] = kTemplate[i];
    }
    BubbleSort(data, 16);
  }
}
