//===- GraphChangeLog.h - Detailed phase changes to IR ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the interface from the pass manager to the GraphChangeLog
/// which records detailed IR changes by phase for comparing different compiler
/// or option variants
///
/// facebook t13480588
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GRAPHCHANGELOG_H
#define LLVM_IR_GRAPHCHANGELOG_H

#include "llvm/Pass.h"

namespace llvm {
FunctionPass *createGraphChangeLogFinalLegacyPass(raw_ostream &OS);
FunctionPass *createGraphChangeLogLegacyPass(raw_ostream &OS,
                                             const char *PassName);
}
#endif
