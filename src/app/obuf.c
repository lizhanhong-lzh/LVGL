#include "obuf.h"
#include <string.h>

/*
 * obuf - Lock-Free Ring Buffer (SPSC: Single Producer Single Consumer)
 * 
 * Producer (ISR): Modifies "head". Reads "tail".
 * Consumer (App): Modifies "tail". Reads "head".
 * 
 * Strategy: Drop NEW data if full (Standard FIFO). 
 * This prevents the "Overwrite Old" race condition on "tail".
 */

void obuf_init(obuf_t *o, uint8_t *storage, size_t capacity)
{
    o->buf = storage;
    o->capacity = capacity;
    o->head = 0;
    o->tail = 0;
    o->dropped = 0;
}

void obuf_clear(obuf_t *o)
{
    /* Not strictly thread-safe if ISR is active, but works for reset */
    o->head = 0;
    o->tail = 0;
    o->dropped = 0;
}

/* Helper to calculate current length available for reading */
size_t obuf_data_len(const obuf_t *o)
{
    size_t h = o->head;
    size_t t = o->tail;
    if (h >= t) return h - t;
    return h + o->capacity - t;
}

void obuf_write(obuf_t *o, const uint8_t *data, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        size_t next_head = (o->head + 1) % o->capacity;
        
        /* Check if full: (head + 1) == tail */
        if (next_head != o->tail) {
            o->buf[o->head] = data[i];
            /* 
             * Memory Barrier is ideally needed here for weak ordered CPUs.
             * Cortex-M7 (stm32F7) ensures order for Normal-NonCacheable or Device memory.
             * But for safety, we update index AFTER data write.
             */
            o->head = next_head; 
        } else {
            /* Buffer Full - Drop NEW incoming byte */
            /* Do not touch tail, to avoid race with Consumer */
            o->dropped++;
        }
    }
}

size_t obuf_read(obuf_t *o, uint8_t *out, size_t n)
{
    size_t i = 0;
    size_t t = o->tail;
    size_t h = o->head;
    
    /* Calculate available linear segments could be faster, but byte-by-byte is simpler */
    for (i = 0; i < n; i++) {
        if (t == h) { // Empty
            break;
        }
        out[i] = o->buf[t];
        t = (t + 1) % o->capacity;
    }
    
    o->tail = t; /* Update shared tail pointer once */
    return i;
}

/*
 * obuf_peek: Read byte at "index" relative to "tail"
 */
int obuf_peek(const obuf_t *o, size_t index)
{
    size_t len = obuf_data_len(o);
    if (index >= len) {
        return -1;
    }

    /* tail + index wrapped */
    size_t pos = (o->tail + index) % o->capacity;
    return (int)o->buf[pos];
}

void obuf_drop(obuf_t *o, size_t n)
{
    size_t len = obuf_data_len(o);
    if (n > len) {
        n = len; /* Limit to available */
    }
    
    /* Advance tail */
    o->tail = (o->tail + n) % o->capacity;
}

int obuf_find(const obuf_t *o, const uint8_t *pattern, size_t pattern_len)
{
    size_t len = obuf_data_len(o);
    
    if (pattern_len == 0 || pattern_len > len) {
        return -1;
    }

    /* Brute-force search */
    for (size_t i = 0; i <= len - pattern_len; i++) {
        size_t matched = 0;
        for (size_t j = 0; j < pattern_len; j++) {
            // Manual peek
            size_t pos = (o->tail + i + j) % o->capacity;
            if (o->buf[pos] != pattern[j]) {
                break;
            }
            matched++;
        }
        
        if (matched == pattern_len) {
            return (int)i;
        }
    }

    return -1;
}
