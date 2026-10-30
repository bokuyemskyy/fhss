#ifndef RB_H
#define RB_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct
{
  uint8_t *buf;
  size_t size;
  size_t head;
  size_t tail;
} RingBuffer_t;

void RB_Init(RingBuffer_t *rb, uint8_t *buf, size_t size);
void RB_Reset(RingBuffer_t *rb);
int RB_IsEmpty(const RingBuffer_t *rb);
int RB_IsFull(const RingBuffer_t *rb);
size_t RB_Available(const RingBuffer_t *rb);
size_t RB_Free(const RingBuffer_t *rb);
size_t RB_Write(RingBuffer_t *rb, const void *data, size_t len);
size_t RB_Read(RingBuffer_t *rb, void *data, size_t len);
size_t RB_Peek(const RingBuffer_t *rb, void *data, size_t len);
size_t RB_PeekOffset(const RingBuffer_t *rb, size_t offset, void *data, size_t len);
size_t RB_AdvanceTail(RingBuffer_t *rb, size_t len);

#endif