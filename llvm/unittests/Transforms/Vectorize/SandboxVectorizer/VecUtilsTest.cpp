//===- VecUtilsTest.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Vectorize/SandboxVectorizer/VecUtils.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/SandboxIR/Context.h"
#include "llvm/SandboxIR/Function.h"
#include "llvm/SandboxIR/Type.h"
#include "llvm/Support/SourceMgr.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace llvm;

struct VecUtilsTest : public testing::Test {
  LLVMContext C;
  std::unique_ptr<Module> M;
  std::unique_ptr<AssumptionCache> AC;
  std::unique_ptr<TargetLibraryInfoImpl> TLII;
  std::unique_ptr<TargetLibraryInfo> TLI;
  std::unique_ptr<DominatorTree> DT;
  std::unique_ptr<LoopInfo> LI;
  std::unique_ptr<ScalarEvolution> SE;
  void parseIR(const char *IR) {
    SMDiagnostic Err;
    M = parseAssemblyString(IR, Err, C);
    if (!M) {
      Err.print("VecUtilsTest", errs());
      return;
    }

    TLII = std::make_unique<TargetLibraryInfoImpl>(M->getTargetTriple());
    TLI = std::make_unique<TargetLibraryInfo>(*TLII);
  }

  ScalarEvolution &getSE(llvm::Function &LLVMF) {
    AC = std::make_unique<AssumptionCache>(LLVMF);
    DT = std::make_unique<DominatorTree>(LLVMF);
    LI = std::make_unique<LoopInfo>(*DT);
    SE = std::make_unique<ScalarEvolution>(LLVMF, *TLI, *AC, *DT, *LI);
    return *SE;
  }
};

sandboxir::BasicBlock &getBasicBlockByName(sandboxir::Function &F,
                                           StringRef Name) {
  for (sandboxir::BasicBlock &BB : F)
    if (BB.getName() == Name)
      return BB;
  llvm_unreachable("Expected to find basic block!");
}

TEST_F(VecUtilsTest, GetNumElements) {
  sandboxir::Context Ctx(C);
  auto *ElemTy = sandboxir::Type::getInt32Ty(Ctx);
  EXPECT_EQ(sandboxir::VecUtils::getNumElements(ElemTy), 1);
  auto *VTy = sandboxir::FixedVectorType::get(ElemTy, 2);
  EXPECT_EQ(sandboxir::VecUtils::getNumElements(VTy), 2);
  auto *VTy1 = sandboxir::FixedVectorType::get(ElemTy, 1);
  EXPECT_EQ(sandboxir::VecUtils::getNumElements(VTy1), 1);
}

TEST_F(VecUtilsTest, GetElementType) {
  sandboxir::Context Ctx(C);
  auto *ElemTy = sandboxir::Type::getInt32Ty(Ctx);
  EXPECT_EQ(sandboxir::VecUtils::getElementType(ElemTy), ElemTy);
  auto *VTy = sandboxir::FixedVectorType::get(ElemTy, 2);
  EXPECT_EQ(sandboxir::VecUtils::getElementType(VTy), ElemTy);
}

TEST_F(VecUtilsTest, AreConsecutive_gep_float) {
  parseIR(R"IR(
define void @foo(ptr %ptr) {
  %gep0 = getelementptr inbounds float, ptr %ptr, i64 0
  %gep1 = getelementptr inbounds float, ptr %ptr, i64 1
  %gep2 = getelementptr inbounds float, ptr %ptr, i64 2
  %gep3 = getelementptr inbounds float, ptr %ptr, i64 3

  %ld0 = load float, ptr %gep0
  %ld1 = load float, ptr %gep1
  %ld2 = load float, ptr %gep2
  %ld3 = load float, ptr %gep3

  %v2ld0 = load <2 x float>, ptr %gep0
  %v2ld1 = load <2 x float>, ptr %gep1
  %v2ld2 = load <2 x float>, ptr %gep2
  %v2ld3 = load <2 x float>, ptr %gep3

  %v3ld0 = load <3 x float>, ptr %gep0
  %v3ld1 = load <3 x float>, ptr %gep1
  %v3ld2 = load <3 x float>, ptr %gep2
  %v3ld3 = load <3 x float>, ptr %gep3
  ret void
}
)IR");
  Function &LLVMF = *M->getFunction("foo");
  const DataLayout &DL = M->getDataLayout();
  auto &SE = getSE(LLVMF);

  sandboxir::Context Ctx(C);
  auto &F = *Ctx.createFunction(&LLVMF);

  auto &BB = *F.begin();
  auto It = std::next(BB.begin(), 4);
  auto *L0 = cast<sandboxir::LoadInst>(&*It++);
  auto *L1 = cast<sandboxir::LoadInst>(&*It++);
  auto *L2 = cast<sandboxir::LoadInst>(&*It++);
  auto *L3 = cast<sandboxir::LoadInst>(&*It++);

  auto *V2L0 = cast<sandboxir::LoadInst>(&*It++);
  auto *V2L1 = cast<sandboxir::LoadInst>(&*It++);
  auto *V2L2 = cast<sandboxir::LoadInst>(&*It++);
  auto *V2L3 = cast<sandboxir::LoadInst>(&*It++);

  auto *V3L0 = cast<sandboxir::LoadInst>(&*It++);
  auto *V3L1 = cast<sandboxir::LoadInst>(&*It++);
  auto *V3L2 = cast<sandboxir::LoadInst>(&*It++);
  auto *V3L3 = cast<sandboxir::LoadInst>(&*It++);

  // Scalar
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(L0, L1, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(L1, L2, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(L2, L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L1, L0, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L2, L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L3, L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L0, L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L0, L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L1, L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L2, L0, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L3, L1, SE, DL));

  // Check 2-wide loads
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(V2L0, V2L2, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(V2L1, V2L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L0, V2L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L1, V2L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L2, V2L3, SE, DL));

  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L3, V2L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L3, V2L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L3, V2L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L3, V2L1, SE, DL));

  // Check 3-wide loads
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(V3L0, V3L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L0, V3L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L1, V3L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L2, V3L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L1, V3L0, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L2, V3L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L3, V3L2, SE, DL));

  // Check mixes of vectors and scalar
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(L0, V2L1, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(L1, V2L2, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(V2L0, L2, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(V3L0, L3, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(V2L0, V3L2, SE, DL));

  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L0, V2L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L0, V3L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L0, V2L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L0, V3L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L0, L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L0, L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L0, V2L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L0, V2L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L1, L0, SE, DL));
}

TEST_F(VecUtilsTest, AreConsecutive_gep_i8) {
  parseIR(R"IR(
define void @foo(ptr %ptr) {
  %gep0 = getelementptr inbounds i8, ptr %ptr, i64 0
  %gep1 = getelementptr inbounds i8, ptr %ptr, i64 4
  %gep2 = getelementptr inbounds i8, ptr %ptr, i64 8
  %gep3 = getelementptr inbounds i8, ptr %ptr, i64 12

  %ld0 = load float, ptr %gep0
  %ld1 = load float, ptr %gep1
  %ld2 = load float, ptr %gep2
  %ld3 = load float, ptr %gep3

  %v2ld0 = load <2 x float>, ptr %gep0
  %v2ld1 = load <2 x float>, ptr %gep1
  %v2ld2 = load <2 x float>, ptr %gep2
  %v2ld3 = load <2 x float>, ptr %gep3

  %v3ld0 = load <3 x float>, ptr %gep0
  %v3ld1 = load <3 x float>, ptr %gep1
  %v3ld2 = load <3 x float>, ptr %gep2
  %v3ld3 = load <3 x float>, ptr %gep3
  ret void
}
)IR");
  Function &LLVMF = *M->getFunction("foo");
  const DataLayout &DL = M->getDataLayout();
  auto &SE = getSE(LLVMF);

  sandboxir::Context Ctx(C);
  auto &F = *Ctx.createFunction(&LLVMF);
  auto &BB = *F.begin();
  auto It = std::next(BB.begin(), 4);
  auto *L0 = cast<sandboxir::LoadInst>(&*It++);
  auto *L1 = cast<sandboxir::LoadInst>(&*It++);
  auto *L2 = cast<sandboxir::LoadInst>(&*It++);
  auto *L3 = cast<sandboxir::LoadInst>(&*It++);

  auto *V2L0 = cast<sandboxir::LoadInst>(&*It++);
  auto *V2L1 = cast<sandboxir::LoadInst>(&*It++);
  auto *V2L2 = cast<sandboxir::LoadInst>(&*It++);
  auto *V2L3 = cast<sandboxir::LoadInst>(&*It++);

  auto *V3L0 = cast<sandboxir::LoadInst>(&*It++);
  auto *V3L1 = cast<sandboxir::LoadInst>(&*It++);
  auto *V3L2 = cast<sandboxir::LoadInst>(&*It++);
  auto *V3L3 = cast<sandboxir::LoadInst>(&*It++);

  // Scalar
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(L0, L1, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(L1, L2, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(L2, L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L1, L0, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L2, L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L3, L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L0, L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L0, L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L1, L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L2, L0, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L3, L1, SE, DL));

  // Check 2-wide loads
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(V2L0, V2L2, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(V2L1, V2L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L0, V2L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L1, V2L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L2, V2L3, SE, DL));

  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L3, V2L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L3, V2L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L3, V2L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L3, V2L1, SE, DL));

  // Check 3-wide loads
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(V3L0, V3L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L0, V3L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L1, V3L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L2, V3L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L1, V3L0, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L2, V3L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L3, V3L2, SE, DL));

  // Check mixes of vectors and scalar
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(L0, V2L1, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(L1, V2L2, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(V2L0, L2, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(V3L0, L3, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(V2L0, V3L2, SE, DL));

  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L0, V2L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L0, V3L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L0, V2L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L0, V3L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L0, L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L0, L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L0, V2L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L0, V2L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L1, L0, SE, DL));
}

TEST_F(VecUtilsTest, AreConsecutive_gep_i1) {
  parseIR(R"IR(
define void @foo(ptr %ptr) {
  %gep0 = getelementptr inbounds i1, ptr %ptr, i64 0
  %gep1 = getelementptr inbounds i2, ptr %ptr, i64 4
  %gep2 = getelementptr inbounds i3, ptr %ptr, i64 8
  %gep3 = getelementptr inbounds i7, ptr %ptr, i64 12

  %ld0 = load float, ptr %gep0
  %ld1 = load float, ptr %gep1
  %ld2 = load float, ptr %gep2
  %ld3 = load float, ptr %gep3

  %v2ld0 = load <2 x float>, ptr %gep0
  %v2ld1 = load <2 x float>, ptr %gep1
  %v2ld2 = load <2 x float>, ptr %gep2
  %v2ld3 = load <2 x float>, ptr %gep3

  %v3ld0 = load <3 x float>, ptr %gep0
  %v3ld1 = load <3 x float>, ptr %gep1
  %v3ld2 = load <3 x float>, ptr %gep2
  %v3ld3 = load <3 x float>, ptr %gep3
  ret void
}
)IR");
  Function &LLVMF = *M->getFunction("foo");
  const DataLayout &DL = M->getDataLayout();
  auto &SE = getSE(LLVMF);

  sandboxir::Context Ctx(C);
  auto &F = *Ctx.createFunction(&LLVMF);
  auto &BB = *F.begin();
  auto It = std::next(BB.begin(), 4);
  auto *L0 = cast<sandboxir::LoadInst>(&*It++);
  auto *L1 = cast<sandboxir::LoadInst>(&*It++);
  auto *L2 = cast<sandboxir::LoadInst>(&*It++);
  auto *L3 = cast<sandboxir::LoadInst>(&*It++);

  auto *V2L0 = cast<sandboxir::LoadInst>(&*It++);
  auto *V2L1 = cast<sandboxir::LoadInst>(&*It++);
  auto *V2L2 = cast<sandboxir::LoadInst>(&*It++);
  auto *V2L3 = cast<sandboxir::LoadInst>(&*It++);

  auto *V3L0 = cast<sandboxir::LoadInst>(&*It++);
  auto *V3L1 = cast<sandboxir::LoadInst>(&*It++);
  auto *V3L2 = cast<sandboxir::LoadInst>(&*It++);
  auto *V3L3 = cast<sandboxir::LoadInst>(&*It++);

  // Scalar
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(L0, L1, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(L1, L2, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(L2, L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L1, L0, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L2, L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L3, L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L0, L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L0, L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L1, L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L2, L0, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L3, L1, SE, DL));

  // Check 2-wide loads
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(V2L0, V2L2, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(V2L1, V2L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L0, V2L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L1, V2L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L2, V2L3, SE, DL));

  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L3, V2L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L3, V2L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L3, V2L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L3, V2L1, SE, DL));

  // Check 3-wide loads
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(V3L0, V3L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L0, V3L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L1, V3L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L2, V3L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L1, V3L0, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L2, V3L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L3, V3L2, SE, DL));

  // Check mixes of vectors and scalar
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(L0, V2L1, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(L1, V2L2, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(V2L0, L2, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(V3L0, L3, SE, DL));
  EXPECT_TRUE(sandboxir::VecUtils::areConsecutive(V2L0, V3L2, SE, DL));

  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L0, V2L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L0, V3L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(L0, V2L3, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L0, V3L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L0, L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L0, L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L0, V2L1, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V3L0, V2L2, SE, DL));
  EXPECT_FALSE(sandboxir::VecUtils::areConsecutive(V2L1, L0, SE, DL));
}

TEST_F(VecUtilsTest, GetNumLanes) {
  parseIR(R"IR(
define <4 x float> @foo(float %v, <2 x float> %v2, <4 x float> %ret, ptr %ptr) {
  store float %v, ptr %ptr
  store <2 x float> %v2, ptr %ptr
  ret <4 x float> %ret
}
)IR");
  Function &LLVMF = *M->getFunction("foo");

  sandboxir::Context Ctx(C);
  auto &F = *Ctx.createFunction(&LLVMF);
  auto &BB = *F.begin();

  auto It = BB.begin();
  auto *S0 = cast<sandboxir::StoreInst>(&*It++);
  auto *S1 = cast<sandboxir::StoreInst>(&*It++);
  auto *Ret = cast<sandboxir::ReturnInst>(&*It++);
  EXPECT_EQ(sandboxir::VecUtils::getNumLanes(S0->getValueOperand()->getType()),
            1u);
  EXPECT_EQ(sandboxir::VecUtils::getNumLanes(S0), 1u);
  EXPECT_EQ(sandboxir::VecUtils::getNumLanes(S1->getValueOperand()->getType()),
            2u);
  EXPECT_EQ(sandboxir::VecUtils::getNumLanes(S1), 2u);
  EXPECT_EQ(sandboxir::VecUtils::getNumLanes(Ret->getReturnValue()->getType()),
            4u);
  EXPECT_EQ(sandboxir::VecUtils::getNumLanes(Ret), 4u);

  SmallVector<sandboxir::Value *> Bndl({S0, S1, Ret});
  EXPECT_EQ(sandboxir::VecUtils::getNumLanes(Bndl), 7u);
}

TEST_F(VecUtilsTest, GetWideType) {
  sandboxir::Context Ctx(C);

  auto *Int32Ty = sandboxir::Type::getInt32Ty(Ctx);
  auto *Int32X4Ty = sandboxir::FixedVectorType::get(Int32Ty, 4);
  EXPECT_EQ(sandboxir::VecUtils::getWideType(Int32Ty, 4), Int32X4Ty);
  auto *Int32X8Ty = sandboxir::FixedVectorType::get(Int32Ty, 8);
  EXPECT_EQ(sandboxir::VecUtils::getWideType(Int32X4Ty, 2), Int32X8Ty);
}

TEST_F(VecUtilsTest, GetLowest) {
  parseIR(R"IR(
define void @foo(i8 %v) {
bb0:
  br label %bb1
bb1:
  %A = add i8 %v, 1
  %B = add i8 %v, 2
  %C = add i8 %v, 3
  ret void
}
)IR");
  Function &LLVMF = *M->getFunction("foo");

  sandboxir::Context Ctx(C);
  auto &F = *Ctx.createFunction(&LLVMF);
  auto &BB0 = getBasicBlockByName(F, "bb0");
  auto It = BB0.begin();
  auto *BB0I = cast<sandboxir::BranchInst>(&*It++);

  auto &BB = getBasicBlockByName(F, "bb1");
  It = BB.begin();
  auto *IA = cast<sandboxir::Instruction>(&*It++);
  auto *C1 = cast<sandboxir::Constant>(IA->getOperand(1));
  auto *IB = cast<sandboxir::Instruction>(&*It++);
  auto *C2 = cast<sandboxir::Constant>(IB->getOperand(1));
  auto *IC = cast<sandboxir::Instruction>(&*It++);
  auto *C3 = cast<sandboxir::Constant>(IC->getOperand(1));
  // Check getLowest(ArrayRef<Instruction *>)
  SmallVector<sandboxir::Instruction *> A({IA});
  EXPECT_EQ(sandboxir::VecUtils::getLowest(A), IA);
  SmallVector<sandboxir::Instruction *> ABC({IA, IB, IC});
  EXPECT_EQ(sandboxir::VecUtils::getLowest(ABC), IC);
  SmallVector<sandboxir::Instruction *> ACB({IA, IC, IB});
  EXPECT_EQ(sandboxir::VecUtils::getLowest(ACB), IC);
  SmallVector<sandboxir::Instruction *> CAB({IC, IA, IB});
  EXPECT_EQ(sandboxir::VecUtils::getLowest(CAB), IC);
  SmallVector<sandboxir::Instruction *> CBA({IC, IB, IA});
  EXPECT_EQ(sandboxir::VecUtils::getLowest(CBA), IC);

  // Check getLowest(ArrayRef<Value *>)
  SmallVector<sandboxir::Value *> C1Only({C1});
  EXPECT_EQ(sandboxir::VecUtils::getLowest(C1Only, &BB), nullptr);
  EXPECT_EQ(sandboxir::VecUtils::getLowest(C1Only, &BB0), nullptr);
  SmallVector<sandboxir::Value *> AOnly({IA});
  EXPECT_EQ(sandboxir::VecUtils::getLowest(AOnly, &BB), IA);
  EXPECT_EQ(sandboxir::VecUtils::getLowest(AOnly, &BB0), nullptr);
  SmallVector<sandboxir::Value *> AC1({IA, C1});
  EXPECT_EQ(sandboxir::VecUtils::getLowest(AC1, &BB), IA);
  EXPECT_EQ(sandboxir::VecUtils::getLowest(AC1, &BB0), nullptr);
  SmallVector<sandboxir::Value *> C1A({C1, IA});
  EXPECT_EQ(sandboxir::VecUtils::getLowest(C1A, &BB), IA);
  EXPECT_EQ(sandboxir::VecUtils::getLowest(C1A, &BB0), nullptr);
  SmallVector<sandboxir::Value *> AC1B({IA, C1, IB});
  EXPECT_EQ(sandboxir::VecUtils::getLowest(AC1B, &BB), IB);
  EXPECT_EQ(sandboxir::VecUtils::getLowest(AC1B, &BB0), nullptr);
  SmallVector<sandboxir::Value *> ABC1({IA, IB, C1});
  EXPECT_EQ(sandboxir::VecUtils::getLowest(ABC1, &BB), IB);
  EXPECT_EQ(sandboxir::VecUtils::getLowest(ABC1, &BB0), nullptr);
  SmallVector<sandboxir::Value *> AC1C2({IA, C1, C2});
  EXPECT_EQ(sandboxir::VecUtils::getLowest(AC1C2, &BB), IA);
  EXPECT_EQ(sandboxir::VecUtils::getLowest(AC1C2, &BB0), nullptr);
  SmallVector<sandboxir::Value *> C1C2C3({C1, C2, C3});
  EXPECT_EQ(sandboxir::VecUtils::getLowest(C1C2C3, &BB), nullptr);
  EXPECT_EQ(sandboxir::VecUtils::getLowest(C1C2C3, &BB0), nullptr);

  SmallVector<sandboxir::Value *> DiffBBs({BB0I, IA});
  EXPECT_EQ(sandboxir::VecUtils::getLowest(DiffBBs, &BB0), BB0I);
  EXPECT_EQ(sandboxir::VecUtils::getLowest(DiffBBs, &BB), IA);
}

TEST_F(VecUtilsTest, GetLastPHIOrSelf) {
  parseIR(R"IR(
define void @foo(i8 %v) {
entry:
  br label %bb1

bb1:
  %phi1 = phi i8 [0, %entry], [1, %bb1]
  %phi2 = phi i8 [0, %entry], [1, %bb1]
  br label %bb1

bb2:
  ret void
}
)IR");
  Function &LLVMF = *M->getFunction("foo");

  sandboxir::Context Ctx(C);
  auto &F = *Ctx.createFunction(&LLVMF);
  auto &BB = getBasicBlockByName(F, "bb1");
  auto It = BB.begin();
  auto *PHI1 = cast<sandboxir::PHINode>(&*It++);
  auto *PHI2 = cast<sandboxir::PHINode>(&*It++);
  auto *Br = cast<sandboxir::BranchInst>(&*It++);
  EXPECT_EQ(sandboxir::VecUtils::getLastPHIOrSelf(PHI1), PHI2);
  EXPECT_EQ(sandboxir::VecUtils::getLastPHIOrSelf(PHI2), PHI2);
  EXPECT_EQ(sandboxir::VecUtils::getLastPHIOrSelf(Br), Br);
  EXPECT_EQ(sandboxir::VecUtils::getLastPHIOrSelf(nullptr), nullptr);
}

TEST_F(VecUtilsTest, GetCommonScalarType) {
  parseIR(R"IR(
define void @foo(i8 %v, ptr %ptr) {
bb0:
  %add0 = add i8 %v, %v
  store i8 %v, ptr %ptr
  ret void
}
)IR");
  Function &LLVMF = *M->getFunction("foo");

  sandboxir::Context Ctx(C);
  auto &F = *Ctx.createFunction(&LLVMF);
  auto &BB = *F.begin();
  auto It = BB.begin();
  auto *Add0 = cast<sandboxir::BinaryOperator>(&*It++);
  auto *Store = cast<sandboxir::StoreInst>(&*It++);
  auto *Ret = cast<sandboxir::ReturnInst>(&*It++);
  {
    SmallVector<sandboxir::Value *> Vec = {Add0, Store};
    EXPECT_EQ(sandboxir::VecUtils::tryGetCommonScalarType(Vec),
              Add0->getType());
    EXPECT_EQ(sandboxir::VecUtils::getCommonScalarType(Vec), Add0->getType());
  }
  {
    SmallVector<sandboxir::Value *> Vec = {Add0, Ret};
    EXPECT_EQ(sandboxir::VecUtils::tryGetCommonScalarType(Vec), nullptr);
#ifndef NDEBUG
    EXPECT_DEATH(sandboxir::VecUtils::getCommonScalarType(Vec), ".*common.*");
#endif // NDEBUG
  }
}

TEST_F(VecUtilsTest, FloorPowerOf2) {
  EXPECT_EQ(sandboxir::VecUtils::getFloorPowerOf2(0), 0u);
  EXPECT_EQ(sandboxir::VecUtils::getFloorPowerOf2(1 << 0), 1u << 0);
  EXPECT_EQ(sandboxir::VecUtils::getFloorPowerOf2(3), 2u);
  EXPECT_EQ(sandboxir::VecUtils::getFloorPowerOf2(4), 4u);
  EXPECT_EQ(sandboxir::VecUtils::getFloorPowerOf2(5), 4u);
  EXPECT_EQ(sandboxir::VecUtils::getFloorPowerOf2(7), 4u);
  EXPECT_EQ(sandboxir::VecUtils::getFloorPowerOf2(8), 8u);
  EXPECT_EQ(sandboxir::VecUtils::getFloorPowerOf2(9), 8u);
}

TEST_F(VecUtilsTest, MatchPackScalar) {
  parseIR(R"IR(
define void @foo(i8 %v0, i8 %v1) {
bb0:
  %NotPack = insertelement <2 x i8> poison, i8 %v0, i64 0
  br label %bb1

bb1:
  %Pack0 = insertelement <2 x i8> poison, i8 %v0, i64 0
  %Pack1 = insertelement <2 x i8> %Pack0, i8 %v1, i64 1

  %NotPack0 = insertelement <2 x i8> poison, i8 %v0, i64 0
  %NotPack1 = insertelement <2 x i8> %NotPack0, i8 %v1, i64 0
  %NotPack2 = insertelement <2 x i8> %NotPack1, i8 %v1, i64 1

  %NotPackBB = insertelement <2 x i8> %NotPack, i8 %v1, i64 1

  ret void
}
)IR");
  Function &LLVMF = *M->getFunction("foo");

  sandboxir::Context Ctx(C);
  auto &F = *Ctx.createFunction(&LLVMF);
  auto &BB = getBasicBlockByName(F, "bb1");
  auto It = BB.begin();
  auto *Pack0 = cast<sandboxir::InsertElementInst>(&*It++);
  auto *Pack1 = cast<sandboxir::InsertElementInst>(&*It++);
  auto *NotPack0 = cast<sandboxir::InsertElementInst>(&*It++);
  auto *NotPack1 = cast<sandboxir::InsertElementInst>(&*It++);
  auto *NotPack2 = cast<sandboxir::InsertElementInst>(&*It++);
  auto *NotPackBB = cast<sandboxir::InsertElementInst>(&*It++);
  auto *Ret = cast<sandboxir::ReturnInst>(&*It++);
  auto *Arg0 = F.getArg(0);
  auto *Arg1 = F.getArg(1);
  EXPECT_FALSE(sandboxir::VecUtils::matchPack(Pack0));
  EXPECT_FALSE(sandboxir::VecUtils::matchPack(Ret));
  {
    auto PackOpt = sandboxir::VecUtils::matchPack(Pack1);
    EXPECT_TRUE(PackOpt);
    EXPECT_THAT(PackOpt->Instrs, testing::ElementsAre(Pack1, Pack0));
    EXPECT_THAT(PackOpt->Operands, testing::ElementsAre(Arg0, Arg1));
  }
  {
    for (auto *NotPack : {NotPack0, NotPack1, NotPack2, NotPackBB})
      EXPECT_FALSE(sandboxir::VecUtils::matchPack(NotPack));
  }
}
