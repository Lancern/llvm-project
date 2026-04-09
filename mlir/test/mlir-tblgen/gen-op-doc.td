// RUN: mlir-tblgen -gen-op-doc -I %S/../../include -dialect=foo %s | FileCheck %s --check-prefix=CHECK-FOO
// RUN: mlir-tblgen -gen-op-doc -I %S/../../include -dialect=bar %s | FileCheck %s --check-prefix=CHECK-BAR
// RUN: mlir-tblgen -gen-op-doc -I %S/../../include -dialect=foo -rst %s | FileCheck %s --check-prefix=RST-FOO
// RUN: mlir-tblgen -gen-op-doc -I %S/../../include -dialect=bar -rst %s | FileCheck %s --check-prefix=RST-BAR

include "mlir/IR/OpBase.td"

def Foo_Dialect : Dialect {
  let name = "foo";
}

def Bar_Dialect : Dialect {
  let name = "bar";
}

// CHECK-FOO: foo.a
// CHECK-BAR-NOT: foo.a
def AOp : Op<Foo_Dialect, "a">;

// CHECK-BAR: bar.b
// CHECK-FOO-NOT: bar.b
def BOp : Op<Bar_Dialect, "b">;

// RST-FOO: ``foo.a``
// RST-BAR-NOT: foo.a
// RST-FOO: ~~~~

// RST-BAR: ``bar.b``
// RST-FOO-NOT: bar.b
// RST-BAR: ~~~~
