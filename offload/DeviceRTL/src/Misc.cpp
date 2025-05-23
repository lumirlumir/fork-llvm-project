//===--------- Misc.cpp - OpenMP device misc interfaces ----------- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "Allocator.h"
#include "Configuration.h"
#include "DeviceTypes.h"
#include "Shared/RPCOpcodes.h"
#include "shared/rpc.h"

#include "Debug.h"

namespace ompx {
namespace impl {

/// Lookup a device-side function using a host pointer /p HstPtr using the table
/// provided by the device plugin. The table is an ordered pair of host and
/// device pointers sorted on the value of the host pointer.
void *indirectCallLookup(void *HstPtr) {
  if (!HstPtr)
    return nullptr;

  struct IndirectCallTable {
    void *HstPtr;
    void *DevPtr;
  };
  IndirectCallTable *Table =
      reinterpret_cast<IndirectCallTable *>(config::getIndirectCallTablePtr());
  uint64_t TableSize = config::getIndirectCallTableSize();

  // If the table is empty we assume this is device pointer.
  if (!Table || !TableSize)
    return HstPtr;

  uint32_t Left = 0;
  uint32_t Right = TableSize;

  // If the pointer is definitely not contained in the table we exit early.
  if (HstPtr < Table[Left].HstPtr || HstPtr > Table[Right - 1].HstPtr)
    return HstPtr;

  while (Left != Right) {
    uint32_t Current = Left + (Right - Left) / 2;
    if (Table[Current].HstPtr == HstPtr)
      return Table[Current].DevPtr;

    if (HstPtr < Table[Current].HstPtr)
      Right = Current;
    else
      Left = Current;
  }

  // If we searched the whole table and found nothing this is a device pointer.
  return HstPtr;
}

/// The openmp client instance used to communicate with the server.
[[gnu::visibility("protected"),
  gnu::weak]] rpc::Client Client asm("__llvm_rpc_client");

} // namespace impl
} // namespace ompx

/// Interfaces
///
///{

extern "C" {
int32_t __kmpc_cancellationpoint(IdentTy *, int32_t, int32_t) { return 0; }

int32_t __kmpc_cancel(IdentTy *, int32_t, int32_t) { return 0; }

double omp_get_wtick(void) {
  // The number of ticks per second for the AMDGPU clock varies by card and can
  // only be retrieved by querying the driver. We rely on the device environment
  // to inform us what the proper frequency is. NVPTX uses a nanosecond
  // resolution, we could omit the global read but this makes it consistent.
  return 1.0 / ompx::config::getClockFrequency();
}

double omp_get_wtime(void) {
  return static_cast<double>(__builtin_readsteadycounter()) * omp_get_wtick();
}

void *__llvm_omp_indirect_call_lookup(void *HstPtr) {
  return ompx::impl::indirectCallLookup(HstPtr);
}

void *omp_alloc(size_t size, omp_allocator_handle_t allocator) {
  switch (allocator) {
  case omp_default_mem_alloc:
  case omp_large_cap_mem_alloc:
  case omp_const_mem_alloc:
  case omp_high_bw_mem_alloc:
  case omp_low_lat_mem_alloc:
    return malloc(size);
  default:
    return nullptr;
  }
}

void omp_free(void *ptr, omp_allocator_handle_t allocator) {
  switch (allocator) {
  case omp_default_mem_alloc:
  case omp_large_cap_mem_alloc:
  case omp_const_mem_alloc:
  case omp_high_bw_mem_alloc:
  case omp_low_lat_mem_alloc:
    free(ptr);
  case omp_null_allocator:
  default:
    return;
  }
}

unsigned long long __llvm_omp_host_call(void *fn, void *data, size_t size) {
  rpc::Client::Port Port = ompx::impl::Client.open<OFFLOAD_HOST_CALL>();
  Port.send_n(data, size);
  Port.send([=](rpc::Buffer *buffer, uint32_t) {
    buffer->data[0] = reinterpret_cast<uintptr_t>(fn);
  });
  unsigned long long Ret;
  Port.recv([&](rpc::Buffer *Buffer, uint32_t) {
    Ret = static_cast<unsigned long long>(Buffer->data[0]);
  });
  Port.close();
  return Ret;
}
}

///}
