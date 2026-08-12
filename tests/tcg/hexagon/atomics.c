/*
 *  Copyright(c) 2019-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <pthread.h>

int err;

#include "hex_test.h"

static inline int32_t load_locked32(int32_t *x)
{
    int32_t value;

    __asm__ __volatile__("%0 = memw_locked(%1)"
                         : "=r"(value) : "r"(x) : "memory");
    return value;
}

static inline int store_conditional32(int32_t *x, int32_t value)
{
    int result;

    __asm__ __volatile__(
        "memw_locked(%1, p0) = %2\n\t"
        "%0 = p0"
        : "=r"(result) : "r"(x), "r"(value) : "p0", "memory");
    return result != 0;
}

static inline int32_t atomic_inc32(int32_t *x)
{
    int32_t old, dummy;
    __asm__ __volatile__(
        "1: %0 = memw_locked(%2)\n\t"
        "   %1 = add(%0, #1)\n\t"
        "   memw_locked(%2, p0) = %1\n\t"
        "   if (!p0) jump 1b\n\t"
        : "=&r"(old), "=&r"(dummy)
        : "r"(x)
        : "p0", "memory");
    return old;
}

static inline int64_t atomic_inc64(int64_t *x)
{
    int64_t old, dummy;
    __asm__ __volatile__(
        "1: %0 = memd_locked(%2)\n\t"
        "   %1 = #1\n\t"
        "   %1 = add(%0, %1)\n\t"
        "   memd_locked(%2, p0) = %1\n\t"
        "   if (!p0) jump 1b\n\t"
        : "=&r"(old), "=&r"(dummy)
        : "r"(x)
        : "p0", "memory");
    return old;
}

static inline int32_t atomic_dec32(int32_t *x)
{
    int32_t old, dummy;
    __asm__ __volatile__(
        "1: %0 = memw_locked(%2)\n\t"
        "   %1 = add(%0, #-1)\n\t"
        "   memw_locked(%2, p0) = %1\n\t"
        "   if (!p0) jump 1b\n\t"
        : "=&r"(old), "=&r"(dummy)
        : "r"(x)
        : "p0", "memory");
    return old;
}

static inline int64_t atomic_dec64(int64_t *x)
{
    int64_t old, dummy;
    __asm__ __volatile__(
        "1: %0 = memd_locked(%2)\n\t"
        "   %1 = #-1\n\t"
        "   %1 = add(%0, %1)\n\t"
        "   memd_locked(%2, p0) = %1\n\t"
        "   if (!p0) jump 1b\n\t"
        : "=&r"(old), "=&r"(dummy)
        : "r"(x)
        : "p0", "memory");
    return old;
}

#define LOOP_CNT 1000
volatile int32_t tick32 = 1; /* Using volatile because we are testing atomics */
volatile int64_t tick64 = 1; /* Using volatile because we are testing atomics */

/* Volatile keeps the guest-thread handoff visible in this ISA test. */
static volatile int aba_value;
/* Volatile keeps the guest-thread handoff visible in this ISA test. */
static volatile int aba_phase;

static void *aba_thread(void *arg)
{
    while (aba_phase != 1) {
    }
    load_locked32((int32_t *)&aba_value);
    check32(store_conditional32((int32_t *)&aba_value, 2), 1);
    load_locked32((int32_t *)&aba_value);
    check32(store_conditional32((int32_t *)&aba_value, 1), 1);
    aba_phase = 2;
    return NULL;
}

static void test_reservations(void)
{
    int32_t value = 1;

    check32(store_conditional32(&value, 2), 0);

    check32(load_locked32(&value), 1);
    value = 2;
    check32(store_conditional32(&value, 3), 1);
    check32(value, 3);

    check32(load_locked32(&value), 3);
    getpid();
    check32(store_conditional32(&value, 4), 0);

    pthread_t thread;
    aba_value = 1;
    aba_phase = 0;
    pthread_create(&thread, NULL, aba_thread, NULL);
    check32(load_locked32((int32_t *)&aba_value), 1);
    aba_phase = 1;
    while (aba_phase != 2) {
    }
    check32(store_conditional32((int32_t *)&aba_value, 3), 0);
    check32(aba_value, 1);
    pthread_join(thread, NULL);
}

void *thread1_func(void *arg)
{
    for (int i = 0; i < LOOP_CNT; i++) {
        atomic_inc32(&tick32);
        atomic_dec64(&tick64);
    }
    return NULL;
}

void *thread2_func(void *arg)
{
    for (int i = 0; i < LOOP_CNT; i++) {
        atomic_dec32(&tick32);
        atomic_inc64(&tick64);
    }
    return NULL;
}

void test_pthread(void)
{
    pthread_t tid1, tid2;

    pthread_create(&tid1, NULL, thread1_func, "hello1");
    pthread_create(&tid2, NULL, thread2_func, "hello2");
    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);

    check32(tick32, 1);
    check64(tick64, 1);
}

int main(int argc, char **argv)
{
    test_reservations();
    test_pthread();
    puts(err ? "FAIL" : "PASS");
    return err;
}
