#ifndef FHSS_H
#define FHSS_H

#include "nrf24l01.h"
#include "ring_buffer.h"
#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define FHSS_TX_BUFFER_SIZE 1024
#define FHSS_RX_BUFFER_SIZE 1024
#define FHSS_RX_BACKPRESSURE_LIMIT 512

#ifdef FHSS_ENABLE_TRACE
#define FHSS_TRACE_BUFFER_SIZE 4096

typedef enum {
  FHSS_EVT_CHANGE_STATE = 0,
  FHSS_EVT_CHANGE_CHANNEL,
  FHSS_EVT_IRQ_TX_DS,
  FHSS_EVT_IRQ_MAX_RT,
  FHSS_EVT_IRQ_RX_DR,
  FHSS_EVT_TIMER,
  FHSS_EVT_PKT_SENT,
  FHSS_EVT_PKT_RECV,
  FHSS_EVT_RX_MISSED_SYNC,
  FHSS_EVT_TX_NO_ACK,
  FHSS_EVT_TX_CHANNEL_MUTED,
  FHSS_EVT_DEBUG,
  FHSS_EVT_RX_BACKPRESSURE
} FHSS_TraceEvent_e;

typedef struct {
  uint32_t timestamp_us;
  uint32_t arg;
  uint8_t event_id;
} FHSS_TraceEntry_t;
#endif

#ifdef FHSS_ENABLE_STATS
typedef struct {
  uint32_t start_time_ms;
  uint32_t end_time_ms;
  uint32_t total_bytes_transmitted;
  uint32_t total_tx_packets;
  uint32_t total_max_rt_errors;
  uint32_t backpressure_hits;
  uint32_t tx_buffer_starves;
  bool is_starved;
} FHSS_StatsTypeDef;
#endif

typedef enum {
  FHSS_STATE_IDLE,
  FHSS_STATE_TX_ADVERTISING,
  FHSS_STATE_RX_SCANNING,
  FHSS_STATE_TX_HOPPING,
  FHSS_STATE_RX_HOPPING,
  FHSS_STATE_TERMINATING
} FHSS_StateTypeDef;

typedef enum {
  FHSS_DISCONNECT_TIMEOUT,
  FHSS_DISCONNECT_PEER_CLOSED,
  FHSS_DISCONNECT_LOCAL_CLOSED
} FHSS_DisconnectReasonTypeDef;

typedef enum {
  FHSS_PKT_ADV_SYNC = 1,
  FHSS_PKT_HOP_SYNC,
  FHSS_PKT_DATA,
  FHSS_PKT_TERM
} FHSS_PacketTypeDef;

typedef enum {
  FHSS_ACK_RESUME = 0x00,
  FHSS_ACK_MUTE = 0x01,
  FHSS_ACK_TERM = 0x02
} FHSS_AckPayloadTypeDef;

typedef struct {
  uint8_t *hopping_channels;
  size_t hopping_channels_len;

  uint8_t *discovery_channels;
  size_t discovery_channels_len;

  uint32_t dwell_time_us;
  uint32_t tx_window_start_us;
  uint32_t tx_window_end_us;

  uint32_t advertising_tx_period_us;
  uint32_t scanning_rx_window_us;

  uint8_t max_tx_failures;
  uint8_t max_failed_channels;

  bool disable_data_acks;

  uint32_t secret_seed;
} FHSS_ConfigTypeDef;

typedef struct {
  NRF24L01_HandleTypeDef *hnrf;
  TIM_HandleTypeDef *htim;
  FHSS_ConfigTypeDef config;

  // Common
  FHSS_StateTypeDef state;
  uint32_t absolute_hop_count;
  uint8_t advertising_channel_index;
  uint8_t failed_channels;

  // TX
  bool hop_acked;
  bool sync_sent;
  uint8_t tx_failures;
  bool channel_muted;
  uint8_t tx_seq;
  uint8_t active_rx_pipe;

  // RX
  bool sync_received;
  uint8_t rx_seq;
  bool rx_backpressure_active;

  // Buffers
  uint8_t tx_rb_data[FHSS_TX_BUFFER_SIZE];
  RingBuffer_t tx_rb;
  uint8_t rx_rb_data[FHSS_RX_BUFFER_SIZE];
  RingBuffer_t rx_rb;
  uint8_t in_flight_data[8];
  RingBuffer_t in_flight_rb;
  uint8_t in_flight_sum;

#ifdef FHSS_ENABLE_TRACE
  FHSS_TraceEntry_t trace_buffer[FHSS_TRACE_BUFFER_SIZE];
  uint16_t trace_head;
  bool trace_wrapped;
  TIM_HandleTypeDef *htim_trace;
#endif

#ifdef FHSS_ENABLE_STATS
  FHSS_StatsTypeDef stats;
#endif
} FHSS_HandleTypeDef;

void FHSS_Init(FHSS_HandleTypeDef *handle);

void FHSS_StartAdvertising(FHSS_HandleTypeDef *handle);
void FHSS_StopAdvertising(FHSS_HandleTypeDef *handle);
void FHSS_StartScanning(FHSS_HandleTypeDef *handle);
void FHSS_StopScanning(FHSS_HandleTypeDef *handle);

void FHSS_TimerIRQCallback(FHSS_HandleTypeDef *handle);
void FHSS_RadioIRQCallback(FHSS_HandleTypeDef *handle);
void FHSS_SyncEventCallback(FHSS_HandleTypeDef *handle);
void FHSS_Process(FHSS_HandleTypeDef *handle);

uint16_t FHSS_Transmit(FHSS_HandleTypeDef *handle, const uint8_t *data,
                       uint16_t len);
uint16_t FHSS_Free(FHSS_HandleTypeDef *handle);
uint16_t FHSS_Available(FHSS_HandleTypeDef *handle);
uint16_t FHSS_Receive(FHSS_HandleTypeDef *handle, uint8_t *buffer,
                      uint16_t max_len);

void FHSS_Terminate(FHSS_HandleTypeDef *handle,
                    FHSS_DisconnectReasonTypeDef reason);
void FHSS_Disconnect(FHSS_HandleTypeDef *handle);
void FHSS_OnDisconnectedEvent(FHSS_HandleTypeDef *handle,
                              FHSS_DisconnectReasonTypeDef reason);

#ifdef FHSS_ENABLE_TRACE

void FHSS_TraceLog(FHSS_HandleTypeDef *handle, FHSS_TraceEvent_e event,
                   uint32_t arg);
void FHSS_DumpTrace(FHSS_HandleTypeDef *handle, UART_HandleTypeDef *huart);
void FHSS_ResetTrace(FHSS_HandleTypeDef *handle);

#define FHSS_TRACE(h, evt, arg) FHSS_TraceLog((h), (evt), (arg))
#define FHSS_TRACE_RESET(h) FHSS_ResetTrace((h))

#else

#define FHSS_TRACE(h, evt, arg) ((void)0)
#define FHSS_TRACE_RESET(h) ((void)0)

#endif

#ifdef FHSS_ENABLE_STATS
void FHSS_ResetStats(FHSS_HandleTypeDef *handle);
void FHSS_DumpStats(FHSS_HandleTypeDef *handle, UART_HandleTypeDef *huart);

#define FHSS_STATS_RESET(h) FHSS_ResetStats((h))

#else

#define FHSS_STATS_RESET(h) ((void)0)

#endif

#endif