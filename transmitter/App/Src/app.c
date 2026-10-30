#include "app.h"
#include "main.h"
#include "stm32f4xx_hal_def.h"
#include "stm32f4xx_hal_uart.h"
#include "usbd_cdc_if.h"
#include "ring_buffer.h"
#include "nrf24l01.h"
#include "fhss.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define USB_RING_BUF_SIZE 4096u
#define CDC_FS_MAX_PACKET 64u

#define FRAME_WIDTH  160u
#define FRAME_HEIGHT 128u
#define FRAME_SIZE   (FRAME_WIDTH * FRAME_HEIGHT * 2u)

extern TIM_HandleTypeDef  htim2;
extern TIM_HandleTypeDef  htim5;
extern SPI_HandleTypeDef  hspi2;
extern UART_HandleTypeDef huart2;
extern USBD_HandleTypeDef hUsbDeviceFS;
extern uint8_t            UserRxBufferFS[];

static NRF24L01_HandleTypeDef hnrf;
static FHSS_HandleTypeDef     hfhss;

static const uint8_t TX_ADDRESS[5] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};

uint8_t discovery_channels[] = {30, 40, 50};
uint8_t hopping_channels[]   = {30, 35, 40, 45, 50, 55, 60, 65, 70};

static volatile uint8_t usb_rx_paused = 0;

static volatile uint8_t trace_dump_pending = 0;

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

  NRF24L01_OpenTxPipe(&hnrf, TX_ADDRESS);
  NRF24L01_SetDynamicAck(&hnrf, true);
  NRF24L01_SetAutoAck(&hnrf, 0, true);
  NRF24L01_SetRFParams(&hnrf, NRF24L01_DATA_RATE_2MBPS, NRF24L01_TX_PWR_0DBM);
  NRF24L01_SetDynamicPayloadLength(&hnrf, 0, true);
}

static uint8_t dat[] = {'1'};

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
  SetupRadio();
  SetupFHSS();

#ifdef FHSS_ENABLE_TRACE
  HAL_TIM_Base_Start(&htim5);
#endif

  FHSS_StartAdvertising(&hfhss);
}


void App_Loop(void)
{
#ifdef FHSS_ENABLE_TRACE
  if (trace_dump_pending)
  {
    trace_dump_pending = 0;

    FHSS_Terminate(&hfhss, FHSS_DISCONNECT_LOCAL_CLOSED);
    FHSS_DumpTrace(&hfhss, &huart2);
    FHSS_DumpStats(&hfhss, &huart2);
    while (1)
    {
    }
  }
#endif

  FHSS_Process(&hfhss);

  if (usb_rx_paused)
  {
    if (FHSS_Free(&hfhss) >= CDC_FS_MAX_PACKET)
    {
      USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
      USBD_CDC_ReceivePacket(&hUsbDeviceFS);
      usb_rx_paused = 0;
    }
  }
}

void App_USB_Receive(uint8_t *buf, uint32_t len)
{
  FHSS_Transmit(&hfhss, buf, len);

  if (FHSS_Free(&hfhss) >= CDC_FS_MAX_PACKET)
  {
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  }
  else
  {
    usb_rx_paused = 1;
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

void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == hfhss.htim->Instance)
  {
    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
    {
      FHSS_SyncEventCallback(&hfhss);
    }
  }
}