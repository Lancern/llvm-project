; RUN: llc -mtriple=amdgcn < %s | FileCheck -check-prefix=GCN %s

; GCN-LABEL: {{^}}global_truncstore_f64_to_f16:
; GCN: s_endpgm
define amdgpu_kernel void @global_truncstore_f64_to_f16(ptr addrspace(1) %out, ptr addrspace(1) %in) #0 {
  %val = load double, ptr addrspace(1) %in
  %cvt = fptrunc double %val to half
  store half %cvt, ptr addrspace(1) %out
  ret void
}

; GCN-LABEL: {{^}}global_truncstore_v2f64_to_v2f16:
; GCN: s_endpgm
define amdgpu_kernel void @global_truncstore_v2f64_to_v2f16(ptr addrspace(1) %out, ptr addrspace(1) %in) #0 {
  %val = load <2 x double>, ptr addrspace(1) %in
  %cvt = fptrunc <2 x double> %val to <2 x half>
  store <2 x half> %cvt, ptr addrspace(1) %out
  ret void
}

; GCN-LABEL: {{^}}global_truncstore_v3f64_to_v3f16:
; GCN: s_endpgm
define amdgpu_kernel void @global_truncstore_v3f64_to_v3f16(ptr addrspace(1) %out, ptr addrspace(1) %in) #0 {
  %val = load <3 x double>, ptr addrspace(1) %in
  %cvt = fptrunc <3 x double> %val to <3 x half>
  store <3 x half> %cvt, ptr addrspace(1) %out
  ret void
}

; GCN-LABEL: {{^}}global_truncstore_v4f64_to_v4f16:
; GCN: s_endpgm
define amdgpu_kernel void @global_truncstore_v4f64_to_v4f16(ptr addrspace(1) %out, ptr addrspace(1) %in) #0 {
  %val = load <4 x double>, ptr addrspace(1) %in
  %cvt = fptrunc <4 x double> %val to <4 x half>
  store <4 x half> %cvt, ptr addrspace(1) %out
  ret void
}

; GCN-LABEL: {{^}}global_truncstore_v8f64_to_v8f16:
; GCN: s_endpgm
define amdgpu_kernel void @global_truncstore_v8f64_to_v8f16(ptr addrspace(1) %out, ptr addrspace(1) %in) #0 {
  %val = load <8 x double>, ptr addrspace(1) %in
  %cvt = fptrunc <8 x double> %val to <8 x half>
  store <8 x half> %cvt, ptr addrspace(1) %out
  ret void
}

; GCN-LABEL: {{^}}global_truncstore_v16f64_to_v16f16:
; GCN: s_endpgm
define amdgpu_kernel void @global_truncstore_v16f64_to_v16f16(ptr addrspace(1) %out, ptr addrspace(1) %in) #0 {
  %val = load <16 x double>, ptr addrspace(1) %in
  %cvt = fptrunc <16 x double> %val to <16 x half>
  store <16 x half> %cvt, ptr addrspace(1) %out
  ret void
}
