#include "fhss.h"
#include "nrf24l01.h"
#include "ring_buffer.h"
#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SYNC_PAYLOAD_BYTES 9
#define MAX_PAYLOAD_BYTES 30
#define TX_RX_ACK_DELAY 37
#define RX_SYNC_LEAD_OFFSET 0
#define HOP_FLIGHT_TIME 230

// Logging
#ifdef FHSS_ENABLE_TRACE
void FHSS_TraceLog(FHSS_HandleTypeDef *handle, FHSS_TraceEvent_e event,
                   uint32_t arg) {
  if (handle == NULL || handle->htim_trace == NULL)
    return;

  uint32_t primask_bit = __get_PRIMASK();
  __disable_irq();

  handle->trace_buffer[handle->trace_head].timestamp_us =
      __HAL_TIM_GET_COUNTER(handle->htim_trace);
  handle->trace_buffer[handle->trace_head].event_id = (uint8_t)event;
  handle->trace_buffer[handle->trace_head].arg = arg;

  handle->trace_head++;
  if (handle->trace_head >= FHSS_TRACE_BUFFER_SIZE) {
    handle->trace_head = 0;
    handle->trace_wrapped = true;
  }

  __set_PRIMASK(primask_bit);
}

void FHSS_DumpTrace(FHSS_HandleTypeDef *handle, UART_HandleTypeDef *huart) {
  if (handle == NULL || huart == NULL)
    return;

  static const char *TraceEventNames[] = {
      "FHSS_EVT_CHANGE_STATE",     "FHSS_EVT_CHANGE_CHANNEL",
      "FHSS_EVT_IRQ_TX_DS",        "FHSS_EVT_IRQ_MAX_RT",
      "FHSS_EVT_IRQ_RX_DR",        "FHSS_EVT_TIMER",
      "FHSS_EVT_PKT_SENT",         "FHSS_EVT_PKT_RECV",
      "FHSS_EVT_RX_MISSED_SYNC",   "FHSS_EVT_TX_NO_ACK",
      "FHSS_EVT_TX_CHANNEL_MUTED", "FHSS_EVT_DEBUG",
      "FHSS_EVT_RX_BACKPRESSURE"};

  char uart_buf[128];
  uint16_t start_idx = handle->trace_wrapped ? handle->trace_head : 0;
  uint16_t count =
      handle->trace_wrapped ? FHSS_TRACE_BUFFER_SIZE : handle->trace_head;

  snprintf(uart_buf, sizeof(uart_buf), "Timestamp_us,Event,Arg\r\n");
  HAL_UART_Transmit(huart, (uint8_t *)uart_buf, strlen(uart_buf),
                    HAL_MAX_DELAY);

  for (uint16_t i = 0; i < count; i++) {
    uint16_t idx = (start_idx + i) % FHSS_TRACE_BUFFER_SIZE;
    FHSS_TraceEntry_t *entry = &handle->trace_buffer[idx];

    const size_t num_events =
        sizeof(TraceEventNames) / sizeof(TraceEventNames[0]);
    const char *evt_name = (entry->event_id < num_events)
                               ? TraceEventNames[entry->event_id]
                               : "UNKNOWN";

    snprintf(uart_buf, sizeof(uart_buf), "%lu,%s,%lu\r\n", entry->timestamp_us,
             evt_name, entry->arg);

    HAL_UART_Transmit(huart, (uint8_t *)uart_buf, strlen(uart_buf),
                      HAL_MAX_DELAY);
  }
}

void FHSS_ResetTrace(FHSS_HandleTypeDef *handle) {
  handle->trace_head = 0;
  handle->trace_wrapped = false;
}
#endif

#ifdef FHSS_ENABLE_STATS
void FHSS_ResetStats(FHSS_HandleTypeDef *handle) {
  if (handle == NULL)
    return;
  memset(&handle->stats, 0, sizeof(FHSS_StatsTypeDef));
}

void FHSS_DumpStats(FHSS_HandleTypeDef *handle, UART_HandleTypeDef *huart) {
  if (handle == NULL || huart == NULL)
    return;

  uint32_t end = (handle->state == FHSS_STATE_IDLE) ? handle->stats.end_time_ms
                                                    : HAL_GetTick();
  uint32_t duration_ms = end - handle->stats.start_time_ms;
  if (duration_ms == 0)
    duration_ms = 1;

  uint32_t dur_sec = duration_ms / 1000;
  uint32_t dur_sec_frac = (duration_ms % 1000) / 10;

  uint32_t tx_kb = handle->stats.total_bytes_transmitted / 1024;
  uint32_t tx_kb_frac =
      ((handle->stats.total_bytes_transmitted % 1024) * 100) / 1024;

  uint32_t tx_bps =
      (uint32_t)(((uint64_t)handle->stats.total_bytes_transmitted * 1000) /
                 duration_ms);
  uint32_t tx_tput_kb = tx_bps / 1024;
  uint32_t tx_tput_kb_frac = ((tx_bps % 1024) * 100) / 1024;

  uint32_t fail_rate_whole = 0;
  uint32_t fail_rate_frac = 0;
  if (handle->stats.total_tx_packets > 0) {
    uint64_t rate_x10000 =
        ((uint64_t)handle->stats.total_max_rt_errors * 10000) /
        handle->stats.total_tx_packets;
    fail_rate_whole = (uint32_t)(rate_x10000 / 100);
    fail_rate_frac = (uint32_t)(rate_x10000 % 100);
  }

  char buf[512];
  snprintf(buf, sizeof(buf),
           "\r\nSession Report\r\n"
           "---------------\r\n"
           "Session Duration : %lu.%02lu sec\r\n"
           "Total TX Pushed  : %lu.%02lu KB\r\n"
           "Avg TX Throughput: %lu.%02lu KB/s\r\n"
           "Total TX Packets : %lu\r\n"
           "MAX_RT Failures  : %lu (%lu.%02lu%%)\r\n"
           "Backpressure Hits: %lu\r\n"
           "TX Buffer Starved: %lu\r\n",
           dur_sec, dur_sec_frac, tx_kb, tx_kb_frac, tx_tput_kb,
           tx_tput_kb_frac, handle->stats.total_tx_packets,
           handle->stats.total_max_rt_errors, fail_rate_whole, fail_rate_frac,
           handle->stats.backpressure_hits, handle->stats.tx_buffer_starves);

  HAL_UART_Transmit(huart, (uint8_t *)buf, strlen(buf), HAL_MAX_DELAY);
}
#endif

// Channels
static inline uint32_t FHSS_CalculateHopHash(uint32_t hop_count,
                                             uint32_t secret_seed) {
  uint32_t k = hop_count ^ secret_seed;
  k ^= k >> 16;
  k *= 0x85ebca6b;
  k ^= k >> 13;
  k *= 0xc2b2ae35;
  k ^= k >> 16;
  return k;
}

static inline uint8_t FHSS_GetHoppingChannel(FHSS_HandleTypeDef *handle) {
  if (handle->config.hopping_channels_len == 0)
    return 0;

  uint32_t hash = FHSS_CalculateHopHash(handle->absolute_hop_count,
                                        handle->config.secret_seed);
  uint32_t array_index = hash % handle->config.hopping_channels_len;
  return handle->config.hopping_channels[array_index];
}

static inline uint8_t FHSS_GetAdvertisingChannel(FHSS_HandleTypeDef *handle) {
  if (handle->config.discovery_channels_len == 0)
    return 0;

  uint32_t array_index =
      handle->advertising_channel_index % handle->config.discovery_channels_len;
  return handle->config.discovery_channels[array_index];
}

static void FHSS_SetChannel(FHSS_HandleTypeDef *handle, uint8_t channel) {
  NRF24L01_SetChannel(handle->hnrf, channel);
  FHSS_TRACE(handle, FHSS_EVT_CHANGE_CHANNEL, channel);
}

static void FHSS_UpdateRxBackpressure(FHSS_HandleTypeDef *handle) {
  if (handle->state != FHSS_STATE_RX_HOPPING &&
      handle->state != FHSS_STATE_RX_SCANNING)
    return;

  uint32_t primask_bit = __get_PRIMASK();
  __disable_irq();

  uint16_t free_space = RB_Free(&handle->rx_rb);
  bool bp_needed = (free_space < FHSS_RX_BACKPRESSURE_LIMIT);

  if (bp_needed != handle->rx_backpressure_active) {
    handle->rx_backpressure_active = bp_needed;
    FHSS_TRACE(handle, FHSS_EVT_RX_BACKPRESSURE, bp_needed ? 1 : 0);

#ifdef FHSS_ENABLE_STATS
    if (bp_needed) {
      handle->stats.backpressure_hits++;
    }
#endif

    NRF24L01_FlushTx(handle->hnrf);
  }

  uint8_t ack_payload[1] = {bp_needed ? FHSS_ACK_MUTE : FHSS_ACK_RESUME};

  while (!NRF24L01_IsTXFull(handle->hnrf)) {
    if (NRF24L01_WriteAckPayload(handle->hnrf, handle->active_rx_pipe,
                                 ack_payload, 1) != NRF24L01_OK) {
      break;
    }
  }

  __set_PRIMASK(primask_bit);
}

static void FHSS_SetRxChannel(FHSS_HandleTypeDef *handle, uint8_t channel) {
  NRF24L01_StopListening(handle->hnrf);
  NRF24L01_FlushRx(handle->hnrf);
  FHSS_SetChannel(handle, channel);
  NRF24L01_StartListening(handle->hnrf);
  FHSS_UpdateRxBackpressure(handle);
}

// Sync packets
static inline void FHSS_BuildSyncPayload(FHSS_HandleTypeDef *handle,
                                         uint8_t pkt_type, uint8_t *buffer) {
  uint32_t current_time = __HAL_TIM_GET_COUNTER(handle->htim);

  buffer[0] = pkt_type;
  buffer[1] = (uint8_t)(current_time & 0xFF);
  buffer[2] = (uint8_t)((current_time >> 8) & 0xFF);
  buffer[3] = (uint8_t)((current_time >> 16) & 0xFF);
  buffer[4] = (uint8_t)((current_time >> 24) & 0xFF);
  buffer[5] = (uint8_t)(handle->absolute_hop_count & 0xFF);
  buffer[6] = (uint8_t)((handle->absolute_hop_count >> 8) & 0xFF);
  buffer[7] = (uint8_t)((handle->absolute_hop_count >> 16) & 0xFF);
  buffer[8] = (uint8_t)((handle->absolute_hop_count >> 24) & 0xFF);
}

static inline uint32_t FHSS_GetSyncTime(FHSS_HandleTypeDef *handle,
                                        const uint8_t *buffer) {
  return buffer[1] | (buffer[2] << 8) | (buffer[3] << 16) | (buffer[4] << 24);
}

static inline uint32_t FHSS_GetSyncHopCount(FHSS_HandleTypeDef *handle,
                                            const uint8_t *buffer) {
  return buffer[5] | (buffer[6] << 8) | (buffer[7] << 16) | (buffer[8] << 24);
}

// State management
static inline void FHSS_SetState(FHSS_HandleTypeDef *handle,
                                 FHSS_StateTypeDef state) {
  handle->state = state;
  FHSS_TRACE(handle, FHSS_EVT_CHANGE_STATE, state);
}

static inline void FHSS_Shutdown(FHSS_HandleTypeDef *handle) {
  HAL_TIM_Base_Stop_IT(handle->htim);
  HAL_TIM_OC_Stop_IT(handle->htim, TIM_CHANNEL_1);
  __HAL_TIM_SET_COUNTER(handle->htim, 0);

  NRF24L01_ClearAllIRQFlags(handle->hnrf);
  NRF24L01_FlushTx(handle->hnrf);
  NRF24L01_FlushRx(handle->hnrf);
  NRF24L01_PowerDown(handle->hnrf);
}

static void FHSS_Reset(FHSS_HandleTypeDef *handle) {
  FHSS_SetState(handle, FHSS_STATE_IDLE);

  handle->absolute_hop_count = 0;
  handle->advertising_channel_index = 0;

  handle->hop_acked = false;
  handle->sync_sent = false;
  handle->sync_received = false;
  handle->tx_failures = 0;
  handle->failed_channels = 0;
  handle->channel_muted = false;
  handle->rx_backpressure_active = false;
  handle->active_rx_pipe = 0;

  handle->in_flight_sum = 0;
  handle->tx_seq = 0;
  handle->rx_seq = 255;

  RB_Reset(&handle->tx_rb);
  RB_Reset(&handle->rx_rb);
  RB_Reset(&handle->in_flight_rb);
}

void FHSS_Init(FHSS_HandleTypeDef *handle) {
  if (handle == NULL || handle->hnrf == NULL || handle->htim == NULL) {
    return;
  }

  RB_Init(&handle->tx_rb, handle->tx_rb_data, FHSS_TX_BUFFER_SIZE);
  RB_Init(&handle->rx_rb, handle->rx_rb_data, FHSS_RX_BUFFER_SIZE);
  RB_Init(&handle->in_flight_rb, handle->in_flight_data, 8);

  FHSS_Reset(handle);

  NRF24L01_SetAutoRetransmission(handle->hnrf, 2, 0);
  NRF24L01_SetDynamicPayloadLength(handle->hnrf, 0, true);
  NRF24L01_SetDynamicAck(handle->hnrf, true);
  NRF24L01_SetAckPayload(handle->hnrf, true);

  FHSS_Shutdown(handle);
}

void FHSS_StartAdvertising(FHSS_HandleTypeDef *handle) {
  if (handle->state != FHSS_STATE_IDLE)
    return;

  FHSS_Reset(handle);
  FHSS_TRACE_RESET(handle);
#ifdef FHSS_ENABLE_STATS
  FHSS_ResetStats(handle);
#endif

  FHSS_SetState(handle, FHSS_STATE_TX_ADVERTISING);

  NRF24L01_PowerUp(handle->hnrf);
  FHSS_SetChannel(handle, FHSS_GetAdvertisingChannel(handle));

  __HAL_TIM_SET_AUTORELOAD(handle->htim,
                           handle->config.advertising_tx_period_us - 1);
  __HAL_TIM_SET_COUNTER(handle->htim, 0);
  HAL_TIM_Base_Start_IT(handle->htim);

  uint8_t buffer[SYNC_PAYLOAD_BYTES];
  FHSS_BuildSyncPayload(handle, FHSS_PKT_ADV_SYNC, buffer);
  NRF24L01_Transmit(handle->hnrf, buffer, SYNC_PAYLOAD_BYTES, false);
  FHSS_TRACE(handle, FHSS_EVT_PKT_SENT, FHSS_PKT_ADV_SYNC);
}

void FHSS_StopAdvertising(FHSS_HandleTypeDef *handle) {
  if (handle->state != FHSS_STATE_TX_ADVERTISING)
    return;

  FHSS_SetState(handle, FHSS_STATE_IDLE);

  FHSS_Shutdown(handle);
}

void FHSS_StartScanning(FHSS_HandleTypeDef *handle) {
  if (handle->state != FHSS_STATE_IDLE)
    return;

  FHSS_Reset(handle);
  FHSS_TRACE_RESET(handle);
#ifdef FHSS_ENABLE_STATS
  FHSS_ResetStats(handle);
#endif

  FHSS_SetState(handle, FHSS_STATE_RX_SCANNING);

  NRF24L01_PowerUp(handle->hnrf);
  FHSS_SetChannel(handle, FHSS_GetAdvertisingChannel(handle));

  __HAL_TIM_SET_AUTORELOAD(handle->htim,
                           handle->config.scanning_rx_window_us - 1);
  __HAL_TIM_SET_COUNTER(handle->htim, 0);
  HAL_TIM_Base_Start_IT(handle->htim);

  NRF24L01_StartListening(handle->hnrf);
}

void FHSS_StopScanning(FHSS_HandleTypeDef *handle) {
  if (handle->state != FHSS_STATE_RX_SCANNING)
    return;

  FHSS_SetState(handle, FHSS_STATE_IDLE);

  NRF24L01_StopListening(handle->hnrf);

  FHSS_Shutdown(handle);
}

void FHSS_Process(FHSS_HandleTypeDef *handle) {
  if (handle->state == FHSS_STATE_TX_HOPPING) {
    uint32_t current_time = __HAL_TIM_GET_COUNTER(handle->htim);

    if (current_time < handle->config.tx_window_start_us ||
        current_time > handle->config.tx_window_end_us || !handle->sync_sent) {
      return;
    }

    while (1) {
      uint32_t primask_bit = __get_PRIMASK();
      __disable_irq();

      if (handle->channel_muted || handle->rx_backpressure_active ||
          RB_IsEmpty(&handle->tx_rb) || NRF24L01_IsTXFull(handle->hnrf)) {
        __set_PRIMASK(primask_bit);
        break;
      }

      if (RB_Available(&handle->in_flight_rb) >= 3) {
        __set_PRIMASK(primask_bit);
        break;
      }

      uint16_t available_bytes =
          RB_Available(&handle->tx_rb) - handle->in_flight_sum;

#ifdef FHSS_ENABLE_STATS
      if (available_bytes == 0) {
        if (!handle->stats.is_starved) {
          handle->stats.tx_buffer_starves++;
          handle->stats.is_starved = true;
        }
      } else {
        handle->stats.is_starved = false;
      }
#endif

      if (available_bytes == 0) {
        __set_PRIMASK(primask_bit);
        break;
      }

      uint8_t bytes_to_write = (available_bytes > MAX_PAYLOAD_BYTES)
                                   ? MAX_PAYLOAD_BYTES
                                   : available_bytes;
      uint8_t buf[32];

      buf[0] = FHSS_PKT_DATA;
      buf[1] = handle->tx_seq++;

      RB_PeekOffset(&handle->tx_rb, handle->in_flight_sum, &buf[2],
                    bytes_to_write);

      NRF24L01_WriteTXPayload(handle->hnrf, buf, bytes_to_write + 2,
                              handle->config.disable_data_acks);
      FHSS_TRACE(handle, FHSS_EVT_PKT_SENT, FHSS_PKT_DATA);

#ifdef FHSS_ENABLE_STATS
      handle->stats.total_tx_packets++;
#endif

      RB_Write(&handle->in_flight_rb, &bytes_to_write, 1);
      handle->in_flight_sum += bytes_to_write;

      __set_PRIMASK(primask_bit);
    }
  }
}

void FHSS_SyncEventCallback(FHSS_HandleTypeDef *handle) {
  if (handle->state == FHSS_STATE_TX_HOPPING && !handle->sync_sent) {
    uint8_t buffer[SYNC_PAYLOAD_BYTES];
    FHSS_BuildSyncPayload(handle, FHSS_PKT_HOP_SYNC, buffer);
    NRF24L01_WriteTXPayload(handle->hnrf, buffer, SYNC_PAYLOAD_BYTES, false);
    FHSS_TRACE(handle, FHSS_EVT_PKT_SENT, FHSS_PKT_HOP_SYNC);
#ifdef FHSS_ENABLE_STATS
    handle->stats.total_tx_packets++;
#endif

    handle->sync_sent = true;

    uint8_t sync_len = 0;
    RB_Write(&handle->in_flight_rb, &sync_len, 1);
  }
}

void FHSS_RadioIRQCallback(FHSS_HandleTypeDef *handle) {
  uint32_t rx_timestamp = __HAL_TIM_GET_COUNTER(handle->htim);

  uint8_t irq = NRF24L01_GetIRQFlags(handle->hnrf);
  NRF24L01_ClearAllIRQFlags(handle->hnrf);

  if (irq & NRF24L01_STATUS_TX_DS) {
    FHSS_TRACE(handle, FHSS_EVT_IRQ_TX_DS, 0);

    handle->hop_acked = true;
    handle->channel_muted = false;

    if (handle->state == FHSS_STATE_TX_ADVERTISING) {
      FHSS_SetState(handle, FHSS_STATE_TX_HOPPING);
#ifdef FHSS_ENABLE_STATS
      handle->stats.start_time_ms = HAL_GetTick();
#endif

      __HAL_TIM_SET_AUTORELOAD(handle->htim, handle->config.dwell_time_us - 1);
      __HAL_TIM_SET_COUNTER(handle->htim, 0);

      __HAL_TIM_SET_COMPARE(handle->htim, TIM_CHANNEL_1,
                            handle->config.tx_window_start_us);

      FHSS_SetChannel(handle, FHSS_GetHoppingChannel(handle));

      NRF24L01_StartBulkTransmit(handle->hnrf);

      handle->sync_sent = false;
      handle->tx_failures = 0;
      handle->failed_channels = 0;
      RB_Reset(&handle->in_flight_rb);
      handle->in_flight_sum = 0;

      HAL_TIM_OC_Start_IT(handle->htim, TIM_CHANNEL_1);
    } else if (handle->state == FHSS_STATE_TX_HOPPING) {
      uint8_t bytes_to_free = 0;

      if (!RB_IsEmpty(&handle->in_flight_rb)) {
        RB_Read(&handle->in_flight_rb, &bytes_to_free, 1);
        RB_AdvanceTail(&handle->tx_rb, bytes_to_free);
        handle->in_flight_sum -= bytes_to_free;
      }
      handle->failed_channels = 0;
      handle->tx_failures = 0;
    }
  }

  if (irq & NRF24L01_STATUS_MAX_RT) {
    FHSS_TRACE(handle, FHSS_EVT_IRQ_MAX_RT, handle->tx_failures);

    NRF24L01_FlushTx(handle->hnrf);
    if (handle->state == FHSS_STATE_TX_ADVERTISING) {
      handle->advertising_channel_index++;

      if (handle->advertising_channel_index <
          handle->config.discovery_channels_len) {
        FHSS_SetChannel(handle, FHSS_GetAdvertisingChannel(handle));

        uint8_t buffer[SYNC_PAYLOAD_BYTES];
        FHSS_BuildSyncPayload(handle, FHSS_PKT_ADV_SYNC, buffer);
        NRF24L01_Transmit(handle->hnrf, buffer, SYNC_PAYLOAD_BYTES, false);
        FHSS_TRACE(handle, FHSS_EVT_PKT_SENT, FHSS_PKT_ADV_SYNC);
      }
    } else if (handle->state == FHSS_STATE_TX_HOPPING) {

#ifdef FHSS_ENABLE_STATS
      handle->stats.total_max_rt_errors++;
#endif

      uint8_t inflight_cnt = RB_Available(&handle->in_flight_rb);
      uint8_t data_packets_flushed = 0;

      for (size_t i = 0; i < inflight_cnt; i++) {
        uint8_t val;
        RB_PeekOffset(&handle->in_flight_rb, i, &val, 1);
        if (val > 0) {
          data_packets_flushed++;
        }
      }

      handle->tx_seq -= data_packets_flushed;

      RB_Reset(&handle->in_flight_rb);
      handle->in_flight_sum = 0;

      handle->tx_failures++;
      if (handle->tx_failures >= handle->config.max_tx_failures &&
          !handle->channel_muted) {
        handle->channel_muted = true;
        FHSS_TRACE(handle, FHSS_EVT_TX_CHANNEL_MUTED,
                   FHSS_GetHoppingChannel(handle));
      }
    }
  }

  if (irq & NRF24L01_STATUS_RX_DR) {
    FHSS_TRACE(handle, FHSS_EVT_IRQ_RX_DR, 0);

    uint8_t buf[32];
    uint8_t len;
    uint8_t pipe;

    while (NRF24L01_Receive(handle->hnrf, buf, &len, &pipe) == NRF24L01_OK) {
      if (len < 1)
        continue;

      handle->active_rx_pipe = pipe;

      if (handle->state == FHSS_STATE_TX_HOPPING ||
          handle->state == FHSS_STATE_TX_ADVERTISING) {
        if (len == 1) {
          if (buf[0] == FHSS_ACK_MUTE) {
            handle->rx_backpressure_active = true;
            FHSS_TRACE(handle, FHSS_EVT_RX_BACKPRESSURE, 1);
          } else if (buf[0] == FHSS_ACK_RESUME) {
            handle->rx_backpressure_active = false;
            FHSS_TRACE(handle, FHSS_EVT_RX_BACKPRESSURE, 0);
          } else if (buf[0] == FHSS_ACK_TERM) {
            FHSS_Terminate(handle, FHSS_DISCONNECT_PEER_CLOSED);
          }
        }
        continue;
      }

      uint32_t end_time = __HAL_TIM_GET_COUNTER(handle->htim);
      uint32_t autoreload = __HAL_TIM_GET_AUTORELOAD(handle->htim);
      uint32_t delta_us;
      if (end_time >= rx_timestamp)
        delta_us = end_time - rx_timestamp;
      else
        delta_us = (autoreload - rx_timestamp) + end_time;

      uint8_t pkt_type = buf[0];
      FHSS_TRACE(handle, FHSS_EVT_PKT_RECV, pkt_type);

      if (handle->state == FHSS_STATE_RX_SCANNING) {
        if (len >= SYNC_PAYLOAD_BYTES && pkt_type == FHSS_PKT_ADV_SYNC) {
          handle->absolute_hop_count = FHSS_GetSyncHopCount(handle, buf);

          handle->state = FHSS_STATE_RX_HOPPING;

          __HAL_TIM_SET_AUTORELOAD(handle->htim,
                                   handle->config.dwell_time_us - 1);
          __HAL_TIM_SET_COUNTER(handle->htim, delta_us - TX_RX_ACK_DELAY +
                                                  RX_SYNC_LEAD_OFFSET);

          FHSS_SetRxChannel(handle, FHSS_GetHoppingChannel(handle));

          handle->sync_received = true;
        }
      } else if (handle->state == FHSS_STATE_RX_HOPPING ||
                 handle->state == FHSS_STATE_TERMINATING) {
        if (len >= SYNC_PAYLOAD_BYTES && pkt_type == FHSS_PKT_HOP_SYNC) {
          uint32_t tx_time = FHSS_GetSyncTime(handle, buf);
          handle->absolute_hop_count = FHSS_GetSyncHopCount(handle, buf);

          __HAL_TIM_SET_COUNTER(handle->htim, tx_time + delta_us +
                                                  HOP_FLIGHT_TIME +
                                                  RX_SYNC_LEAD_OFFSET);
          handle->sync_received = true;
        } else if (pkt_type == FHSS_PKT_DATA) {

          FHSS_TRACE(handle, FHSS_EVT_DEBUG, RB_Free(&handle->rx_rb));
          uint8_t current_seq = buf[1];

          uint8_t diff = current_seq - handle->rx_seq;

          if (diff > 0 && diff < 250) {
            RB_Write(&handle->rx_rb, &buf[2], len - 2);
            handle->rx_seq = current_seq;
          }
        } else if (pkt_type == FHSS_PKT_TERM) {
          FHSS_Terminate(handle, FHSS_DISCONNECT_PEER_CLOSED);
        }
      }
    }

    FHSS_UpdateRxBackpressure(handle);
  }
}

void FHSS_TimerIRQCallback(FHSS_HandleTypeDef *handle) {
  FHSS_TRACE(handle, FHSS_EVT_TIMER, 0);

  switch (handle->state) {
  case FHSS_STATE_TX_HOPPING:
    handle->absolute_hop_count++;

    uint8_t inflight_cnt = RB_Available(&handle->in_flight_rb);
    uint8_t data_packets_flushed = 0;
    for (size_t i = 0; i < inflight_cnt; i++) {
      uint8_t val;
      RB_PeekOffset(&handle->in_flight_rb, i, &val, 1);
      if (val > 0) {
        data_packets_flushed++;
      }
    }
    handle->tx_seq -= data_packets_flushed;

    NRF24L01_StopBulkTransmit(handle->hnrf);
    NRF24L01_FlushTx(handle->hnrf);
    FHSS_SetChannel(handle, FHSS_GetHoppingChannel(handle));
    NRF24L01_StartBulkTransmit(handle->hnrf);

    RB_Reset(&handle->in_flight_rb);
    handle->in_flight_sum = 0;

    handle->sync_sent = false;
    handle->tx_failures = 0;

    if (!handle->hop_acked) {
      handle->failed_channels++;
      FHSS_TRACE(handle, FHSS_EVT_TX_NO_ACK, handle->failed_channels);
    } else {
      handle->failed_channels = 0;
    }

    handle->hop_acked = false;

    if (handle->failed_channels >= handle->config.max_failed_channels) {
      FHSS_Terminate(handle, FHSS_DISCONNECT_TIMEOUT);
      return;
    }
    break;

  case FHSS_STATE_RX_HOPPING:
  case FHSS_STATE_TERMINATING:
    handle->absolute_hop_count++;

    FHSS_SetRxChannel(handle, FHSS_GetHoppingChannel(handle));

    FHSS_UpdateRxBackpressure(handle);

    if (!handle->sync_received) {
      handle->failed_channels++;
      FHSS_TRACE(handle, FHSS_EVT_RX_MISSED_SYNC, handle->failed_channels);
    } else
      handle->failed_channels = 0;

    if (handle->failed_channels >= handle->config.max_failed_channels) {
      FHSS_Terminate(handle, FHSS_DISCONNECT_TIMEOUT);
      return;
    }

    handle->sync_received = false;
    break;

  case FHSS_STATE_TX_ADVERTISING:
    handle->advertising_channel_index = 0;

    FHSS_SetChannel(handle, FHSS_GetAdvertisingChannel(handle));

    uint8_t buffer[SYNC_PAYLOAD_BYTES];
    FHSS_BuildSyncPayload(handle, FHSS_PKT_ADV_SYNC, buffer);
    NRF24L01_Transmit(handle->hnrf, buffer, SYNC_PAYLOAD_BYTES, false);
    FHSS_TRACE(handle, FHSS_EVT_PKT_SENT, FHSS_PKT_ADV_SYNC);
    break;

  case FHSS_STATE_RX_SCANNING:
    handle->advertising_channel_index++;
    if (handle->advertising_channel_index >=
        handle->config.discovery_channels_len) {
      handle->advertising_channel_index = 0;
    }

    FHSS_SetRxChannel(handle, FHSS_GetAdvertisingChannel(handle));
    break;
  case FHSS_STATE_IDLE:
  default:
    FHSS_Shutdown(handle);
    break;
  }
}

uint16_t FHSS_Transmit(FHSS_HandleTypeDef *handle, const uint8_t *data,
                       uint16_t len) {
  if (handle == NULL || data == NULL || len == 0)
    return 0;

  uint32_t primask_bit = __get_PRIMASK();
  __disable_irq();

  uint16_t free_space = RB_Free(&handle->tx_rb);

  if (free_space < len) {
    __set_PRIMASK(primask_bit);
    return 0;
  }

  size_t bytes_written = RB_Write(&handle->tx_rb, data, len);

  __set_PRIMASK(primask_bit);

#ifdef FHSS_ENABLE_STATS
  handle->stats.total_bytes_transmitted += bytes_written;
#endif

  return bytes_written;
}

uint16_t FHSS_Free(FHSS_HandleTypeDef *handle) {
  if (handle == NULL)
    return 0;

  uint32_t primask_bit = __get_PRIMASK();
  __disable_irq();
  size_t bytes_free = RB_Free(&handle->tx_rb);
  __set_PRIMASK(primask_bit);

  return bytes_free;
}

uint16_t FHSS_Available(FHSS_HandleTypeDef *handle) {
  if (handle == NULL)
    return 0;

  uint32_t primask_bit = __get_PRIMASK();
  __disable_irq();
  size_t bytes_available = RB_Available(&handle->rx_rb);
  __set_PRIMASK(primask_bit);

  return bytes_available;
}

uint16_t FHSS_Receive(FHSS_HandleTypeDef *handle, uint8_t *buffer,
                      uint16_t max_len) {
  if (handle == NULL || buffer == NULL || max_len == 0)
    return 0;

  uint32_t primask_bit = __get_PRIMASK();
  __disable_irq();
  size_t bytes_read = RB_Read(&handle->rx_rb, buffer, max_len);
  __set_PRIMASK(primask_bit);

  FHSS_UpdateRxBackpressure(handle);

  return bytes_read;
}

__weak void FHSS_OnDisconnectedEvent(FHSS_HandleTypeDef *handle,
                                     FHSS_DisconnectReasonTypeDef reason) {}

void FHSS_Terminate(FHSS_HandleTypeDef *handle,
                    FHSS_DisconnectReasonTypeDef reason) {
  if (handle == NULL || handle->state == FHSS_STATE_IDLE)
    return;

#ifdef FHSS_ENABLE_STATS
  handle->stats.end_time_ms = HAL_GetTick();
#endif

  uint32_t primask_bit = __get_PRIMASK();
  __disable_irq();

  if (handle->state == FHSS_STATE_RX_HOPPING ||
      handle->state == FHSS_STATE_RX_SCANNING) {
    NRF24L01_StopListening(handle->hnrf);
  } else {
    NRF24L01_StopBulkTransmit(handle->hnrf);
  }

  FHSS_Reset(handle);
  FHSS_Shutdown(handle);

  __set_PRIMASK(primask_bit);

  FHSS_OnDisconnectedEvent(handle, reason);
}

void FHSS_Disconnect(FHSS_HandleTypeDef *handle) {
  if (handle == NULL || handle->state == FHSS_STATE_IDLE)
    return;

  if (handle->state == FHSS_STATE_TX_HOPPING) {
    FHSS_SetState(handle, FHSS_STATE_TERMINATING);
    HAL_TIM_Base_Stop_IT(handle->htim);

    NRF24L01_StopBulkTransmit(handle->hnrf);
    NRF24L01_FlushTx(handle->hnrf);

    uint8_t pkt[1] = {FHSS_PKT_TERM};

    NRF24L01_Transmit(handle->hnrf, pkt, 1, false);

    uint32_t start = HAL_GetTick();
    while (!(NRF24L01_GetIRQFlags(handle->hnrf) &
             (NRF24L01_STATUS_TX_DS | NRF24L01_STATUS_MAX_RT))) {
      if (HAL_GetTick() - start > 5)
        break;
    }
  } else if (handle->state == FHSS_STATE_RX_HOPPING) {
    FHSS_SetState(handle, FHSS_STATE_TERMINATING);
    NRF24L01_FlushTx(handle->hnrf);
    uint8_t ack_payload[1] = {FHSS_ACK_TERM};
    NRF24L01_WriteAckPayload(handle->hnrf, handle->active_rx_pipe, ack_payload,
                             1);

    uint32_t start = HAL_GetTick();
    uint32_t timeout_ms = (handle->config.dwell_time_us / 1000) + 10;

    while (!NRF24L01_IsTXEmpty(handle->hnrf)) {
      if (HAL_GetTick() - start > timeout_ms)
        break;
    }
  }

  FHSS_Terminate(handle, FHSS_DISCONNECT_LOCAL_CLOSED);
}