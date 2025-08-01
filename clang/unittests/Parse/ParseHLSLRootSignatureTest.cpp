//=== ParseHLSLRootSignatureTest.cpp - Parse Root Signature tests ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/ModuleLoader.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"

#include "clang/Lex/LexHLSLRootSignature.h"
#include "clang/Parse/ParseHLSLRootSignature.h"
#include "gtest/gtest.h"

using namespace clang;
using namespace clang::hlsl;
using namespace llvm::hlsl::rootsig;

namespace {

using llvm::dxbc::RootSignatureVersion;

// Diagnostic helper for helper tests
class ExpectedDiagConsumer : public DiagnosticConsumer {
  virtual void anchor() {}

  void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
                        const Diagnostic &Info) override {
    if (!FirstDiag || !ExpectedDiagID.has_value()) {
      Satisfied = false;
      return;
    }
    FirstDiag = false;

    Satisfied = ExpectedDiagID.value() == Info.getID();
  }

  bool FirstDiag = true;
  bool Satisfied = false;
  std::optional<unsigned> ExpectedDiagID;

public:
  void setNoDiag() {
    Satisfied = true;
    ExpectedDiagID = std::nullopt;
  }

  void setExpected(unsigned DiagID) {
    Satisfied = false;
    ExpectedDiagID = DiagID;
  }

  bool isSatisfied() { return Satisfied; }
};

// The test fixture.
class ParseHLSLRootSignatureTest : public ::testing::Test {
protected:
  ParseHLSLRootSignatureTest()
      : FileMgr(FileMgrOpts), Consumer(new ExpectedDiagConsumer()),
        Diags(DiagnosticIDs::create(), DiagOpts, Consumer),
        SourceMgr(Diags, FileMgr), TargetOpts(new TargetOptions) {
    // This is an arbitrarily chosen target triple to create the target info.
    TargetOpts->Triple = "dxil";
    Target = TargetInfo::CreateTargetInfo(Diags, *TargetOpts);
  }

  std::unique_ptr<Preprocessor> createPP(StringRef Source,
                                         TrivialModuleLoader &ModLoader) {
    std::unique_ptr<llvm::MemoryBuffer> Buf =
        llvm::MemoryBuffer::getMemBuffer(Source);
    SourceMgr.setMainFileID(SourceMgr.createFileID(std::move(Buf)));

    HeaderSearchOptions SearchOpts;
    HeaderSearch HeaderInfo(SearchOpts, SourceMgr, Diags, LangOpts,
                            Target.get());
    auto PP = std::make_unique<Preprocessor>(
        PPOpts, Diags, LangOpts, SourceMgr, HeaderInfo, ModLoader,
        /*IILookup =*/nullptr, /*OwnsHeaderSearch =*/false);
    PP->Initialize(*Target);
    PP->EnterMainSourceFile();
    return PP;
  }

  std::unique_ptr<ASTContext> createMinimalASTContext() {
    IdentifierTable Idents(LangOpts);
    SelectorTable Selectors;
    Builtin::Context Builtins;

    return std::make_unique<ASTContext>(LangOpts, SourceMgr, Idents, Selectors,
                                        Builtins, TU_Complete);
  }

  StringLiteral *wrapSource(std::unique_ptr<ASTContext> &Ctx,
                            StringRef Source) {
    SourceLocation Locs[1] = {SourceLocation()};
    return StringLiteral::Create(*Ctx, Source, StringLiteralKind::Unevaluated,
                                 false, Ctx->VoidTy, Locs);
  }

  FileSystemOptions FileMgrOpts;
  FileManager FileMgr;
  DiagnosticOptions DiagOpts;
  ExpectedDiagConsumer *Consumer;
  DiagnosticsEngine Diags;
  SourceManager SourceMgr;
  LangOptions LangOpts;
  PreprocessorOptions PPOpts;
  std::shared_ptr<TargetOptions> TargetOpts;
  IntrusiveRefCntPtr<TargetInfo> Target;
};

// Valid Parser Tests

TEST_F(ParseHLSLRootSignatureTest, ValidParseEmptyTest) {
  const llvm::StringLiteral Source = R"cc()cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test no diagnostics produced
  Consumer->setNoDiag();

  ASSERT_FALSE(Parser.parse());
  ASSERT_EQ((int)Parser.getElements().size(), 0);

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, ValidParseDTClausesTest) {
  using llvm::dxbc::DescriptorRangeFlags;
  const llvm::StringLiteral Source = R"cc(
    DescriptorTable(
      CBV(b0),
      SRV(space = 3, offset = 32, t42, flags = 0, numDescriptors = 4),
      visibility = SHADER_VISIBILITY_PIXEL,
      Sampler(s987, space = +2, offset = DESCRIPTOR_RANGE_OFFSET_APPEND),
      UAV(u4294967294, numDescriptors = unbounded,
        flags = Descriptors_Volatile | Data_Volatile
                      | Data_Static_While_Set_At_Execute | Data_Static
                      | Descriptors_Static_Keeping_Buffer_Bounds_Checks
      )
    ),
    DescriptorTable()
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test no diagnostics produced
  Consumer->setNoDiag();

  ASSERT_FALSE(Parser.parse());

  auto Elements = Parser.getElements();
  // First Descriptor Table with 4 elements
  RootElement Elem = Elements[0].getElement();
  ASSERT_TRUE(std::holds_alternative<DescriptorTableClause>(Elem));
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Type, ClauseType::CBuffer);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Reg.ViewType,
            RegisterType::BReg);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Reg.Number, 0u);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).NumDescriptors, 1u);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Space, 0u);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Offset,
            DescriptorTableOffsetAppend);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Flags,
            DescriptorRangeFlags::DataStaticWhileSetAtExecute);

  Elem = Elements[1].getElement();
  ASSERT_TRUE(std::holds_alternative<DescriptorTableClause>(Elem));
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Type, ClauseType::SRV);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Reg.ViewType,
            RegisterType::TReg);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Reg.Number, 42u);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).NumDescriptors, 4u);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Space, 3u);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Offset, 32u);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Flags,
            DescriptorRangeFlags::None);

  Elem = Elements[2].getElement();
  ASSERT_TRUE(std::holds_alternative<DescriptorTableClause>(Elem));
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Type, ClauseType::Sampler);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Reg.ViewType,
            RegisterType::SReg);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Reg.Number, 987u);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).NumDescriptors, 1u);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Space, 2u);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Offset,
            DescriptorTableOffsetAppend);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Flags,
            DescriptorRangeFlags::None);

  Elem = Elements[3].getElement();
  ASSERT_TRUE(std::holds_alternative<DescriptorTableClause>(Elem));
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Type, ClauseType::UAV);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Reg.ViewType,
            RegisterType::UReg);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Reg.Number, 4294967294u);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).NumDescriptors,
            NumDescriptorsUnbounded);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Space, 0u);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Offset,
            DescriptorTableOffsetAppend);
  auto ValidDescriptorRangeFlags =
      DescriptorRangeFlags::DescriptorsVolatile |
      DescriptorRangeFlags::DataVolatile |
      DescriptorRangeFlags::DataStaticWhileSetAtExecute |
      DescriptorRangeFlags::DataStatic |
      DescriptorRangeFlags::DescriptorsStaticKeepingBufferBoundsChecks;
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Flags,
            ValidDescriptorRangeFlags);

  Elem = Elements[4].getElement();
  ASSERT_TRUE(std::holds_alternative<DescriptorTable>(Elem));
  ASSERT_EQ(std::get<DescriptorTable>(Elem).NumClauses, (uint32_t)4);
  ASSERT_EQ(std::get<DescriptorTable>(Elem).Visibility,
            llvm::dxbc::ShaderVisibility::Pixel);

  // Empty Descriptor Table
  Elem = Elements[5].getElement();
  ASSERT_TRUE(std::holds_alternative<DescriptorTable>(Elem));
  ASSERT_EQ(std::get<DescriptorTable>(Elem).NumClauses, 0u);
  ASSERT_EQ(std::get<DescriptorTable>(Elem).Visibility,
            llvm::dxbc::ShaderVisibility::All);

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, ValidParseStaticSamplerTest) {
  const llvm::StringLiteral Source = R"cc(
    StaticSampler(s0),
    StaticSampler(s0, maxAnisotropy = 3, space = 4,
      visibility = SHADER_VISIBILITY_DOMAIN,
      minLOD = 4.2f, mipLODBias = 0.23e+3,
      addressW = TEXTURE_ADDRESS_CLAMP,
      addressV = TEXTURE_ADDRESS_BORDER,
      filter = FILTER_MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT,
      maxLOD = 9000, addressU = TEXTURE_ADDRESS_MIRROR,
      comparisonFunc = COMPARISON_NOT_EQUAL,
      borderColor = STATIC_BORDER_COLOR_OPAQUE_BLACK_UINT
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test no diagnostics produced
  Consumer->setNoDiag();

  ASSERT_FALSE(Parser.parse());

  auto Elements = Parser.getElements();
  ASSERT_EQ(Elements.size(), 2u);

  // Check default values are as expected
  RootElement Elem = Elements[0].getElement();
  ASSERT_TRUE(std::holds_alternative<StaticSampler>(Elem));
  ASSERT_EQ(std::get<StaticSampler>(Elem).Reg.ViewType, RegisterType::SReg);
  ASSERT_EQ(std::get<StaticSampler>(Elem).Reg.Number, 0u);
  ASSERT_EQ(std::get<StaticSampler>(Elem).Filter,
            llvm::dxbc::SamplerFilter::Anisotropic);
  ASSERT_EQ(std::get<StaticSampler>(Elem).AddressU,
            llvm::dxbc::TextureAddressMode::Wrap);
  ASSERT_EQ(std::get<StaticSampler>(Elem).AddressV,
            llvm::dxbc::TextureAddressMode::Wrap);
  ASSERT_EQ(std::get<StaticSampler>(Elem).AddressW,
            llvm::dxbc::TextureAddressMode::Wrap);
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MipLODBias, 0.f);
  ASSERT_EQ(std::get<StaticSampler>(Elem).MaxAnisotropy, 16u);
  ASSERT_EQ(std::get<StaticSampler>(Elem).CompFunc,
            llvm::dxbc::ComparisonFunc::LessEqual);
  ASSERT_EQ(std::get<StaticSampler>(Elem).BorderColor,
            llvm::dxbc::StaticBorderColor::OpaqueWhite);
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MinLOD, 0.f);
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MaxLOD, 3.402823466e+38f);
  ASSERT_EQ(std::get<StaticSampler>(Elem).Space, 0u);
  ASSERT_EQ(std::get<StaticSampler>(Elem).Visibility,
            llvm::dxbc::ShaderVisibility::All);

  // Check values can be set as expected
  Elem = Elements[1].getElement();
  ASSERT_TRUE(std::holds_alternative<StaticSampler>(Elem));
  ASSERT_EQ(std::get<StaticSampler>(Elem).Reg.ViewType, RegisterType::SReg);
  ASSERT_EQ(std::get<StaticSampler>(Elem).Reg.Number, 0u);
  ASSERT_EQ(std::get<StaticSampler>(Elem).Filter,
            llvm::dxbc::SamplerFilter::MaximumMinPointMagLinearMipPoint);
  ASSERT_EQ(std::get<StaticSampler>(Elem).AddressU,
            llvm::dxbc::TextureAddressMode::Mirror);
  ASSERT_EQ(std::get<StaticSampler>(Elem).AddressV,
            llvm::dxbc::TextureAddressMode::Border);
  ASSERT_EQ(std::get<StaticSampler>(Elem).AddressW,
            llvm::dxbc::TextureAddressMode::Clamp);
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MipLODBias, 230.f);
  ASSERT_EQ(std::get<StaticSampler>(Elem).MaxAnisotropy, 3u);
  ASSERT_EQ(std::get<StaticSampler>(Elem).CompFunc,
            llvm::dxbc::ComparisonFunc::NotEqual);
  ASSERT_EQ(std::get<StaticSampler>(Elem).BorderColor,
            llvm::dxbc::StaticBorderColor::OpaqueBlackUint);
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MinLOD, 4.2f);
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MaxLOD, 9000.f);
  ASSERT_EQ(std::get<StaticSampler>(Elem).Space, 4u);
  ASSERT_EQ(std::get<StaticSampler>(Elem).Visibility,
            llvm::dxbc::ShaderVisibility::Domain);

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, ValidParseFloatsTest) {
  const llvm::StringLiteral Source = R"cc(
    StaticSampler(s0, mipLODBias = 0),
    StaticSampler(s0, mipLODBias = +1),
    StaticSampler(s0, mipLODBias = -1),
    StaticSampler(s0, mipLODBias = 42.),
    StaticSampler(s0, mipLODBias = +4.2),
    StaticSampler(s0, mipLODBias = -.42),
    StaticSampler(s0, mipLODBias = .42e+3),
    StaticSampler(s0, mipLODBias = 42E-12),
    StaticSampler(s0, mipLODBias = 42.f),
    StaticSampler(s0, mipLODBias = 4.2F),
    StaticSampler(s0, mipLODBias = 42.e+10f),
    StaticSampler(s0, mipLODBias = -2147483648),
    StaticSampler(s0, mipLODBias = 2147483648),
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test no diagnostics produced
  Consumer->setNoDiag();

  ASSERT_FALSE(Parser.parse());

  auto Elements = Parser.getElements();
  RootElement Elem = Elements[0].getElement();
  ASSERT_TRUE(std::holds_alternative<StaticSampler>(Elem));
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MipLODBias, 0.f);

  Elem = Elements[1].getElement();
  ASSERT_TRUE(std::holds_alternative<StaticSampler>(Elem));
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MipLODBias, 1.f);

  Elem = Elements[2].getElement();
  ASSERT_TRUE(std::holds_alternative<StaticSampler>(Elem));
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MipLODBias, -1.f);

  Elem = Elements[3].getElement();
  ASSERT_TRUE(std::holds_alternative<StaticSampler>(Elem));
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MipLODBias, 42.f);

  Elem = Elements[4].getElement();
  ASSERT_TRUE(std::holds_alternative<StaticSampler>(Elem));
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MipLODBias, 4.2f);

  Elem = Elements[5].getElement();
  ASSERT_TRUE(std::holds_alternative<StaticSampler>(Elem));
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MipLODBias, -.42f);

  Elem = Elements[6].getElement();
  ASSERT_TRUE(std::holds_alternative<StaticSampler>(Elem));
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MipLODBias, 420.f);

  Elem = Elements[7].getElement();
  ASSERT_TRUE(std::holds_alternative<StaticSampler>(Elem));
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MipLODBias, 0.000000000042f);

  Elem = Elements[8].getElement();
  ASSERT_TRUE(std::holds_alternative<StaticSampler>(Elem));
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MipLODBias, 42.f);

  Elem = Elements[9].getElement();
  ASSERT_TRUE(std::holds_alternative<StaticSampler>(Elem));
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MipLODBias, 4.2f);

  Elem = Elements[10].getElement();
  ASSERT_TRUE(std::holds_alternative<StaticSampler>(Elem));
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MipLODBias, 420000000000.f);

  Elem = Elements[11].getElement();
  ASSERT_TRUE(std::holds_alternative<StaticSampler>(Elem));
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MipLODBias, -2147483648.f);

  Elem = Elements[12].getElement();
  ASSERT_TRUE(std::holds_alternative<StaticSampler>(Elem));
  ASSERT_FLOAT_EQ(std::get<StaticSampler>(Elem).MipLODBias, 2147483648.f);

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, ValidSamplerFlagsTest) {
  // This test will checks we can set the valid enum for Sampler descriptor
  // range flags
  const llvm::StringLiteral Source = R"cc(
    DescriptorTable(Sampler(s0, flags = DESCRIPTORS_VOLATILE))
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test no diagnostics produced
  Consumer->setNoDiag();

  ASSERT_FALSE(Parser.parse());

  auto Elements = Parser.getElements();
  RootElement Elem = Elements[0].getElement();
  ASSERT_TRUE(std::holds_alternative<DescriptorTableClause>(Elem));
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Type, ClauseType::Sampler);
  auto ValidSamplerFlags =
      llvm::dxbc::DescriptorRangeFlags::DescriptorsVolatile;
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Flags, ValidSamplerFlags);

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, ValidParseRootConsantsTest) {
  const llvm::StringLiteral Source = R"cc(
    RootConstants(num32BitConstants = 1, b0),
    RootConstants(b42, space = 3, num32BitConstants = 4294967295,
      visibility = SHADER_VISIBILITY_HULL
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test no diagnostics produced
  Consumer->setNoDiag();

  ASSERT_FALSE(Parser.parse());

  auto Elements = Parser.getElements();
  ASSERT_EQ(Elements.size(), 2u);

  RootElement Elem = Elements[0].getElement();
  ASSERT_TRUE(std::holds_alternative<RootConstants>(Elem));
  ASSERT_EQ(std::get<RootConstants>(Elem).Num32BitConstants, 1u);
  ASSERT_EQ(std::get<RootConstants>(Elem).Reg.ViewType, RegisterType::BReg);
  ASSERT_EQ(std::get<RootConstants>(Elem).Reg.Number, 0u);
  ASSERT_EQ(std::get<RootConstants>(Elem).Space, 0u);
  ASSERT_EQ(std::get<RootConstants>(Elem).Visibility,
            llvm::dxbc::ShaderVisibility::All);

  Elem = Elements[1].getElement();
  ASSERT_TRUE(std::holds_alternative<RootConstants>(Elem));
  ASSERT_EQ(std::get<RootConstants>(Elem).Num32BitConstants, 4294967295u);
  ASSERT_EQ(std::get<RootConstants>(Elem).Reg.ViewType, RegisterType::BReg);
  ASSERT_EQ(std::get<RootConstants>(Elem).Reg.Number, 42u);
  ASSERT_EQ(std::get<RootConstants>(Elem).Space, 3u);
  ASSERT_EQ(std::get<RootConstants>(Elem).Visibility,
            llvm::dxbc::ShaderVisibility::Hull);

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, ValidParseRootFlagsTest) {
  using llvm::dxbc::RootFlags;
  const llvm::StringLiteral Source = R"cc(
    RootFlags(),
    RootFlags(0),
    RootFlags(
      deny_domain_shader_root_access |
      deny_pixel_shader_root_access |
      local_root_signature |
      cbv_srv_uav_heap_directly_indexed |
      deny_amplification_shader_root_access |
      deny_geometry_shader_root_access |
      deny_hull_shader_root_access |
      deny_mesh_shader_root_access |
      allow_stream_output |
      sampler_heap_directly_indexed |
      allow_input_assembler_input_layout |
      deny_vertex_shader_root_access
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test no diagnostics produced
  Consumer->setNoDiag();

  ASSERT_FALSE(Parser.parse());

  auto Elements = Parser.getElements();
  ASSERT_EQ(Elements.size(), 3u);

  RootElement Elem = Elements[0].getElement();
  ASSERT_TRUE(std::holds_alternative<RootFlags>(Elem));
  ASSERT_EQ(std::get<RootFlags>(Elem), RootFlags::None);

  Elem = Elements[1].getElement();
  ASSERT_TRUE(std::holds_alternative<RootFlags>(Elem));
  ASSERT_EQ(std::get<RootFlags>(Elem), RootFlags::None);

  Elem = Elements[2].getElement();
  ASSERT_TRUE(std::holds_alternative<RootFlags>(Elem));
  auto ValidRootFlags = RootFlags::AllowInputAssemblerInputLayout |
                        RootFlags::DenyVertexShaderRootAccess |
                        RootFlags::DenyHullShaderRootAccess |
                        RootFlags::DenyDomainShaderRootAccess |
                        RootFlags::DenyGeometryShaderRootAccess |
                        RootFlags::DenyPixelShaderRootAccess |
                        RootFlags::AllowStreamOutput |
                        RootFlags::LocalRootSignature |
                        RootFlags::DenyAmplificationShaderRootAccess |
                        RootFlags::DenyMeshShaderRootAccess |
                        RootFlags::CBVSRVUAVHeapDirectlyIndexed |
                        RootFlags::SamplerHeapDirectlyIndexed;
  ASSERT_EQ(std::get<RootFlags>(Elem), ValidRootFlags);

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, ValidParseRootDescriptorsTest) {
  using llvm::dxbc::RootDescriptorFlags;
  const llvm::StringLiteral Source = R"cc(
    CBV(b0),
    SRV(space = 4, t42, visibility = SHADER_VISIBILITY_GEOMETRY,
      flags = DATA_VOLATILE | DATA_STATIC | DATA_STATIC_WHILE_SET_AT_EXECUTE
    ),
    UAV(visibility = SHADER_VISIBILITY_HULL, u34893247),
    CBV(b0, flags = 0),
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test no diagnostics produced
  Consumer->setNoDiag();

  ASSERT_FALSE(Parser.parse());

  auto Elements = Parser.getElements();
  ASSERT_EQ(Elements.size(), 4u);

  RootElement Elem = Elements[0].getElement();
  ASSERT_TRUE(std::holds_alternative<RootDescriptor>(Elem));
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Type, DescriptorType::CBuffer);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Reg.ViewType, RegisterType::BReg);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Reg.Number, 0u);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Space, 0u);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Visibility,
            llvm::dxbc::ShaderVisibility::All);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Flags,
            RootDescriptorFlags::DataStaticWhileSetAtExecute);

  Elem = Elements[1].getElement();
  ASSERT_TRUE(std::holds_alternative<RootDescriptor>(Elem));
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Type, DescriptorType::SRV);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Reg.ViewType, RegisterType::TReg);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Reg.Number, 42u);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Space, 4u);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Visibility,
            llvm::dxbc::ShaderVisibility::Geometry);
  auto ValidRootDescriptorFlags =
      RootDescriptorFlags::DataVolatile |
      RootDescriptorFlags::DataStaticWhileSetAtExecute |
      RootDescriptorFlags::DataStatic;
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Flags, ValidRootDescriptorFlags);

  Elem = Elements[2].getElement();
  ASSERT_TRUE(std::holds_alternative<RootDescriptor>(Elem));
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Type, DescriptorType::UAV);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Reg.ViewType, RegisterType::UReg);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Reg.Number, 34893247u);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Space, 0u);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Visibility,
            llvm::dxbc::ShaderVisibility::Hull);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Flags,
            RootDescriptorFlags::DataVolatile);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Flags,
            RootDescriptorFlags::DataVolatile);

  Elem = Elements[3].getElement();
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Type, DescriptorType::CBuffer);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Reg.ViewType, RegisterType::BReg);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Reg.Number, 0u);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Space, 0u);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Visibility,
            llvm::dxbc::ShaderVisibility::All);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Flags, RootDescriptorFlags::None);

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, ValidTrailingCommaTest) {
  // This test will checks we can handling trailing commas ','
  const llvm::StringLiteral Source = R"cc(
    DescriptorTable(
      CBV(b0, ),
      SRV(t42),
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test no diagnostics produced
  Consumer->setNoDiag();

  ASSERT_FALSE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, ValidVersion10Test) {
  // This test checks that the default values are set correctly
  // when parsing with root signature version 1.0
  const llvm::StringLiteral Source = R"cc(
    CBV(b0),
    SRV(t0),
    UAV(u0),
    DescriptorTable(
      CBV(b1),
      SRV(t1),
      UAV(u1),
      Sampler(s1),
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_0, Signature, *PP);

  // Test no diagnostics produced
  Consumer->setNoDiag();

  ASSERT_FALSE(Parser.parse());

  auto Elements = Parser.getElements();
  auto DefRootDescriptorFlag = llvm::dxbc::RootDescriptorFlags::DataVolatile;
  RootElement Elem = Elements[0].getElement();
  ASSERT_TRUE(std::holds_alternative<RootDescriptor>(Elem));
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Type, DescriptorType::CBuffer);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Flags, DefRootDescriptorFlag);

  Elem = Elements[1].getElement();
  ASSERT_TRUE(std::holds_alternative<RootDescriptor>(Elem));
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Type, DescriptorType::SRV);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Flags, DefRootDescriptorFlag);

  Elem = Elements[2].getElement();
  ASSERT_TRUE(std::holds_alternative<RootDescriptor>(Elem));
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Type, DescriptorType::UAV);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Flags, DefRootDescriptorFlag);

  auto ValidNonSamplerFlags =
      llvm::dxbc::DescriptorRangeFlags::DescriptorsVolatile |
      llvm::dxbc::DescriptorRangeFlags::DataVolatile;
  Elem = Elements[3].getElement();
  ASSERT_TRUE(std::holds_alternative<DescriptorTableClause>(Elem));
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Type, ClauseType::CBuffer);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Flags, ValidNonSamplerFlags);

  Elem = Elements[4].getElement();
  ASSERT_TRUE(std::holds_alternative<DescriptorTableClause>(Elem));
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Type, ClauseType::SRV);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Flags, ValidNonSamplerFlags);

  Elem = Elements[5].getElement();
  ASSERT_TRUE(std::holds_alternative<DescriptorTableClause>(Elem));
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Type, ClauseType::UAV);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Flags, ValidNonSamplerFlags);

  Elem = Elements[6].getElement();
  ASSERT_TRUE(std::holds_alternative<DescriptorTableClause>(Elem));
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Type, ClauseType::Sampler);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Flags,
            llvm::dxbc::DescriptorRangeFlags::DescriptorsVolatile);

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, ValidVersion11Test) {
  // This test checks that the default values are set correctly
  // when parsing with root signature version 1.1
  const llvm::StringLiteral Source = R"cc(
    CBV(b0),
    SRV(t0),
    UAV(u0),
    DescriptorTable(
      CBV(b1),
      SRV(t1),
      UAV(u1),
      Sampler(s1),
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test no diagnostics produced
  Consumer->setNoDiag();

  ASSERT_FALSE(Parser.parse());

  auto Elements = Parser.getElements();
  RootElement Elem = Elements[0].getElement();
  ASSERT_TRUE(std::holds_alternative<RootDescriptor>(Elem));
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Type, DescriptorType::CBuffer);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Flags,
            llvm::dxbc::RootDescriptorFlags::DataStaticWhileSetAtExecute);

  Elem = Elements[1].getElement();
  ASSERT_TRUE(std::holds_alternative<RootDescriptor>(Elem));
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Type, DescriptorType::SRV);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Flags,
            llvm::dxbc::RootDescriptorFlags::DataStaticWhileSetAtExecute);

  Elem = Elements[2].getElement();
  ASSERT_TRUE(std::holds_alternative<RootDescriptor>(Elem));
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Type, DescriptorType::UAV);
  ASSERT_EQ(std::get<RootDescriptor>(Elem).Flags,
            llvm::dxbc::RootDescriptorFlags::DataVolatile);

  Elem = Elements[3].getElement();
  ASSERT_TRUE(std::holds_alternative<DescriptorTableClause>(Elem));
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Type, ClauseType::CBuffer);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Flags,
            llvm::dxbc::DescriptorRangeFlags::DataStaticWhileSetAtExecute);

  Elem = Elements[4].getElement();
  ASSERT_TRUE(std::holds_alternative<DescriptorTableClause>(Elem));
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Type, ClauseType::SRV);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Flags,
            llvm::dxbc::DescriptorRangeFlags::DataStaticWhileSetAtExecute);

  Elem = Elements[5].getElement();
  ASSERT_TRUE(std::holds_alternative<DescriptorTableClause>(Elem));
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Type, ClauseType::UAV);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Flags,
            llvm::dxbc::DescriptorRangeFlags::DataVolatile);

  Elem = Elements[6].getElement();
  ASSERT_TRUE(std::holds_alternative<DescriptorTableClause>(Elem));
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Type, ClauseType::Sampler);
  ASSERT_EQ(std::get<DescriptorTableClause>(Elem).Flags,
            llvm::dxbc::DescriptorRangeFlags::None);

  ASSERT_TRUE(Consumer->isSatisfied());
}

// Invalid Parser Tests

TEST_F(ParseHLSLRootSignatureTest, InvalidParseUnexpectedTokenTest) {
  const llvm::StringLiteral Source = R"cc(
    DescriptorTable()
    space
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_expected_either);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidParseInvalidTokenTest) {
  const llvm::StringLiteral Source = R"cc(
    notAnIdentifier
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced - invalid token
  Consumer->setExpected(diag::err_hlsl_invalid_token);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidParseUnexpectedEndOfStreamTest) {
  const llvm::StringLiteral Source = R"cc(
    DescriptorTable
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced - expected '(' after DescriptorTable
  Consumer->setExpected(diag::err_expected_after);

  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidMissingDTParameterTest) {
  // This test will check that the parsing fails due a mandatory
  // parameter (register) not being specified
  const llvm::StringLiteral Source = R"cc(
    DescriptorTable(
      CBV()
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_rootsig_missing_param);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidMissingRDParameterTest) {
  // This test will check that the parsing fails due a mandatory
  // parameter (register) not being specified
  const llvm::StringLiteral Source = R"cc(
    SRV()
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_rootsig_missing_param);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidMissingRCParameterTest) {
  // This test will check that the parsing fails due a mandatory
  // parameter (num32BitConstants) not being specified
  const llvm::StringLiteral Source = R"cc(
    RootConstants(b0)
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_rootsig_missing_param);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidRepeatedMandatoryDTParameterTest) {
  // This test will check that the parsing fails due the same mandatory
  // parameter being specified multiple times
  const llvm::StringLiteral Source = R"cc(
    DescriptorTable(
      CBV(b32, b84)
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_rootsig_repeat_param);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidRepeatedMandatoryRCParameterTest) {
  // This test will check that the parsing fails due the same mandatory
  // parameter being specified multiple times
  const llvm::StringLiteral Source = R"cc(
    RootConstants(num32BitConstants = 32, num32BitConstants = 24)
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_rootsig_repeat_param);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidRepeatedOptionalDTParameterTest) {
  // This test will check that the parsing fails due the same optional
  // parameter being specified multiple times
  const llvm::StringLiteral Source = R"cc(
    DescriptorTable(
      CBV(space = 2, space = 0)
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_rootsig_repeat_param);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidRepeatedOptionalRCParameterTest) {
  // This test will check that the parsing fails due the same optional
  // parameter being specified multiple times
  const llvm::StringLiteral Source = R"cc(
    RootConstants(
      visibility = Shader_Visibility_All,
      b0, num32BitConstants = 1,
      visibility = Shader_Visibility_Pixel
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_rootsig_repeat_param);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidLexOverflowedNumberTest) {
  // This test will check that the lexing fails due to an integer overflow
  const llvm::StringLiteral Source = R"cc(
    DescriptorTable(
      CBV(b4294967296)
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_number_literal_overflow);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidParseOverflowedNegativeNumberTest) {
  // This test will check that parsing fails due to a unsigned integer having
  // too large of a magnitude to be interpreted as its negative
  const llvm::StringLiteral Source = R"cc(
    StaticSampler(s0, mipLODBias = -4294967295)
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_number_literal_overflow);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidLexOverflowedFloatTest) {
  // This test will check that the lexing fails due to a float overflow
  const llvm::StringLiteral Source = R"cc(
    StaticSampler(s0, mipLODBias = 3.402823467e+38F)
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_number_literal_overflow);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidLexNegOverflowedFloatTest) {
  // This test will check that the lexing fails due to negative float overflow
  const llvm::StringLiteral Source = R"cc(
    StaticSampler(s0, mipLODBias = -3.402823467e+38F)
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_number_literal_overflow);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidLexOverflowedDoubleTest) {
  // This test will check that the lexing fails due to an overflow of double
  const llvm::StringLiteral Source = R"cc(
    StaticSampler(s0, mipLODBias = 1.e+500)
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_number_literal_overflow);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidLexUnderflowFloatTest) {
  // This test will check that the lexing fails due to double underflow
  const llvm::StringLiteral Source = R"cc(
    StaticSampler(s0, mipLODBias = 10e-309)
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_number_literal_underflow);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidNonZeroFlagsTest) {
  // This test will check that parsing fails when a non-zero integer literal
  // is given to flags
  const llvm::StringLiteral Source = R"cc(
    DescriptorTable(
      CBV(b0, flags = 3)
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_rootsig_non_zero_flag);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidRootElementMissingCommaTest) {
  // This test will check that an error is produced when there is a missing
  // comma between parameters
  const llvm::StringLiteral Source = R"cc(
    RootFlags()
    RootConstants(num32BitConstants = 1, b0)
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_expected_either);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidDescriptorTableMissingCommaTest) {
  // This test will check that an error is produced when there is a missing
  // comma between parameters
  const llvm::StringLiteral Source = R"cc(
    DescriptorTable(
      CBV(b0)
      visibility = SHADER_VISIBILITY_ALL
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_expected_either);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidRootConstantParamsCommaTest) {
  // This test will check that an error is produced when there is a missing
  // comma between parameters
  const llvm::StringLiteral Source = R"cc(
    RootConstants(
      num32BitConstants = 1
      b0
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_expected_either);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidRootDescriptorParamsCommaTest) {
  // This test will check that an error is produced when there is a missing
  // comma between parameters
  const llvm::StringLiteral Source = R"cc(
    CBV(
      b0
      flags = 0
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_expected_either);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidDescriptorClauseParamsCommaTest) {
  // This test will check that an error is produced when there is a missing
  // comma between parameters
  const llvm::StringLiteral Source = R"cc(
    DescriptorTable(
      UAV(
        u0
        flags = 0
      )
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_expected_either);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidStaticSamplerCommaTest) {
  // This test will check that an error is produced when there is a missing
  // comma between parameters
  const llvm::StringLiteral Source = R"cc(
    StaticSampler(
      s0
      maxLOD = 3
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_expected_either);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidRootDescriptorParamTest) {
  // This test will check that an error is produced when there is a invalid
  // value of a parameter
  const llvm::StringLiteral Source = R"cc(
    SRV(t0, invalid)
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_invalid_token);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidDescriptorTableParamTest) {
  // This test will check that an error is produced when there is a invalid
  // value of a parameter
  const llvm::StringLiteral Source = R"cc(
    DescriptorTable(
      visibility = SHADER_VISIBILITY_ALL,
      invalid
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_invalid_token);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidDescriptorTableClauseParamTest) {
  // This test will check that an error is produced when there is a invalid
  // value of a parameter
  const llvm::StringLiteral Source = R"cc(
    DescriptorTable(
      CBV(invalid)
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_invalid_token);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidStaticSamplerParamTest) {
  // This test will check that an error is produced when there is a invalid
  // value of a parameter
  const llvm::StringLiteral Source = R"cc(
    StaticSampler(
      s0,
      filter = FILTER_MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT,
      invalid,
      comparisonFunc = COMPARISON_EQUAL,
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_invalid_token);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidVisibilityValueTest) {
  // This test will check that an error is produced when there is a invalid
  // value of a parameter
  const llvm::StringLiteral Source = R"cc(
    UAV(
      u0,
      visibility = SHADER_VISIBILITY_TYPO
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_invalid_token);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidRegisterValueTest) {
  // This test will check that an error is produced when there is a invalid
  // value of a parameter
  const llvm::StringLiteral Source = R"cc(
    StaticSampler(
      b0
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_invalid_token);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidFilterValueTest) {
  // This test will check that an error is produced when there is a invalid
  // value of a parameter
  const llvm::StringLiteral Source = R"cc(
    StaticSampler(
      s0,
      filter = FILTER_TYPO
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_invalid_token);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidTextureAddressModeValueTest) {
  // This test will check that an error is produced when there is a invalid
  // value of a parameter
  const llvm::StringLiteral Source = R"cc(
    StaticSampler(
      s0,
      addressU = TEXTURE_ADDRESS_MODE_TYPO
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_invalid_token);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidComparisonFuncValueTest) {
  // This test will check that an error is produced when there is a invalid
  // value of a parameter
  const llvm::StringLiteral Source = R"cc(
    StaticSampler(
      s0,
      comparisonFunc = COMPARISON_FUNC_TYPO
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_invalid_token);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidStaticBorderColorValueTest) {
  // This test will check that an error is produced when there is a invalid
  // value of a parameter
  const llvm::StringLiteral Source = R"cc(
    StaticSampler(
      s0,
      borderColor = STATIC_BORDER_COLOR_TYPO
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_invalid_token);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidRootFlagsValueTest) {
  // This test will check that an error is produced when there is a invalid
  // value of a parameter
  const llvm::StringLiteral Source = R"cc(
    RootFlags( ROOT_FLAG_TYPO )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_invalid_token);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidRootDescriptorFlagsValueTest) {
  // This test will check that an error is produced when there is a invalid
  // value of a parameter
  const llvm::StringLiteral Source = R"cc(
    CBV( flags = DATA_STATIC | ROOT_DESRIPTOR_FLAG_TYPO )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_invalid_token);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

TEST_F(ParseHLSLRootSignatureTest, InvalidDescriptorRangeFlagsValueTest) {
  // This test will check that an error is produced when there is a invalid
  // value of a parameter
  const llvm::StringLiteral Source = R"cc(
    DescriptorTable(
      CBV(
        flags = DATA_STATIC | DESRIPTOR_RANGE_FLAG_TYPO | DESCRIPTORS_VOLATILE
      )
    )
  )cc";

  auto Ctx = createMinimalASTContext();
  StringLiteral *Signature = wrapSource(Ctx, Source);

  TrivialModuleLoader ModLoader;
  auto PP = createPP(Source, ModLoader);

  hlsl::RootSignatureParser Parser(RootSignatureVersion::V1_1, Signature, *PP);

  // Test correct diagnostic produced
  Consumer->setExpected(diag::err_hlsl_invalid_token);
  ASSERT_TRUE(Parser.parse());

  ASSERT_TRUE(Consumer->isSatisfied());
}

} // anonymous namespace
