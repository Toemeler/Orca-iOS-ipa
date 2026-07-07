/* Minimal stub of <libkern/OSAtomic.h> for the local wx-config probe
   (tools/wx-config-probe.sh). Only what wx/atomic.h references. */
#ifndef _WX_SDK_STUB_OSATOMIC_H_
#define _WX_SDK_STUB_OSATOMIC_H_

#include <stdint.h>

static inline int32_t OSAtomicIncrement32(volatile int32_t *v) { return ++*v; }
static inline int32_t OSAtomicDecrement32(volatile int32_t *v) { return --*v; }
static inline int32_t OSAtomicIncrement32Barrier(volatile int32_t *v) { return ++*v; }
static inline int32_t OSAtomicDecrement32Barrier(volatile int32_t *v) { return --*v; }

#endif
