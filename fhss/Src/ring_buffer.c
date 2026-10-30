#include "ring_buffer.h"

static uint32_t abs_discard = 0;

void RB_Init(RingBuffer_t *rb, uint8_t *buf, size_t size) {
  rb->buf = buf;
  rb->size = size;
  rb->head = 0;
  rb->tail = 0;
}

void RB_Reset(RingBuffer_t *rb) {
  rb->head = 0;
  rb->tail = 0;
}

static uint32_t RB_Next(const RingBuffer_t *rb, size_t index) {
  index++;

  if (index >= rb->size)
    index = 0;

  return index;
}

int RB_IsEmpty(const RingBuffer_t *rb) { return rb->head == rb->tail; }

int RB_IsFull(const RingBuffer_t *rb) {
  return RB_Next(rb, rb->head) == rb->tail;
}

size_t RB_Available(const RingBuffer_t *rb) {
  if (rb->head >= rb->tail)
    return rb->head - rb->tail;

  return rb->size - rb->tail + rb->head;
}

size_t RB_Free(const RingBuffer_t *rb) {
  return (rb->size - 1u) - RB_Available(rb);
}

size_t RB_Write(RingBuffer_t *rb, const void *data, size_t len) {
  const uint8_t *src = data;

  if (len == 0)
    return 0;

  if (len > RB_Free(rb)) {
    abs_discard += len;
    return 0;
  }

  size_t first = rb->size - rb->head;

  if (first > len)
    first = len;

  memcpy(&rb->buf[rb->head], src, first);

  if (len > first) {
    memcpy(&rb->buf[0], src + first, len - first);
  }

  rb->head += len;

  if (rb->head >= rb->size)
    rb->head -= rb->size;

  return len;
}
size_t RB_AdvanceTail(RingBuffer_t *rb, size_t max_len) {
  size_t available = RB_Available(rb);
  size_t len = (max_len < available) ? max_len : available;

  if (len == 0)
    return 0;

  rb->tail += len;

  if (rb->tail >= rb->size)
    rb->tail -= rb->size;

  return len;
}

size_t RB_Read(RingBuffer_t *rb, void *data, size_t max_len) {
  uint8_t *dst = data;

  size_t available = RB_Available(rb);
  size_t len = (max_len < available) ? max_len : available;

  if (len == 0)
    return 0;

  size_t first = rb->size - rb->tail;

  if (first > len)
    first = len;

  memcpy(dst, &rb->buf[rb->tail], first);

  if (len > first) {
    memcpy(dst + first, &rb->buf[0], len - first);
  }

  rb->tail += len;

  if (rb->tail >= rb->size)
    rb->tail -= rb->size;

  return len;
}

size_t RB_Peek(const RingBuffer_t *rb, void *data, size_t max_len) {
  uint8_t *dst = data;

  size_t available = RB_Available(rb);
  size_t len = (max_len < available) ? max_len : available;

  if (len == 0)
    return 0;

  size_t first = rb->size - rb->tail;

  if (first > len)
    first = len;

  memcpy(dst, &rb->buf[rb->tail], first);

  if (len > first) {
    memcpy(dst + first, &rb->buf[0], len - first);
  }

  return len;
}

size_t RB_PeekOffset(const RingBuffer_t *rb, size_t offset, void *data,
                     size_t max_len) {
  uint8_t *dst = data;
  size_t available = RB_Available(rb);

  if (offset >= available)
    return 0;

  size_t readable = available - offset;
  size_t len = (max_len < readable) ? max_len : readable;

  if (len == 0)
    return 0;

  size_t read_idx = rb->tail + offset;
  if (read_idx >= rb->size)
    read_idx -= rb->size;

  size_t first = rb->size - read_idx;
  if (first > len)
    first = len;

  memcpy(dst, &rb->buf[read_idx], first);

  if (len > first) {
    memcpy(dst + first, &rb->buf[0], len - first);
  }

  return len;
}