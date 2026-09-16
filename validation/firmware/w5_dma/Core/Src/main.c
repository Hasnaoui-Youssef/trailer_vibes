/* W5 - DMA transfer + completion IRQ, CPU mostly idle-looping. Anchors the
 * high end of the instructions/byte spread: long stretches of a plain
 * spin-wait between DMA-driven interrupt bursts.
 */
#include "main.h"

#define BUFFER_WORDS 64

DMA_HandleTypeDef handle_HPDMA1_Channel12;

static const uint32_t src_buffer[BUFFER_WORDS] = {
    0x00010203u, 0x04050607u, 0x08090A0Bu, 0x0C0D0E0Fu,
};
static uint32_t dest_buffer[BUFFER_WORDS];

static volatile uint32_t transfer_complete = 0;
static volatile uint32_t transfer_error = 0;

static void TransferComplete(DMA_HandleTypeDef *hdma)
{
  (void)hdma;
  transfer_complete = 1;
}

static void TransferError(DMA_HandleTypeDef *hdma)
{
  (void)hdma;
  transfer_error = 1;
}

static void MX_HPDMA1_Init(void)
{
  __HAL_RCC_HPDMA1_CLK_ENABLE();

  HAL_NVIC_SetPriority(HPDMA1_Channel12_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(HPDMA1_Channel12_IRQn);

  handle_HPDMA1_Channel12.Instance = HPDMA1_Channel12;
  handle_HPDMA1_Channel12.Init.Request = DMA_REQUEST_SW;
  handle_HPDMA1_Channel12.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
  handle_HPDMA1_Channel12.Init.Direction = DMA_MEMORY_TO_MEMORY;
  handle_HPDMA1_Channel12.Init.SrcInc = DMA_SINC_INCREMENTED;
  handle_HPDMA1_Channel12.Init.DestInc = DMA_DINC_INCREMENTED;
  handle_HPDMA1_Channel12.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_WORD;
  handle_HPDMA1_Channel12.Init.DestDataWidth = DMA_DEST_DATAWIDTH_WORD;
  handle_HPDMA1_Channel12.Init.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
  handle_HPDMA1_Channel12.Init.SrcBurstLength = 1;
  handle_HPDMA1_Channel12.Init.DestBurstLength = 1;
  handle_HPDMA1_Channel12.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
  handle_HPDMA1_Channel12.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
  handle_HPDMA1_Channel12.Init.Mode = DMA_NORMAL;
  if (HAL_DMA_Init(&handle_HPDMA1_Channel12) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_DMA_ConfigChannelAttributes(&handle_HPDMA1_Channel12, DMA_CHANNEL_NPRIV) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_DMA_RegisterCallback(&handle_HPDMA1_Channel12, HAL_DMA_XFER_CPLT_CB_ID, TransferComplete);
  HAL_DMA_RegisterCallback(&handle_HPDMA1_Channel12, HAL_DMA_XFER_ERROR_CB_ID, TransferError);
}

int main(void)
{
  SCB_EnableICache();
  SCB_EnableDCache();

  HAL_Init();
  SystemClock_Config();
  MX_HPDMA1_Init();

  while (1)
  {
    for (int i = 0; i < BUFFER_WORDS; i++)
    {
      dest_buffer[i] = 0;
    }
    transfer_complete = 0;
    transfer_error = 0;

    if (HAL_DMA_Start_IT(&handle_HPDMA1_Channel12, (uint32_t)src_buffer, (uint32_t)dest_buffer,
                          BUFFER_WORDS * sizeof(uint32_t)) != HAL_OK)
    {
      Error_Handler();
    }

    while ((transfer_complete == 0) && (transfer_error == 0))
    {
      /* Idle spin while the DMA engine runs the transfer. */
    }

    if (transfer_error != 0)
    {
      Error_Handler();
    }

    for (volatile uint32_t idle = 0; idle < 20000u; idle++)
    {
      /* Idle stretch between transfers - the low-density part of this
       * workload's trace. */
    }
  }
}
