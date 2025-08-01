//===-- Definition of type jmp_buf ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_TYPES_JMP_BUF_H
#define LLVM_LIBC_TYPES_JMP_BUF_H

#include "__jmp_buf.h"

typedef __jmp_buf jmp_buf[1];

#endif // LLVM_LIBC_TYPES_JMP_BUF_H
