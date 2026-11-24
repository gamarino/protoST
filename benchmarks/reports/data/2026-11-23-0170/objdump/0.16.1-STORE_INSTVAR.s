endbr64
mov rdx,QWORD PTR [r15+0x20]
mov eax,DWORD PTR [rdx-0x24]
lea rsi,[rdx-0x40]
test eax,eax
je <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE.cold+X>
mov rdi,QWORD PTR [r15+0x30]
sub eax,0x1
mov ecx,DWORD PTR [rdx-0x2c]
add ecx,DWORD PTR [rdx-0x30]
mov DWORD PTR [rdx-0x24],eax
lea edx,[rax+rcx*1+0x2]
mov rax,QWORD PTR [rdi]
mov ecx,ebx
mov rdi,r15
mov r8,QWORD PTR [rax+rdx*8]
mov rdx,rsi
mov rsi,QWORD PTR [rbp-0x4b8]
call <_ZN7protoST15ExecutionEngine12storeInstVarEPN5proto12ProtoContextERKNS0_5FrameEjPKNS1_11ProtoObjectE>
mov r12,QWORD PTR [r15+0x20]
mov rax,QWORD PTR [r15+0x18]
cmp r12,rax
je <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE+X>
mov r13,QWORD PTR [r12-0x40]
mov r8,QWORD PTR [r12-0x38]
mov rdx,QWORD PTR [r13+0x8]
lea rcx,[r8+0x1]
sub rdx,QWORD PTR [r13+0x0]
cmp rcx,rdx
jae <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE+X>
mov rdi,QWORD PTR [r15+0x10]
movzx eax,BYTE PTR [rdi]
test al,al
jne <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE+X>
mov rdx,QWORD PTR [r13+0x0]
lea rax,[r8+0x2]
movzx esi,BYTE PTR [rdx+r8*1]
movzx ebx,BYTE PTR [rdx+r8*1+0x1]
mov QWORD PTR [r12-0x38],rax
mov BYTE PTR [rbp-0x4c0],sil
cmp sil,0xfe
jne <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE+X>
mov rsi,QWORD PTR [r13+0x8]
sub rsi,rdx
nop
data16 cs nop WORD PTR [rax+rax*1+0x0]
data16 cs nop WORD PTR [rax+rax*1+0x0]
data16 cs nop WORD PTR [rax+rax*1+0x0]
data16 cs nop WORD PTR [rax+rax*1+0x0]
data16 cs nop WORD PTR [rax+rax*1+0x0]
lea rcx,[rax+0x1]
cmp rcx,rsi
jae <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE.cold+X>
movzx ecx,BYTE PTR [rdx+rax*1+0x1]
movzx edi,BYTE PTR [rdx+rax*1]
shl ebx,0x8
add rax,0x2
mov QWORD PTR [r12-0x38],rax
or ebx,ecx
cmp dil,0xfe
je <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE+X>
mov BYTE PTR [rbp-0x4c0],dil
movzx eax,BYTE PTR [rbp-0x4c0]
lea rdx,[rip+0x1619c1] # <_ZZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextEE7kLabels>
jmp QWORD PTR [rdx+rax*8]
