#include "app.h"
#include "main.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_def.h"
#include "stm32f4xx_hal_gpio.h"
#include "stm32f4xx_hal_uart.h"
#include "ring_buffer.h"
#include "nrf24l01.h"
#include "fhss.h"
#include <string.h>
#include <stdio.h>
#include "st7735.h"

#define FRAME_WIDTH  160u
#define FRAME_HEIGHT 128u
#define FRAME_SIZE   (FRAME_WIDTH * FRAME_HEIGHT * 2u)

extern TIM_HandleTypeDef  htim2;
extern TIM_HandleTypeDef  htim5;
extern SPI_HandleTypeDef  hspi1;
extern SPI_HandleTypeDef  hspi2;
extern UART_HandleTypeDef huart2;

static NRF24L01_HandleTypeDef hnrf;
static FHSS_HandleTypeDef     hfhss;

static const uint8_t RX_ADDRESS[5] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};

uint8_t discovery_channels[] = {30, 40, 50};
uint8_t hopping_channels[]   = {30, 35, 40, 45, 50, 55, 60, 65, 70};

ColorMode_t color_mode     = COLOR_MODE_RGB565;
uint32_t    bytes_consumed = 0;
int16_t     odd_byte       = -1;

static volatile uint8_t trace_dump_pending = 0;

//
uint8_t spi_tx_buffer[FRAME_CHUNK_SIZE * 2];

uint8_t          read_idx    = 0;
volatile uint8_t dma_free    = 1;
volatile uint8_t pixel_count = 0;
//


static void SetupRadio(void)
{
  hnrf.hspi     = &hspi2;
  hnrf.csn_port = NRF24L01_CSN_GPIO_Port;
  hnrf.csn_pin  = NRF24L01_CSN_Pin;
  hnrf.ce_port  = NRF24L01_CE_GPIO_Port;
  hnrf.ce_pin   = NRF24L01_CE_Pin;

  NRF24L01_ConfigTypeDef cfg = NRF24L01_DefaultConfig();
  if (NRF24L01_Init(&hnrf, &cfg) != NRF24L01_OK)
  {
    Error_Handler();
  }

  NRF24L01_OpenRxPipe(&hnrf, 0, RX_ADDRESS);
  NRF24L01_SetDynamicAck(&hnrf, true);
  NRF24L01_SetAutoAck(&hnrf, 0, true);
  NRF24L01_SetRFParams(&hnrf, NRF24L01_DATA_RATE_2MBPS, NRF24L01_TX_PWR_0DBM);
  NRF24L01_SetDynamicPayloadLength(&hnrf, 0, true);
}

static void SetupFHSS(void)
{
  hfhss.hnrf = &hnrf;
  hfhss.htim = &htim2;

  hfhss.config.hopping_channels     = hopping_channels;
  hfhss.config.hopping_channels_len = sizeof(hopping_channels);

  hfhss.config.discovery_channels     = discovery_channels;
  hfhss.config.discovery_channels_len = sizeof(discovery_channels);

  hfhss.config.dwell_time_us      = 10000;
  hfhss.config.tx_window_start_us = 500;
  hfhss.config.tx_window_end_us   = 9500;

  hfhss.config.advertising_tx_period_us = 20000;
  hfhss.config.scanning_rx_window_us    = 100000;

  hfhss.config.max_tx_failures     = 5;
  hfhss.config.max_failed_channels = 10;

  hfhss.config.secret_seed = 0xABCDEFAB;

#ifdef FHSS_ENABLE_TRACE
  hfhss.htim_trace = &htim5;
#endif

  FHSS_Init(&hfhss);
}

void App_Init(void)
{
  ST7735_Init();
  ST7735_FillScreen(ST7735_BLACK);

  SetupRadio();
  SetupFHSS();

#ifdef FHSS_ENABLE_TRACE
  HAL_TIM_Base_Start(&htim5);
#endif

  FHSS_StartScanning(&hfhss);
}

void App_Loop(void)
{
#ifdef FHSS_ENABLE_TRACE
  if (trace_dump_pending || (HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_RESET))
  {
    trace_dump_pending = 0;

    FHSS_Terminate(&hfhss, FHSS_DISCONNECT_LOCAL_CLOSED);
    FHSS_DumpTrace(&hfhss, &huart2);

    while (1)
    {
    }
  }
#endif

  FHSS_Process(&hfhss);

  if (dma_free)
  {
    static uint8_t rx[4096];
    uint32_t       bytes_left;
    uint32_t       to_read;
    uint32_t       n;

    if (odd_byte >= 0)
    {
      bytes_left = FRAME_SIZE - bytes_consumed - 1;
      to_read    = (bytes_left > 4095) ? 4095 : bytes_left;
      rx[0]      = (uint8_t)odd_byte;
      odd_byte   = -1;
      n          = FHSS_Receive(&hfhss, &rx[1], to_read) + 1;
    }
    else
    {
      bytes_left = FRAME_SIZE - bytes_consumed;
      to_read    = (bytes_left > 4096) ? 4096 : bytes_left;
      n          = FHSS_Receive(&hfhss, rx, to_read);
    }

    // char uart_buf[32];
    // int  len = snprintf(uart_buf, sizeof(uart_buf), "n: %lu\r\n", n);
    // HAL_UART_Transmit(&huart2, (uint8_t *)uart_buf, len, HAL_MAX_DELAY);

    if (n % 2 != 0)
    {
      odd_byte = rx[n - 1];
      n--;
    }

    if (n > 0)
    {
      dma_free = 0;

      if (bytes_consumed == 0)
      {
        ST7735_StartVideoFrame();
      }

      if (color_mode == COLOR_MODE_RGB565)
      {
        ST7735_WritePixelsDMA(rx, n);
        bytes_consumed += n;
      }

      if (bytes_consumed >= FRAME_SIZE)
      {
        ST7735_EndVideoFrame();
        bytes_consumed = 0;
      }
    }
  }
}

void FHSS_OnDisconnectedEvent(FHSS_HandleTypeDef *handle, FHSS_DisconnectReasonTypeDef reason)
{
  if (handle == &hfhss)
  {
    trace_dump_pending = 1;
  }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == NRF24L01_IRQ_Pin)
  {
    FHSS_RadioIRQCallback(&hfhss);
  }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2)
  {
    FHSS_TimerIRQCallback(&hfhss);
  }
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI1)
  {
    dma_free = 1;
  }
}