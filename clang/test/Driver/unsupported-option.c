// RUN: not %clang %s --hedonism -### 2>&1 | \
// RUN: FileCheck %s
// CHECK: error: unknown argument: '--hedonism'

// RUN: not %clang %s --hell -### 2>&1 | \
// RUN: FileCheck %s --check-prefix=DID-YOU-MEAN
// DID-YOU-MEAN: error: unknown argument '--hell'; did you mean '--help'?

// RUN: not %clang --target=powerpc-ibm-aix %s -mlong-double-128 2>&1 | \
// RUN: FileCheck %s --check-prefix=AIX-LONGDOUBLE128-ERR
// AIX-LONGDOUBLE128-ERR: error: unsupported option '-mlong-double-128' for target 'powerpc-ibm-aix'

// RUN: not %clang --target=powerpc64-ibm-aix %s -mlong-double-128 2>&1 | \
// RUN: FileCheck %s --check-prefix=AIX64-LONGDOUBLE128-ERR
// AIX64-LONGDOUBLE128-ERR: error: unsupported option '-mlong-double-128' for target 'powerpc64-ibm-aix'

// RUN: not %clang -fprofile-sample-use=code.prof --target=powerpc-ibm-aix %s 2>&1 | \
// RUN: FileCheck %s --check-prefix=AIX-PROFILE-SAMPLE
// AIX-PROFILE-SAMPLE: error: unsupported option '-fprofile-sample-use=' for target

// -mhtm is unsupported on x86_64. Test that using it in different command
// line permutations generates an `unsupported option` error.
// RUN: not %clang --target=x86_64 -### %s -mhtm 2>&1 \
// RUN:   | FileCheck %s -check-prefix=UNSUP_OPT
// RUN: not %clang --target=x86_64 -### %s -mhtm -lc 2>&1 \
// RUN:   | FileCheck %s -check-prefix=UNSUP_OPT
// RUN: not %clang --target=x86_64 -### -mhtm -lc %s 2>&1 \
// RUN:   | FileCheck %s -check-prefix=UNSUP_OPT
// UNSUP_OPT: error: unsupported option


// RUN: not %clang -c -Qunused-arguments --target=aarch64-- -mfpu=crypto-neon-fp-armv8 %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=QUNUSED_ARGUMENTS
// QUNUSED_ARGUMENTS: error: unsupported option '-mfpu=' for target 'aarch64--'
