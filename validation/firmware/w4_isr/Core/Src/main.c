/* W4 - periodic interrupt workload: TIM6 update interrupt drives real
 * exception entry/exit into the trace while the main loop runs a small
 * compute pattern whose branch outcome depends on the ISR's tick count.
 */
#include "main.h"

TIM_HandleTypeDef htim6;
static volatile uint32_t isr_tick_count = 0;

static void MX_TIM6_Init(void)
{
  __HAL_RCC_TIM6_CLK_ENABLE();

  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 4800 - 1;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 2000 - 1;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_NVIC_SetPriority(TIM6_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(TIM6_IRQn);

  if (HAL_TIM_Base_Start_IT(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM6)
  {
    isr_tick_count++;
  }
}

int main(void)
{
  SCB_EnableICache();
  SCB_EnableDCache();

  HAL_Init();
  SystemClock_Config();
  MX_TIM6_Init();

  uint32_t x = 0;

  while (1)
  {
    for (int i = 0; i < 5; i++)
    {
      x += (isr_tick_count & 1u) ? (i * 2u + 1u) : (i * 3u + 2u);
    }
  }
}
