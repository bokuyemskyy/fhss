#include "app.h"
#include "main.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_uart.h"
#include "nrf24l01.h"
#include "fhss.h"
#include <string.h>

extern TIM_HandleTypeDef  htim2;
extern TIM_HandleTypeDef  htim5;
extern SPI_HandleTypeDef  hspi2;
extern UART_HandleTypeDef huart2;

static NRF24L01_HandleTypeDef hnrf;
static FHSS_HandleTypeDef     hfhss;
static const uint8_t          TX_ADDRESS[5] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};

#define SCENARIO_FHSS_ACK       1
#define SCENARIO_FHSS_NO_ACK    2
#define SCENARIO_NO_FHSS_ACK    3
#define SCENARIO_NO_FHSS_NO_ACK 4

#define ACTIVE_SCENARIO SCENARIO_NO_FHSS_NO_ACK

#define BENCHMARK_DURATION_MS 10000

static uint32_t benchmark_start = 0;
static uint8_t  benchmark_done  = 0;
static uint8_t  dummy_data[64];

uint8_t discovery_channels[] = {30, 40, 50};
uint8_t hopping_channels[]   = {30, 35, 40, 45, 50, 55, 60, 65, 70};

uint8_t single_channel[] = {40};

static void SetupRadio(void)
{
  hnrf.hspi     = &hspi2;
  hnrf.csn_port = NRF24L01_CSN_GPIO_Port;
  hnrf.csn_pin  = NRF24L01_CSN_Pin;
  hnrf.ce_port  = NRF24L01_CE_GPIO_Port;
  hnrf.ce_pin   = NRF24L01_CE_Pin;

  NRF24L01_ConfigTypeDef cfg = NRF24L01_DefaultConfig();
  NRF24L01_Init(&hnrf, &cfg);
  NRF24L01_OpenTxPipe(&hnrf, TX_ADDRESS);
  NRF24L01_SetDynamicAck(&hnrf, true);
  NRF24L01_SetAutoAck(&hnrf, 0, true);
  NRF24L01_SetRFParams(&hnrf, NRF24L01_DATA_RATE_2MBPS, NRF24L01_TX_PWR_0DBM);
  NRF24L01_SetDynamicPayloadLength(&hnrf, 0, true);
}

static void SetupFHSS(void)
{
  hfhss.hnrf                            = &hnrf;
  hfhss.htim                            = &htim2;
  hfhss.config.discovery_channels       = discovery_channels;
  hfhss.config.discovery_channels_len   = sizeof(discovery_channels);
  hfhss.config.max_tx_failures          = 5;
  hfhss.config.max_failed_channels      = 10;
  hfhss.config.secret_seed              = 0xABCDEFAB;
  hfhss.config.advertising_tx_period_us = 20000;
  hfhss.config.scanning_rx_window_us    = 100000;

#if (ACTIVE_SCENARIO == SCENARIO_FHSS_ACK || ACTIVE_SCENARIO == SCENARIO_FHSS_NO_ACK)
  hfhss.config.hopping_channels     = hopping_channels;
  hfhss.config.hopping_channels_len = sizeof(hopping_channels);
  hfhss.config.dwell_time_us        = 10000;
  hfhss.config.tx_window_start_us   = 500;
  hfhss.config.tx_window_end_us     = 9500;
#else
  hfhss.config.hopping_channels     = single_channel;
  hfhss.config.hopping_channels_len = sizeof(single_channel);
  hfhss.config.dwell_time_us        = 15000000;
  hfhss.config.tx_window_start_us   = 500;
  hfhss.config.tx_window_end_us     = 14999500;
#endif

#if (ACTIVE_SCENARIO == SCENARIO_FHSS_NO_ACK || ACTIVE_SCENARIO == SCENARIO_NO_FHSS_NO_ACK)
  hfhss.config.disable_data_acks = true;
#else
  hfhss.config.disable_data_acks = false;
#endif

#ifdef FHSS_ENABLE_TRACE
  hfhss.htim_trace = &htim5;
#endif

  FHSS_Init(&hfhss);
}

void App_Init(void)
{
  SetupRadio();
  SetupFHSS();
  memset(dummy_data, 0xAA, sizeof(dummy_data));

#ifdef FHSS_ENABLE_TRACE
  HAL_TIM_Base_Start(&htim5);
#endif

  benchmark_start = HAL_GetTick();
  FHSS_StartAdvertising(&hfhss);
}

void App_Loop(void)
{
  FHSS_Process(&hfhss);

  if (!benchmark_done && (hfhss.state == FHSS_STATE_TX_HOPPING))
  {
    uint32_t now = HAL_GetTick();
    if (now - benchmark_start < BENCHMARK_DURATION_MS)
    {
      uint16_t free_space = FHSS_Free(&hfhss);
      if (free_space > 0)
      {
        uint16_t to_write = (free_space > sizeof(dummy_data)) ? sizeof(dummy_data) : free_space;
        FHSS_Transmit(&hfhss, dummy_data, to_write);
      }
    }
    else
    {
      benchmark_done = 1;
      FHSS_Terminate(&hfhss, FHSS_DISCONNECT_LOCAL_CLOSED);
      FHSS_DumpStats(&hfhss, &huart2);
    }
  }
}

void App_USB_Receive(uint8_t *buf, uint32_t len)
{
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == NRF24L01_IRQ_Pin)
    FHSS_RadioIRQCallback(&hfhss);
}
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2)
    FHSS_TimerIRQCallback(&hfhss);
}
void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == hfhss.htim->Instance && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
    FHSS_SyncEventCallback(&hfhss);
}