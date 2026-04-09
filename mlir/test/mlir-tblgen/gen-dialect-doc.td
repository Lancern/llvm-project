// RUN: mlir-tblgen -gen-dialect-doc -I %S/../../include -dialect=test %s | FileCheck %s
// RUN: mlir-tblgen -gen-dialect-doc -I %S/../../include -dialect=test_toc %s | FileCheck %s --check-prefix=CHECK_TOC
// RUN: mlir-tblgen -gen-dialect-doc -I %S/../../include -dialect=test -rst %s | FileCheck %s --check-prefix=RST
// RUN: mlir-tblgen -gen-dialect-doc -I %S/../../include -dialect=test_toc -rst %s | FileCheck %s --check-prefix=RST_TOC

include "mlir/IR/OpBase.td"
include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/EnumAttr.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

def Test_Dialect : Dialect {
  let name = "test";
  let summary = "Dialect of ops to test";
  let description = [{
    Dialect without a [TOC] here.
    TOC added by tool.
  }];
  let cppNamespace = "NS";
}

def OpGroupA : OpDocGroup {
  let summary = "Group of ops";
  let description = "Grouped for some reason.";
}

let opDocGroup = OpGroupA in {
def ADOp : Op<Test_Dialect, "d", [NoMemoryEffect, SingleBlockImplicitTerminator<"YieldOp">]>;
def AAOp : Op<Test_Dialect, "a", [NoMemoryEffect, SingleBlockImplicitTerminator<"YieldOp">]>;
}

def OpGroupB : OpDocGroup {
  let summary = "Other group of ops";
  let description = "Grouped for some other reason.";
}

let opDocGroup = OpGroupB in {
def ACOp : Op<Test_Dialect, "c", [NoMemoryEffect, SingleBlockImplicitTerminator<"YieldOp">]>;
def ABOp : Op<Test_Dialect, "b", [NoMemoryEffect, SingleBlockImplicitTerminator<"YieldOp">]>;
}

def AEOp : Op<Test_Dialect, "e", [NoMemoryEffect]> {
  let summary = "Op with a summary";
  let description = "Op with a description";
  let arguments = (ins ConfinedType<AnyType, [CPred<"::llvm::isa<::mlir::TensorType>($_self)">]>:$tensor,
                       I16Attr:$int_attr);
  let results = (outs
    ConfinedType<AnyType, [CPred<"::llvm::isa<::mlir::TensorType>($_self)">]>:$output
  ); 
}

def TestAttr : DialectAttr<Test_Dialect, CPred<"true">> {
  let summary = "attribute summary";
  let description = "attribute description";
}

def TestType : DialectType<Test_Dialect, CPred<"true">> {
  let summary = "type summary";
  let description = "type description";
}

def TestAttrDef : AttrDef<Test_Dialect, "TestAttrDef"> {
  let mnemonic = "test_attr_def";
}

def TestAttrDefParams : AttrDef<Test_Dialect, "TestAttrDefParams"> {
  let mnemonic = "test_attr_def_params";
  let parameters = (ins "int":$value);

  let assemblyFormat = "`<` $value `>`";
}

def TestTypeDef : TypeDef<Test_Dialect, "TestTypeDef"> {
  let mnemonic = "test_type_def";
}

def TestTypeDefParams : TypeDef<Test_Dialect, "TestTypeDefParams"> {
  let mnemonic = "test_type_def_params";
  let parameters = (ins "int":$value);

  let assemblyFormat = "`<` $value `>`";
}

def TestEnum :
    I32EnumAttr<"TestEnum",
        "enum summary", [
        I32EnumAttrCase<"First", 0, "first">,
        I32EnumAttrCase<"Second", 1, "second">,
        I32EnumAttrCase<"Third", 2, "third">]> {
  let genSpecializedAttr = 1;
  let cppNamespace = "NS";
}

// CHECK: Dialect without a [TOC] here.
// CHECK-NEXT:  TOC added by tool.
// CHECK-EMPTY:
// CHECK-NEXT:  [TOC]
// CHECK-EMPTY:

// CHECK-NOT: [TOC]

// CHECK:       test.e
// CHECK-EMPTY:
// CHECK-NEXT:  _Op with a summary_
// CHECK-EMPTY:
// CHECK-NEXT:  Op with a description
// CHECK-EMPTY:

// CHECK: Operands:
// CHECK-EMPTY:
// CHECK-NEXT:  | Operand | Description |
// CHECK-NEXT:  | :-----: | ----------- |
// CHECK-NEXT:  | `tensor` | |
// CHECK-EMPTY:
// CHECK-NEXT:  Results:
// CHECK-EMPTY:
// CHECK-NEXT:  | Result | Description |
// CHECK-NEXT:  | :----: | ----------- |
// CHECK-NEXT:  | `output` | |
// CHECK-EMPTY:

// CHECK: Group of ops
// CHECK: test.a
// CHECK: test.d
// CHECK: Other group
// CHECK: test.b
// CHECK: test.c
// CHECK: Traits: `SingleBlockImplicitTerminator<YieldOp>`, `SingleBlock`
// CHECK: Interfaces: `NoMemoryEffect (MemoryEffectOpInterface)`
// CHECK: Effects: `MemoryEffects::Effect{}`

// CHECK:       ## Attribute constraints
// CHECK-EMPTY:
// CHECK-NEXT:  ### attribute summary
// CHECK-EMPTY:
// CHECK:       attribute description

// CHECK: TestAttrDefAttr
// CHECK: Syntax:
// CHECK: #test.test_attr_def

// CHECK: TestAttrDefParamsAttr
// CHECK: Syntax:
// CHECK: #test.test_attr_def_params

// CHECK: ## Type constraints
// CHECK: ### type summary
// CHECK: type description

// CHECK: TestTypeDefType
// CHECK: Syntax:
// CHECK: !test.test_type_def

// CHECK: TestTypeDefParamsType
// CHECK: Syntax:
// CHECK: !test.test_type_def_params

// CHECK:       ## Enums
// CHECK-EMPTY:
// CHECK-NEXT:  ### TestEnum
// CHECK-EMPTY:
// CHECK-NEXT:  _Enum summary_
// CHECK-EMPTY:
// CHECK-NEXT:  #### Cases:
// CHECK-EMPTY:
// CHECK-NEXT:  | Symbol | Value | String |
// CHECK-NEXT:  | :----: | :---: | ------ |
// CHECK-NEXT:  | First | `0` | first |
// CHECK-NEXT:  | Second | `1` | second |
// CHECK-NEXT:  | Third | `2` | third |
// CHECK-EMPTY:

def Toc_Dialect : Dialect {
  let name = "test_toc";
  let summary = "Dialect of ops to test";
  let description = [{
    Dialect with

    [TOC]

    here.
  }];
  let cppNamespace = "NS";
}
def BOp : Op<Toc_Dialect, "b", []>;

// CHECK_TOC: Dialect with
// CHECK_TOC: [TOC]
// CHECK_TOC: here.

// RST: .. Autogenerated by mlir-tblgen; don't manually edit

// RST:       'test' Dialect
// RST-NEXT:  ==============
// RST-EMPTY:
// RST-NEXT:  *Dialect of ops to test*
// RST-EMPTY:
// RST-NEXT:  Dialect without a [TOC] here.
// RST-NEXT:  TOC added by tool.
// RST-EMPTY:
// RST-NEXT:  .. contents::
// RST-NEXT:     :local:
// RST-EMPTY:

// RST-NOT: [TOC]

// RST:       ``test.e`` (NS::AEOp)
// RST-NEXT:  ~~~~~~~~~~~~~~~~~~~~~
// RST-EMPTY:
// RST-NEXT:  *Op with a summary*
// RST-EMPTY:
// RST-NEXT:  Op with a description
// RST-EMPTY:

// RST: Operands:
// RST-NEXT: ^^^^^^^^^
// RST-EMPTY:
// RST-NEXT: .. list-table::
// RST-NEXT:    :header-rows: 1
// RST-EMPTY:
// RST-NEXT:    * - Operand
// RST-NEXT:      - Description
// RST-NEXT:    * - ``tensor``
// RST-NEXT:      -
// RST-EMPTY:
// RST-NEXT: Results:
// RST-NEXT: ^^^^^^^^
// RST-EMPTY:
// RST-NEXT: .. list-table::
// RST-NEXT:    :header-rows: 1
// RST-EMPTY:
// RST-NEXT:    * - Result
// RST-NEXT:      - Description
// RST-NEXT:    * - ``output``
// RST-NEXT:      -
// RST-EMPTY:

// RST: Group of ops
// RST-NEXT: ~~~~~~~~~~~~
// RST: ``test.a`` (NS::AAOp)
// RST-NEXT: ^^^^^^^^^^^^^^^^^^^^^
// RST: ``test.d`` (NS::ADOp)
// RST-NEXT: ^^^^^^^^^^^^^^^^^^^^^
// RST: Other group of ops
// RST-NEXT: ~~~~~~~~~~~~~~~~~~
// RST: ``test.b`` (NS::ABOp)
// RST-NEXT: ^^^^^^^^^^^^^^^^^^^^^
// RST: ``test.c`` (NS::ACOp)
// RST-NEXT: ^^^^^^^^^^^^^^^^^^^^^
// RST: Traits: ``SingleBlockImplicitTerminator<YieldOp>``, ``SingleBlock``
// RST: Interfaces: ``NoMemoryEffect (MemoryEffectOpInterface)``
// RST: Effects: ``MemoryEffects::Effect{}``

// RST:       Attribute constraints
// RST-NEXT:  ---------------------
// RST-EMPTY:
// RST-NEXT:  attribute summary
// RST-NEXT:  ~~~~~~~~~~~~~~~~~
// RST-EMPTY:
// RST:       attribute description

// RST: TestAttrDefAttr
// RST: Syntax: ``#test.test_attr_def``

// RST: TestAttrDefParamsAttr
// RST: Syntax:
// RST: .. code-block::
// RST: #test.test_attr_def_params

// RST: Type constraints
// RST-NEXT: ----------------
// RST: type summary
// RST-NEXT: ~~~~~~~~~~~~
// RST: type description

// RST: TestTypeDefType
// RST: Syntax: ``!test.test_type_def``

// RST: TestTypeDefParamsType
// RST: Syntax:
// RST: .. code-block::
// RST: !test.test_type_def_params

// RST:       Enums
// RST-NEXT:  -----
// RST-EMPTY:
// RST-NEXT:  TestEnum
// RST-NEXT:  ~~~~~~~~
// RST-EMPTY:
// RST-NEXT:  *Enum summary*
// RST-EMPTY:
// RST-NEXT:  Cases:
// RST-NEXT:  ^^^^^^
// RST-EMPTY:
// RST-NEXT:  .. list-table::
// RST-NEXT:     :header-rows: 1
// RST-EMPTY:
// RST-NEXT:     * - Symbol
// RST-NEXT:       - Value
// RST-NEXT:       - String
// RST-NEXT:     * - First
// RST-NEXT:       - ``0``
// RST-NEXT:       - first
// RST-NEXT:     * - Second
// RST-NEXT:       - ``1``
// RST-NEXT:       - second
// RST-NEXT:     * - Third
// RST-NEXT:       - ``2``
// RST-NEXT:       - third
// RST-EMPTY:

// RST_TOC: Dialect with
// RST_TOC: [TOC]
// RST_TOC: here.
// RST_TOC: .. contents::
// RST_TOC-NEXT: :local:
