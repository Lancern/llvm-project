; NOTE: Assertions have been autogenerated by utils/update_test_checks.py UTC_ARGS: --version 5
; RUN: opt -mtriple=amdgcn-amd-amdhsa -passes=attributor %s -S | FileCheck %s --check-prefix=AMDGCN

@G = addrspace(1) global i32 zeroinitializer, align 4
declare void @clobber(i32) #0
declare void @clobber.p5(ptr addrspace(5)) #0
declare ptr addrspace(1) @get_ptr() #0
declare noalias ptr addrspace(1) @get_noalias_ptr() #0
declare noalias ptr addrspace(1) @get_untouched_ptr() #1

define void @test_nonkernel(ptr addrspace(1) noalias %ptr) {
; AMDGCN-LABEL: define void @test_nonkernel(
; AMDGCN-SAME: ptr addrspace(1) noalias nofree noundef readonly align 4 captures(none) dereferenceable_or_null(4) [[PTR:%.*]]) #[[ATTR2:[0-9]+]] {
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) [[PTR]], align 4
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7:[0-9]+]]
; AMDGCN-NEXT:    ret void
;
  %val = load i32, ptr addrspace(1) %ptr, align 4
  ;; may not be !invariant.load, as the caller may modify %ptr
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_plain(ptr addrspace(1) %ptr) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_plain(
; AMDGCN-SAME: ptr addrspace(1) nofree noundef readonly align 4 captures(none) dereferenceable_or_null(4) [[PTR:%.*]]) #[[ATTR2]] {
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) [[PTR]], align 4
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %val = load i32, ptr addrspace(1) %ptr, align 4
  ;; may not be !invariant.load, as %ptr may alias a pointer in @clobber
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_noalias_ptr(ptr addrspace(1) noalias %ptr) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_noalias_ptr(
; AMDGCN-SAME: ptr addrspace(1) noalias nofree noundef readonly align 4 captures(none) dereferenceable_or_null(4) [[PTR:%.*]]) #[[ATTR2]] {
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) [[PTR]], align 4, !invariant.load [[META0:![0-9]+]]
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %val = load i32, ptr addrspace(1) %ptr, align 4
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_gep(ptr addrspace(1) %ptr) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_gep(
; AMDGCN-SAME: ptr addrspace(1) nofree readonly align 4 captures(none) [[PTR:%.*]]) #[[ATTR2]] {
; AMDGCN-NEXT:    [[GEP:%.*]] = getelementptr i32, ptr addrspace(1) [[PTR]], i32 4
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) [[GEP]], align 4
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %gep = getelementptr i32, ptr addrspace(1) %ptr, i32 4
  %val = load i32, ptr addrspace(1) %gep, align 4
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_noalias_gep(ptr addrspace(1) noalias %ptr) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_noalias_gep(
; AMDGCN-SAME: ptr addrspace(1) noalias nofree readonly align 4 captures(none) [[PTR:%.*]]) #[[ATTR2]] {
; AMDGCN-NEXT:    [[GEP:%.*]] = getelementptr i32, ptr addrspace(1) [[PTR]], i32 4
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) [[GEP]], align 4, !invariant.load [[META0]]
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %gep = getelementptr i32, ptr addrspace(1) %ptr, i32 4
  %val = load i32, ptr addrspace(1) %gep, align 4
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_swap(ptr addrspace(1) noalias %ptr, i32 inreg %swap) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_swap(
; AMDGCN-SAME: ptr addrspace(1) noalias nofree noundef align 4 captures(none) dereferenceable_or_null(4) [[PTR:%.*]], i32 inreg [[SWAP:%.*]]) #[[ATTR2]] {
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) [[PTR]], align 4
; AMDGCN-NEXT:    store i32 [[SWAP]], ptr addrspace(1) [[PTR]], align 4
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %val = load i32, ptr addrspace(1) %ptr, align 4
  ;; cannot be !invariant.load due to the write to %ptr
  store i32 %swap, ptr addrspace(1) %ptr, align 4
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_volatile(ptr addrspace(1) noalias %ptr) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_volatile(
; AMDGCN-SAME: ptr addrspace(1) noalias nofree noundef align 4 [[PTR:%.*]]) #[[ATTR3:[0-9]+]] {
; AMDGCN-NEXT:    [[VAL:%.*]] = load volatile i32, ptr addrspace(1) [[PTR]], align 4
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %val = load volatile i32, ptr addrspace(1) %ptr, align 4
  ;; volatiles loads cannot be !invariant.load
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_unordered(ptr addrspace(1) noalias %ptr) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_unordered(
; AMDGCN-SAME: ptr addrspace(1) noalias nofree noundef readonly align 4 captures(none) dereferenceable_or_null(4) [[PTR:%.*]]) #[[ATTR2]] {
; AMDGCN-NEXT:    [[VAL:%.*]] = load atomic i32, ptr addrspace(1) [[PTR]] unordered, align 4, !invariant.load [[META0]]
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %val = load atomic i32, ptr addrspace(1) %ptr unordered, align 4
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_monotonic(ptr addrspace(1) noalias %ptr) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_monotonic(
; AMDGCN-SAME: ptr addrspace(1) noalias nofree noundef readonly align 4 captures(none) dereferenceable_or_null(4) [[PTR:%.*]]) #[[ATTR2]] {
; AMDGCN-NEXT:    [[VAL:%.*]] = load atomic i32, ptr addrspace(1) [[PTR]] monotonic, align 4
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %val = load atomic i32, ptr addrspace(1) %ptr monotonic, align 4
  ;; atomic loads with ordering guarantees may have side effects
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_global() {
; AMDGCN-LABEL: define amdgpu_kernel void @test_global(
; AMDGCN-SAME: ) #[[ATTR2]] {
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) @G, align 4
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %val = load i32, ptr addrspace(1) @G, align 4
  ;; is not an !invariant.load as global variables may change
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_alloca(ptr addrspace(1) %ptr) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_alloca(
; AMDGCN-SAME: ptr addrspace(1) nofree noundef readonly align 4 captures(none) dereferenceable_or_null(4) [[PTR:%.*]]) #[[ATTR2]] {
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) [[PTR]], align 4
; AMDGCN-NEXT:    [[ALLOCA:%.*]] = alloca i32, align 4, addrspace(5)
; AMDGCN-NEXT:    store i32 [[VAL]], ptr addrspace(5) [[ALLOCA]], align 4
; AMDGCN-NEXT:    [[LOAD:%.*]] = load i32, ptr addrspace(5) [[ALLOCA]], align 4
; AMDGCN-NEXT:    call void @clobber.p5(ptr addrspace(5) noundef align 4 [[ALLOCA]]) #[[ATTR7]]
; AMDGCN-NEXT:    call void @clobber(i32 [[LOAD]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %val = load i32, ptr addrspace(1) %ptr, align 4
  %alloca = alloca i32, addrspace(5)
  store i32 %val, ptr addrspace(5) %alloca
  %load = load i32, ptr addrspace(5) %alloca
  call void @clobber.p5(ptr addrspace(5) %alloca)
  call void @clobber(i32 %load)
  ret void
}

define internal void @copy.i32(ptr addrspace(5) %alloca, i32 %qty) {
; AMDGCN-LABEL: define internal void @copy.i32(
; AMDGCN-SAME: ptr addrspace(5) noalias nofree noundef writeonly align 4 captures(none) dereferenceable_or_null(4) [[ALLOCA:%.*]], i32 [[QTY:%.*]]) #[[ATTR4:[0-9]+]] {
; AMDGCN-NEXT:    store i32 [[QTY]], ptr addrspace(5) [[ALLOCA]], align 4
; AMDGCN-NEXT:    ret void
;
  store i32 %qty, ptr addrspace(5) %alloca
  ret void
}

define amdgpu_kernel void @test_internal_alloca(ptr addrspace(1) %ptr) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_internal_alloca(
; AMDGCN-SAME: ptr addrspace(1) nofree noundef readonly align 4 captures(none) dereferenceable_or_null(4) [[PTR:%.*]]) #[[ATTR2]] {
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) [[PTR]], align 4
; AMDGCN-NEXT:    [[ALLOCA:%.*]] = alloca i32, align 4, addrspace(5)
; AMDGCN-NEXT:    call void @copy.i32(ptr addrspace(5) noalias nofree noundef writeonly align 4 captures(none) dereferenceable_or_null(4) [[ALLOCA]], i32 [[VAL]]) #[[ATTR8:[0-9]+]]
; AMDGCN-NEXT:    [[LOAD:%.*]] = load i32, ptr addrspace(5) [[ALLOCA]], align 4
; AMDGCN-NEXT:    call void @clobber(i32 [[LOAD]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %val = load i32, ptr addrspace(1) %ptr, align 4
  %alloca = alloca i32, addrspace(5)
  call void @copy.i32(ptr addrspace(5) %alloca, i32 %val)
  %load = load i32, ptr addrspace(5) %alloca
  call void @clobber(i32 %load)
  ret void
}

define internal i32 @test_internal_noalias_load(ptr addrspace(1) %ptr) {
; AMDGCN-LABEL: define internal i32 @test_internal_noalias_load(
; AMDGCN-SAME: ptr addrspace(1) noalias nofree noundef readonly align 4 captures(none) dereferenceable_or_null(4) [[PTR:%.*]]) #[[ATTR5:[0-9]+]] {
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) [[PTR]], align 4, !invariant.load [[META0]]
; AMDGCN-NEXT:    ret i32 [[VAL]]
;
  %val = load i32, ptr addrspace(1) %ptr, align 4
  ;; is an !invariant.load due to its only caller @test_call_internal_noalias
  ret i32 %val
}

define amdgpu_kernel void @test_call_internal_noalias(ptr addrspace(1) noalias %ptr) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_call_internal_noalias(
; AMDGCN-SAME: ptr addrspace(1) noalias nofree readonly captures(none) [[PTR:%.*]]) #[[ATTR2]] {
; AMDGCN-NEXT:    [[VAL:%.*]] = call i32 @test_internal_noalias_load(ptr addrspace(1) noalias nofree noundef readonly align 4 captures(none) [[PTR]]) #[[ATTR9:[0-9]+]]
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %val = call i32 @test_internal_noalias_load(ptr addrspace(1) %ptr)
  call void @clobber(i32 %val)
  ret void
}

define internal i32 @test_internal_load(ptr addrspace(1) noalias %ptr) {
; AMDGCN-LABEL: define internal i32 @test_internal_load(
; AMDGCN-SAME: ptr addrspace(1) noalias nofree noundef readonly align 4 captures(none) dereferenceable_or_null(4) [[PTR:%.*]]) #[[ATTR5]] {
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) [[PTR]], align 4
; AMDGCN-NEXT:    ret i32 [[VAL]]
;
  %val = load i32, ptr addrspace(1) %ptr, align 4
  ;; may not be an !invariant.load since the pointer in @test_call_internal may alias
  ret i32 %val
}

define amdgpu_kernel void @test_call_internal(ptr addrspace(1) %ptr) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_call_internal(
; AMDGCN-SAME: ptr addrspace(1) nofree readonly captures(none) [[PTR:%.*]]) #[[ATTR2]] {
; AMDGCN-NEXT:    [[VAL:%.*]] = call i32 @test_internal_load(ptr addrspace(1) nofree noundef readonly align 4 captures(none) [[PTR]]) #[[ATTR9]]
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %val = call i32 @test_internal_load(ptr addrspace(1) %ptr)
  call void @clobber(i32 %val)
  ret void
}

define internal i32 @test_internal_written(ptr addrspace(1) %ptr) {
; AMDGCN-LABEL: define internal i32 @test_internal_written(
; AMDGCN-SAME: ptr addrspace(1) noalias nofree noundef readonly align 4 captures(none) dereferenceable_or_null(4) [[PTR:%.*]]) #[[ATTR5]] {
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) [[PTR]], align 4
; AMDGCN-NEXT:    ret i32 [[VAL]]
;
  %val = load i32, ptr addrspace(1) %ptr, align 4
  ;; cannot be an !invariant.load because of the write in caller @test_call_internal_written
  ret i32 %val
}

define amdgpu_kernel void @test_call_internal_written(ptr addrspace(1) noalias %ptr, i32 inreg %x) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_call_internal_written(
; AMDGCN-SAME: ptr addrspace(1) noalias nofree captures(none) [[PTR:%.*]], i32 inreg [[X:%.*]]) #[[ATTR2]] {
; AMDGCN-NEXT:    [[VAL:%.*]] = call i32 @test_internal_written(ptr addrspace(1) noalias nofree noundef readonly align 4 captures(none) [[PTR]]) #[[ATTR9]]
; AMDGCN-NEXT:    store i32 [[X]], ptr addrspace(1) [[PTR]], align 4
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %val = call i32 @test_internal_written(ptr addrspace(1) %ptr)
  store i32 %x, ptr addrspace(1) %ptr
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_call_ptr() {
; AMDGCN-LABEL: define amdgpu_kernel void @test_call_ptr(
; AMDGCN-SAME: ) #[[ATTR2]] {
; AMDGCN-NEXT:    [[PTR:%.*]] = call align 4 ptr addrspace(1) @get_ptr() #[[ATTR7]]
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) [[PTR]], align 4
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %ptr = call ptr addrspace(1) @get_ptr()
  %val = load i32, ptr addrspace(1) %ptr, align 4
  ;; may not be an !invariant.load since %ptr may alias
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_call_noalias_ptr() {
; AMDGCN-LABEL: define amdgpu_kernel void @test_call_noalias_ptr(
; AMDGCN-SAME: ) #[[ATTR2]] {
; AMDGCN-NEXT:    [[PTR:%.*]] = call align 4 ptr addrspace(1) @get_noalias_ptr() #[[ATTR7]]
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) [[PTR]], align 4
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %ptr = call ptr addrspace(1) @get_noalias_ptr()
  %val = load i32, ptr addrspace(1) %ptr, align 4
  ;; may not be an !invariant.load since %ptr may have been written to before returning
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_call_untouched_ptr() {
; AMDGCN-LABEL: define amdgpu_kernel void @test_call_untouched_ptr(
; AMDGCN-SAME: ) #[[ATTR2]] {
; AMDGCN-NEXT:    [[PTR:%.*]] = call noalias align 4 ptr addrspace(1) @get_untouched_ptr() #[[ATTR10:[0-9]+]]
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) [[PTR]], align 4, !invariant.load [[META0]]
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %ptr = call ptr addrspace(1) @get_untouched_ptr()
  %val = load i32, ptr addrspace(1) %ptr, align 4
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_make_buffer(ptr addrspace(1) %ptr) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_make_buffer(
; AMDGCN-SAME: ptr addrspace(1) nofree readonly captures(none) [[PTR:%.*]]) #[[ATTR2]] {
; AMDGCN-NEXT:    [[RSRC:%.*]] = call align 4 ptr addrspace(7) @llvm.amdgcn.make.buffer.rsrc.p7.p1(ptr addrspace(1) [[PTR]], i16 noundef 0, i32 noundef 0, i32 noundef 0) #[[ATTR11:[0-9]+]]
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(7) [[RSRC]], align 4
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %rsrc = call ptr addrspace(7) @llvm.amdgcn.make.buffer.rsrc.p7.p1(ptr addrspace(1) %ptr, i16 0, i32 0, i32 0)
  %val = load i32, ptr addrspace(7) %rsrc, align 4
  ;; original %ptr may alias
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_make_buffer_noalias(ptr addrspace(1) noalias %ptr) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_make_buffer_noalias(
; AMDGCN-SAME: ptr addrspace(1) noalias nofree readonly captures(none) [[PTR:%.*]]) #[[ATTR2]] {
; AMDGCN-NEXT:    [[RSRC:%.*]] = call align 4 ptr addrspace(7) @llvm.amdgcn.make.buffer.rsrc.p7.p1(ptr addrspace(1) [[PTR]], i16 noundef 0, i32 noundef 0, i32 noundef 0) #[[ATTR11]]
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(7) [[RSRC]], align 4, !invariant.load [[META0]]
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %rsrc = call ptr addrspace(7) @llvm.amdgcn.make.buffer.rsrc.p7.p1(ptr addrspace(1) %ptr, i16 0, i32 0, i32 0)
  %val = load i32, ptr addrspace(7) %rsrc, align 4
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_selected_load(i1 inreg %cond, ptr addrspace(1) noalias %ptr.true, ptr addrspace(1) noalias %ptr.false) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_selected_load(
; AMDGCN-SAME: i1 inreg [[COND:%.*]], ptr addrspace(1) noalias nofree readonly captures(none) [[PTR_TRUE:%.*]], ptr addrspace(1) noalias nofree readonly captures(none) [[PTR_FALSE:%.*]]) #[[ATTR2]] {
; AMDGCN-NEXT:    [[PTR:%.*]] = select i1 [[COND]], ptr addrspace(1) [[PTR_TRUE]], ptr addrspace(1) [[PTR_FALSE]]
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) [[PTR]], align 4, !invariant.load [[META0]]
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %ptr = select i1 %cond, ptr addrspace(1) %ptr.true, ptr addrspace(1) %ptr.false
  %val = load i32, ptr addrspace(1) %ptr, align 4
  ;; either pointer yields an !invariant.load
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_selected_load_partial_noalias(i1 inreg %cond, ptr addrspace(1) noalias %ptr.true, ptr addrspace(1) %ptr.false) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_selected_load_partial_noalias(
; AMDGCN-SAME: i1 inreg [[COND:%.*]], ptr addrspace(1) noalias nofree readonly captures(none) [[PTR_TRUE:%.*]], ptr addrspace(1) nofree readonly captures(none) [[PTR_FALSE:%.*]]) #[[ATTR2]] {
; AMDGCN-NEXT:    [[PTR:%.*]] = select i1 [[COND]], ptr addrspace(1) [[PTR_TRUE]], ptr addrspace(1) [[PTR_FALSE]]
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) [[PTR]], align 4
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
  %ptr = select i1 %cond, ptr addrspace(1) %ptr.true, ptr addrspace(1) %ptr.false
  %val = load i32, ptr addrspace(1) %ptr, align 4
  ;; %ptr.false may alias, so no !invariant.load
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_branch_load(i1 %cond, ptr addrspace(1) noalias %ptr.true, ptr addrspace(1) noalias %ptr.false) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_branch_load(
; AMDGCN-SAME: i1 noundef [[COND:%.*]], ptr addrspace(1) noalias nofree readonly captures(none) [[PTR_TRUE:%.*]], ptr addrspace(1) noalias nofree readonly captures(none) [[PTR_FALSE:%.*]]) #[[ATTR2]] {
; AMDGCN-NEXT:  [[ENTRY:.*:]]
; AMDGCN-NEXT:    br i1 [[COND]], label %[[TRUE:.*]], label %[[FALSE:.*]]
; AMDGCN:       [[TRUE]]:
; AMDGCN-NEXT:    call void @clobber(i32 noundef 1) #[[ATTR7]]
; AMDGCN-NEXT:    br label %[[FINISH:.*]]
; AMDGCN:       [[FALSE]]:
; AMDGCN-NEXT:    br label %[[FINISH]]
; AMDGCN:       [[FINISH]]:
; AMDGCN-NEXT:    [[PTR:%.*]] = phi ptr addrspace(1) [ [[PTR_TRUE]], %[[TRUE]] ], [ [[PTR_FALSE]], %[[FALSE]] ]
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) [[PTR]], align 4, !invariant.load [[META0]]
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
entry:
  br i1 %cond, label %true, label %false
true:
  call void @clobber(i32 1)
  br label %finish
false:
  br label %finish
finish:
  %ptr = phi ptr addrspace(1) [ %ptr.true, %true ], [ %ptr.false, %false ]
  %val = load i32, ptr addrspace(1) %ptr, align 4
  ;; either pointer yields an !invariant.load
  call void @clobber(i32 %val)
  ret void
}

define amdgpu_kernel void @test_branch_load_partial_noalias(i1 %cond, ptr addrspace(1) noalias %ptr.true, ptr addrspace(1) %ptr.false) {
; AMDGCN-LABEL: define amdgpu_kernel void @test_branch_load_partial_noalias(
; AMDGCN-SAME: i1 noundef [[COND:%.*]], ptr addrspace(1) noalias nofree readonly captures(none) [[PTR_TRUE:%.*]], ptr addrspace(1) nofree readonly captures(none) [[PTR_FALSE:%.*]]) #[[ATTR2]] {
; AMDGCN-NEXT:  [[ENTRY:.*:]]
; AMDGCN-NEXT:    br i1 [[COND]], label %[[TRUE:.*]], label %[[FALSE:.*]]
; AMDGCN:       [[TRUE]]:
; AMDGCN-NEXT:    call void @clobber(i32 noundef 1) #[[ATTR7]]
; AMDGCN-NEXT:    br label %[[FINISH:.*]]
; AMDGCN:       [[FALSE]]:
; AMDGCN-NEXT:    br label %[[FINISH]]
; AMDGCN:       [[FINISH]]:
; AMDGCN-NEXT:    [[PTR:%.*]] = phi ptr addrspace(1) [ [[PTR_TRUE]], %[[TRUE]] ], [ [[PTR_FALSE]], %[[FALSE]] ]
; AMDGCN-NEXT:    [[VAL:%.*]] = load i32, ptr addrspace(1) [[PTR]], align 4
; AMDGCN-NEXT:    call void @clobber(i32 [[VAL]]) #[[ATTR7]]
; AMDGCN-NEXT:    ret void
;
entry:
  br i1 %cond, label %true, label %false
true:
  call void @clobber(i32 1)
  br label %finish
false:
  br label %finish
finish:
  %ptr = phi ptr addrspace(1) [ %ptr.true, %true ], [ %ptr.false, %false ]
  %val = load i32, ptr addrspace(1) %ptr, align 4
  ;; ptr.false may alias, so no !invariant.load
  call void @clobber(i32 %val)
  ret void
}

attributes #0 = { nofree norecurse nosync nounwind willreturn }
attributes #1 = { nofree norecurse nosync nounwind willreturn readonly }
;.
; AMDGCN: [[META0]] = !{}
;.
