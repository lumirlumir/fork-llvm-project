; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py UTC_ARGS: --version 5
; RUN: llc < %s -mtriple=amdgcn -mcpu=tonga -show-mc-encoding | FileCheck -enable-var-scope -check-prefixes=UNPACKED %s
; RUN: llc < %s -mtriple=amdgcn -mcpu=gfx810 | FileCheck -enable-var-scope -check-prefixes=PACKED %s
; RUN: llc < %s -mtriple=amdgcn -mcpu=gfx900 | FileCheck -enable-var-scope -check-prefixes=PACKED %s

define amdgpu_ps half @buffer_load_format_d16_x(ptr addrspace(8) inreg %rsrc) {
; UNPACKED-LABEL: buffer_load_format_d16_x:
; UNPACKED:       ; %bb.0: ; %main_body
; UNPACKED-NEXT:    buffer_load_format_d16_x v0, off, s[0:3], 0 ; encoding: [0x00,0x00,0x20,0xe0,0x00,0x00,0x00,0x80]
; UNPACKED-NEXT:    s_waitcnt vmcnt(0) ; encoding: [0x70,0x0f,0x8c,0xbf]
; UNPACKED-NEXT:    ; return to shader part epilog
;
; PACKED-LABEL: buffer_load_format_d16_x:
; PACKED:       ; %bb.0: ; %main_body
; PACKED-NEXT:    buffer_load_format_d16_x v0, off, s[0:3], 0
; PACKED-NEXT:    s_waitcnt vmcnt(0)
; PACKED-NEXT:    ; return to shader part epilog
main_body:
  %data = call half @llvm.amdgcn.raw.ptr.buffer.load.format.f16(ptr addrspace(8) %rsrc, i32 0, i32 0, i32 0)
  ret half %data
}

define amdgpu_ps half @buffer_load_format_d16_xy(ptr addrspace(8) inreg %rsrc) {
; UNPACKED-LABEL: buffer_load_format_d16_xy:
; UNPACKED:       ; %bb.0: ; %main_body
; UNPACKED-NEXT:    buffer_load_format_d16_xy v[0:1], off, s[0:3], 0 ; encoding: [0x00,0x00,0x24,0xe0,0x00,0x00,0x00,0x80]
; UNPACKED-NEXT:    s_waitcnt vmcnt(0) ; encoding: [0x70,0x0f,0x8c,0xbf]
; UNPACKED-NEXT:    v_mov_b32_e32 v0, v1 ; encoding: [0x01,0x03,0x00,0x7e]
; UNPACKED-NEXT:    ; return to shader part epilog
;
; PACKED-LABEL: buffer_load_format_d16_xy:
; PACKED:       ; %bb.0: ; %main_body
; PACKED-NEXT:    buffer_load_format_d16_xy v0, off, s[0:3], 0
; PACKED-NEXT:    s_waitcnt vmcnt(0)
; PACKED-NEXT:    v_lshrrev_b32_e32 v0, 16, v0
; PACKED-NEXT:    ; return to shader part epilog
main_body:
  %data = call <2 x half> @llvm.amdgcn.raw.ptr.buffer.load.format.v2f16(ptr addrspace(8) %rsrc, i32 0, i32 0, i32 0)
  %elt = extractelement <2 x half> %data, i32 1
  ret half %elt
}

define amdgpu_ps half @buffer_load_format_d16_xyz(ptr addrspace(8) inreg %rsrc) {
; UNPACKED-LABEL: buffer_load_format_d16_xyz:
; UNPACKED:       ; %bb.0: ; %main_body
; UNPACKED-NEXT:    buffer_load_format_d16_xyz v[0:2], off, s[0:3], 0 ; encoding: [0x00,0x00,0x28,0xe0,0x00,0x00,0x00,0x80]
; UNPACKED-NEXT:    s_waitcnt vmcnt(0) ; encoding: [0x70,0x0f,0x8c,0xbf]
; UNPACKED-NEXT:    v_mov_b32_e32 v0, v2 ; encoding: [0x02,0x03,0x00,0x7e]
; UNPACKED-NEXT:    ; return to shader part epilog
;
; PACKED-LABEL: buffer_load_format_d16_xyz:
; PACKED:       ; %bb.0: ; %main_body
; PACKED-NEXT:    buffer_load_format_d16_xyz v[0:1], off, s[0:3], 0
; PACKED-NEXT:    s_waitcnt vmcnt(0)
; PACKED-NEXT:    v_mov_b32_e32 v0, v1
; PACKED-NEXT:    ; return to shader part epilog
main_body:
  %data = call <3 x half> @llvm.amdgcn.raw.ptr.buffer.load.format.v3f16(ptr addrspace(8) %rsrc, i32 0, i32 0, i32 0)
  %elt = extractelement <3 x half> %data, i32 2
  ret half %elt
}

define amdgpu_ps half @buffer_load_format_d16_xyzw(ptr addrspace(8) inreg %rsrc) {
; UNPACKED-LABEL: buffer_load_format_d16_xyzw:
; UNPACKED:       ; %bb.0: ; %main_body
; UNPACKED-NEXT:    buffer_load_format_d16_xyzw v[0:3], off, s[0:3], 0 ; encoding: [0x00,0x00,0x2c,0xe0,0x00,0x00,0x00,0x80]
; UNPACKED-NEXT:    s_waitcnt vmcnt(0) ; encoding: [0x70,0x0f,0x8c,0xbf]
; UNPACKED-NEXT:    v_mov_b32_e32 v0, v3 ; encoding: [0x03,0x03,0x00,0x7e]
; UNPACKED-NEXT:    ; return to shader part epilog
;
; PACKED-LABEL: buffer_load_format_d16_xyzw:
; PACKED:       ; %bb.0: ; %main_body
; PACKED-NEXT:    buffer_load_format_d16_xyzw v[0:1], off, s[0:3], 0
; PACKED-NEXT:    s_waitcnt vmcnt(0)
; PACKED-NEXT:    v_lshrrev_b32_e32 v0, 16, v1
; PACKED-NEXT:    ; return to shader part epilog
main_body:
  %data = call <4 x half> @llvm.amdgcn.raw.ptr.buffer.load.format.v4f16(ptr addrspace(8) %rsrc, i32 0, i32 0, i32 0)
  %elt = extractelement <4 x half> %data, i32 3
  ret half %elt
}

declare half @llvm.amdgcn.raw.ptr.buffer.load.format.f16(ptr addrspace(8), i32, i32, i32)
declare <2 x half> @llvm.amdgcn.raw.ptr.buffer.load.format.v2f16(ptr addrspace(8), i32, i32, i32)
declare <3 x half> @llvm.amdgcn.raw.ptr.buffer.load.format.v3f16(ptr addrspace(8), i32, i32, i32)
declare <4 x half> @llvm.amdgcn.raw.ptr.buffer.load.format.v4f16(ptr addrspace(8), i32, i32, i32)
