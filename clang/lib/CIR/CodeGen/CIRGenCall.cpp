//===--- CIRGenCall.cpp - Encapsulate calling convention details ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These classes wrap the information about a call or function definition used
// to handle ABI compliancy.
//
//===----------------------------------------------------------------------===//

#include "CIRGenCall.h"
#include "CIRGenFunction.h"
#include "clang/CIR/MissingFeatures.h"

using namespace clang;
using namespace clang::CIRGen;

CIRGenFunctionInfo *CIRGenFunctionInfo::create() {
  // For now we just create an empty CIRGenFunctionInfo.
  CIRGenFunctionInfo *fi = new CIRGenFunctionInfo();
  return fi;
}

CIRGenCallee CIRGenCallee::prepareConcreteCallee(CIRGenFunction &cgf) const {
  assert(!cir::MissingFeatures::opCallVirtual());
  return *this;
}

static const CIRGenFunctionInfo &arrangeFreeFunctionLikeCall(CIRGenTypes &cgt) {
  assert(!cir::MissingFeatures::opCallArgs());
  return cgt.arrangeCIRFunctionInfo();
}

const CIRGenFunctionInfo &CIRGenTypes::arrangeFreeFunctionCall() {
  return arrangeFreeFunctionLikeCall(*this);
}

RValue CIRGenFunction::emitCall(const CIRGenFunctionInfo &funcInfo,
                                const CIRGenCallee &callee,
                                cir::CIRCallOpInterface *callOp,
                                mlir::Location loc) {
  assert(!cir::MissingFeatures::opCallArgs());
  assert(!cir::MissingFeatures::emitLifetimeMarkers());

  const CIRGenCallee &concreteCallee = callee.prepareConcreteCallee(*this);
  mlir::Operation *calleePtr = concreteCallee.getFunctionPointer();

  assert(!cir::MissingFeatures::opCallInAlloca());

  mlir::NamedAttrList attrs;
  StringRef funcName;
  if (auto calleeFuncOp = dyn_cast<cir::FuncOp>(calleePtr))
    funcName = calleeFuncOp.getName();

  assert(!cir::MissingFeatures::opCallCallConv());
  assert(!cir::MissingFeatures::opCallSideEffect());

  // lancern: 3 implement CIRGenFunction::emitCall
}
