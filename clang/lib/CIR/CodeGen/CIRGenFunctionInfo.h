//==-- CIRGenFunctionInfo.h - Representation of fn argument/return types ---==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines CIRGenFunctionInfo and associated types used in representing the
// CIR source types and ABI-coerced types for function arguments and
// return values.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CIR_CIRGENFUNCTIONINFO_H
#define LLVM_CLANG_CIR_CIRGENFUNCTIONINFO_H

#include "llvm/ADT/FoldingSet.h"

namespace clang::CIRGen {

class CIRGenFunctionInfo final : public llvm::FoldingSetNode {
public:
  static CIRGenFunctionInfo *create();

  static void profile(llvm::FoldingSetNodeID &id) {
    // Currently we don't have anything to add to the node.
  }
};

} // namespace clang::CIRGen

#endif
