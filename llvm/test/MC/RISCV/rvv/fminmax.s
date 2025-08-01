# RUN: llvm-mc -triple=riscv64 -show-encoding --mattr=+zve32f %s \
# RUN:     | FileCheck %s --check-prefixes=CHECK-ENCODING,CHECK-INST
# RUN: not llvm-mc -triple=riscv64 -show-encoding %s 2>&1 \
# RUN:     | FileCheck %s --check-prefix=CHECK-ERROR
# RUN: llvm-mc -triple=riscv64 -filetype=obj --mattr=+zve32f %s \
# RUN:     | llvm-objdump -d --mattr=+zve32f - \
# RUN:     | FileCheck %s --check-prefix=CHECK-INST
# RUN: llvm-mc -triple=riscv64 -filetype=obj --mattr=+zve32f %s \
# RUN:     | llvm-objdump -d - | FileCheck %s --check-prefix=CHECK-UNKNOWN

vfmin.vv v8, v4, v20, v0.t
# CHECK-INST: vfmin.vv v8, v4, v20, v0.t
# CHECK-ENCODING: [0x57,0x14,0x4a,0x10]
# CHECK-ERROR: instruction requires the following: 'V'{{.*}}'Zve32f' (Vector Extensions for Embedded Processors){{$}}
# CHECK-UNKNOWN: 104a1457 <unknown>

vfmin.vv v8, v4, v20
# CHECK-INST: vfmin.vv v8, v4, v20
# CHECK-ENCODING: [0x57,0x14,0x4a,0x12]
# CHECK-ERROR: instruction requires the following: 'V'{{.*}}'Zve32f' (Vector Extensions for Embedded Processors){{$}}
# CHECK-UNKNOWN: 124a1457 <unknown>

vfmin.vf v8, v4, fa0, v0.t
# CHECK-INST: vfmin.vf v8, v4, fa0, v0.t
# CHECK-ENCODING: [0x57,0x54,0x45,0x10]
# CHECK-ERROR: instruction requires the following: 'V'{{.*}}'Zve32f' (Vector Extensions for Embedded Processors){{$}}
# CHECK-UNKNOWN: 10455457 <unknown>

vfmin.vf v8, v4, fa0
# CHECK-INST: vfmin.vf v8, v4, fa0
# CHECK-ENCODING: [0x57,0x54,0x45,0x12]
# CHECK-ERROR: instruction requires the following: 'V'{{.*}}'Zve32f' (Vector Extensions for Embedded Processors){{$}}
# CHECK-UNKNOWN: 12455457 <unknown>

vfmax.vv v8, v4, v20, v0.t
# CHECK-INST: vfmax.vv v8, v4, v20, v0.t
# CHECK-ENCODING: [0x57,0x14,0x4a,0x18]
# CHECK-ERROR: instruction requires the following: 'V'{{.*}}'Zve32f' (Vector Extensions for Embedded Processors){{$}}
# CHECK-UNKNOWN: 184a1457 <unknown>

vfmax.vv v8, v4, v20
# CHECK-INST: vfmax.vv v8, v4, v20
# CHECK-ENCODING: [0x57,0x14,0x4a,0x1a]
# CHECK-ERROR: instruction requires the following: 'V'{{.*}}'Zve32f' (Vector Extensions for Embedded Processors){{$}}
# CHECK-UNKNOWN: 1a4a1457 <unknown>

vfmax.vf v8, v4, fa0, v0.t
# CHECK-INST: vfmax.vf v8, v4, fa0, v0.t
# CHECK-ENCODING: [0x57,0x54,0x45,0x18]
# CHECK-ERROR: instruction requires the following: 'V'{{.*}}'Zve32f' (Vector Extensions for Embedded Processors){{$}}
# CHECK-UNKNOWN: 18455457 <unknown>

vfmax.vf v8, v4, fa0
# CHECK-INST: vfmax.vf v8, v4, fa0
# CHECK-ENCODING: [0x57,0x54,0x45,0x1a]
# CHECK-ERROR: instruction requires the following: 'V'{{.*}}'Zve32f' (Vector Extensions for Embedded Processors){{$}}
# CHECK-UNKNOWN: 1a455457 <unknown>
