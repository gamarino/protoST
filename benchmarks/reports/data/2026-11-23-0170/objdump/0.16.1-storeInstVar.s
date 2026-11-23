endbr64
push   r12
mov    r12,rdx
push   rbp
mov    rbp,r8
push   rbx
mov    rbx,rdi
sub    rsp,0x10
mov    rdi,QWORD PTR [rdx]
mov    edx,ecx
mov    rax,QWORD PTR [rdi+0x138]
test   rax,rax
je     <_ZN7protoST15ExecutionEngine12storeInstVarEPN5proto12ProtoContextERKNS0_5FrameEjPKNS1_11ProtoObjectE>
cmp    rdx,QWORD PTR [rax]
jb     <_ZN7protoST15ExecutionEngine12storeInstVarEPN5proto12ProtoContextERKNS0_5FrameEjPKNS1_11ProtoObjectE>
mov    QWORD PTR [rsp+0x8],rsi
call   <_ZNK7protoST14BytecodeModule12ivSymbolSlowEPN5proto12ProtoContextEm>
mov    rsi,QWORD PTR [rsp+0x8]
mov    rdx,rax
mov    rax,QWORD PTR [rbx+0x30]
mov    ecx,DWORD PTR [r12+0x10]
mov    rax,QWORD PTR [rax]
add    ecx,0x1
mov    rbx,QWORD PTR [rax+rcx*8]
test   rbx,rbx
je     <_ZN7protoST15ExecutionEngine12storeInstVarEPN5proto12ProtoContextERKNS0_5FrameEjPKNS1_11ProtoObjectE.cold>
cmp    rbx,0x141
je     <_ZN7protoST15ExecutionEngine12storeInstVarEPN5proto12ProtoContextERKNS0_5FrameEjPKNS1_11ProtoObjectE.cold>
mov    rcx,rbp
mov    rdi,rbx
call   <_ZNK5proto11ProtoObject12setAttributeEPNS_12ProtoContextEPKNS_11ProtoStringEPKS0_@plt>
cmp    rax,rbx
jne    <_ZN7protoST15ExecutionEngine12storeInstVarEPN5proto12ProtoContextERKNS0_5FrameEjPKNS1_11ProtoObjectE.cold>
add    rsp,0x10
pop    rbx
pop    rbp
pop    r12
ret
nop    DWORD PTR [rax+rax*1+0x0]
mov    r8,QWORD PTR [rax+0x8]
lea    rcx,[rdx+rdx*2]
lea    rcx,[r8+rcx*8]
mov    rax,QWORD PTR [rcx+0x8]
test   rax,rax
je     <_ZN7protoST15ExecutionEngine12storeInstVarEPN5proto12ProtoContextERKNS0_5FrameEjPKNS1_11ProtoObjectE>
mov    rdx,rax
jmp    <_ZN7protoST15ExecutionEngine12storeInstVarEPN5proto12ProtoContextERKNS0_5FrameEjPKNS1_11ProtoObjectE>
