endbr64
mov r12,QWORD PTR [r15+0x20]
mov esi,ebx
mov rdi,QWORD PTR [r12-0x40]
lea rax,[r12-0x40]
mov QWORD PTR [rbp-0x5c8],rax
mov rax,QWORD PTR [rdi+0xd8]
mov rcx,QWORD PTR [rdi+0xd0]
mov rdx,rax
sub rdx,rcx
sar rdx,0x3
cmp rsi,rdx
jae <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE.cold+X>
lea r14,[rcx+rsi*8]
mov edx,DWORD PTR [r12-0x24]
movzx r10d,WORD PTR [r14+0x6]
mov DWORD PTR [rbp-0x5c0],r10d
cmp edx,r10d
jb <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE.cold+X>
cmp BYTE PTR [r12-0x10],0x0
jne <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE+X>
test edx,edx
je <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE.cold+X>
mov rax,QWORD PTR [r15+0x30]
mov ecx,DWORD PTR [r12-0x2c]
mov rdi,r15
add edx,DWORD PTR [r12-0x30]
mov rsi,QWORD PTR [rbp-0x5b8]
mov rax,QWORD PTR [rax]
lea edx,[rdx+rcx*1+0x1]
mov ecx,DWORD PTR [r14]
mov r8,QWORD PTR [rax+rdx*8]
mov rdx,QWORD PTR [rbp-0x5c8]
call <_ZN7protoST15ExecutionEngine12storeInstVarEPN5proto12ProtoContextERKNS0_5FrameEjPKNS1_11ProtoObjectE>
mov r12,QWORD PTR [r15+0x20]
mov ecx,DWORD PTR [r12-0x24]
test ecx,ecx
je <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE.cold+X>
mov rsi,QWORD PTR [r15+0x30]
sub ecx,0x1
mov eax,DWORD PTR [r12-0x2c]
add eax,DWORD PTR [r12-0x30]
mov rdi,QWORD PTR [rsi]
lea edx,[rcx+rax*1+0x2]
sub ecx,DWORD PTR [rbp-0x5c0]
mov r8,QWORD PTR [rdi+rdx*8]
lea edx,[rcx+0x1]
mov DWORD PTR [r12-0x24],edx
cmp edx,DWORD PTR [r12-0x28]
jae <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE.cold+X>
lea eax,[rdx+rax*1+0x2]
add ecx,0x2
mov QWORD PTR [rdi+rax*8],r8
mov rax,QWORD PTR [r15+0x18]
mov DWORD PTR [r12-0x24],ecx
cmp r12,rax
je <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE+X>
mov r13,QWORD PTR [r12-0x40]
mov r8,QWORD PTR [r12-0x38]
mov rdx,QWORD PTR [r13+0x8]
lea rdi,[r8+0x1]
sub rdx,QWORD PTR [r13+0x0]
cmp rdi,rdx
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
mov BYTE PTR [rbp-0x5c0],sil
cmp sil,0xfe
jne <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE+X>
mov rsi,QWORD PTR [r13+0x8]
sub rsi,rdx
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
mov BYTE PTR [rbp-0x5c0],dil
movzx eax,BYTE PTR [rbp-0x5c0]
lea rdx,[rip+0x18165e] # <_ZZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextEE7kLabels>
jmp QWORD PTR [rdx+rax*8]
