/* -*- mode: C; c-basic-offset: 4; intent-tabs-mode: nil -*-
 *
 * Thundercracker firmware
 *
 * Copyright <c> 2012 Sifteo, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef RING_BUFFER_H_
#define RING_BUFFER_H_

#include "stm32/common.h"


/**
 * Bounded single-producer/single-consumer ring with release/acquire publication.
 * Multiple producers and reset require an external interrupt guard.
 */
template <unsigned tSize, typename tItemType = uint8_t, typename tIndexType = uint16_t>
class RingBuffer
{
    tIndexType mHead;      /// Index of the next item to read
    tIndexType mTail;      /// Index of the next empty slot to write into
    tItemType mBuf[tSize];

public:
    static_assert(__atomic_always_lock_free(sizeof(tIndexType), 0), "ring indices must be lock free");
    static_assert(tSize >= 2 && (tSize & (tSize - 1)) == 0, "power of two ring size");
    static_assert(tSize - 1 <= tIndexType(-1), "ring index width");

    RingBuffer() :
        mHead(0),
        mTail(0)
    {}

    void ALWAYS_INLINE init() {
        // must be power of 2
        STATIC_ASSERT((tSize & (tSize - 1)) == 0);
        __atomic_store_n(&mHead, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&mTail, 0, __ATOMIC_RELAXED);
    }

    unsigned ALWAYS_INLINE capacity() const {
        return tSize - 1;
    }

    bool ALWAYS_INLINE enqueue(tItemType c)
    {
        if (full()) return false;
        unsigned tail = __atomic_load_n(&mTail, __ATOMIC_RELAXED);
        mBuf[tail] = c;
        __atomic_store_n(&mTail, capacity() & (tail + 1), __ATOMIC_RELEASE);
        return true;
    }

    tItemType ALWAYS_INLINE dequeue()
    {
        if (empty()) return tItemType();
        unsigned head = __atomic_load_n(&mHead, __ATOMIC_RELAXED);
        tItemType c = mBuf[head];
        __atomic_store_n(&mHead, capacity() & (head + 1), __ATOMIC_RELEASE);
        return c;
    }

    /*
     * DMA support
     */

    void fill(tItemType c)
    {
        for (tIndexType t = 0; t < tSize; ++t)
            mBuf[t] = c;
    }

    unsigned ALWAYS_INLINE getDMACount() const {
        return tSize;
    }

    uintptr_t ALWAYS_INLINE getDMABuffer() const {
        return (uintptr_t) &mBuf[0];
    }

    void ALWAYS_INLINE dequeueWithDMACount(unsigned count) {
        // Update the 'head' pointer, given an updated DMA count.
        ASSERT(count < getDMACount());
        __atomic_store_n(&mHead, capacity() & (tSize - count), __ATOMIC_RELEASE);
    }

    /*
     * Copy from 'src' to 'this' until the source is empty or destination
     * has >= fillThreshold items in the queue.
     */
    template <typename T>
    void ALWAYS_INLINE pull(T &src, unsigned fillThreshold = tSize - 1)
    {
        ASSERT(fillThreshold <= capacity());
        unsigned rCount = src.readAvailable();
        unsigned selfReadAvailable = readAvailable();
        if (selfReadAvailable < fillThreshold) {
            unsigned wCount = fillThreshold - selfReadAvailable;
            unsigned count = MIN(rCount, wCount);
            while (count--) {
                enqueue(src.dequeue());
            }
        }
    }

    bool ALWAYS_INLINE full() const {
        return (capacity() & (__atomic_load_n(&mTail, __ATOMIC_RELAXED) + 1)) ==
            __atomic_load_n(&mHead, __ATOMIC_ACQUIRE);
    }

    bool ALWAYS_INLINE empty() const {
        return __atomic_load_n(&mHead, __ATOMIC_RELAXED) ==
            __atomic_load_n(&mTail, __ATOMIC_ACQUIRE);
    }

    unsigned ALWAYS_INLINE readAvailable() const {
        unsigned head = __atomic_load_n(&mHead, __ATOMIC_ACQUIRE);
        unsigned tail = __atomic_load_n(&mTail, __ATOMIC_ACQUIRE);
        return (tail - head) & capacity();
    }

    unsigned ALWAYS_INLINE writeAvailable() const {
        return capacity() - readAvailable();
    }
};

#endif // RING_BUFFER_H_
