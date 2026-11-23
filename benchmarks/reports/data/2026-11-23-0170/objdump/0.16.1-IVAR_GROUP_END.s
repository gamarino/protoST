endbr64
mov rax,QWORD PTR [r15+0x20]
mov esi,ebx
mov rdi,QWORD PTR [rax-0x40]
mov QWORD PTR [rbp-0x4c0],rax
lea r14,[rax-0x40]
mov rax,QWORD PTR [rdi+0xd8]
mov r8,QWORD PTR [rdi+0xd0]
mov rdx,rax
sub rdx,r8
sar rdx,0x3
cmp rsi,rdx
jae <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE.cold+X>
lea r12,[r8+rsi*8]
mov rsi,QWORD PTR [rbp-0x4c0]
mov edx,DWORD PTR [rsi-0x24]
movzx esi,WORD PTR [r12+0x6]
cmp edx,esi
jb <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE.cold+X>
mov r10,QWORD PTR [rbp-0x4c0]
cmp BYTE PTR [r10-0x10],0x0
jne <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE+X>
test edx,edx
je <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE.cold+X>
mov rax,QWORD PTR [r15+0x30]
mov ecx,DWORD PTR [r10-0x2c]
mov rdi,r15
add edx,DWORD PTR [r10-0x30]
mov rsi,QWORD PTR [rbp-0x4b8]
mov rax,QWORD PTR [rax]
lea edx,[rdx+rcx*1+0x1]
mov ecx,DWORD PTR [r12]
mov r8,QWORD PTR [rax+rdx*8]
mov rdx,r14
call <_ZN7protoST15ExecutionEngine12storeInstVarEPN5proto12ProtoContextERKNS0_5FrameEjPKNS1_11ProtoObjectE>
mov rax,QWORD PTR [rbp-0x4c0]
mov eax,DWORD PTR [rax-0x24]
test eax,eax
je <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE.cold+X>
mov rbx,QWORD PTR [rbp-0x4c0]
mov rsi,QWORD PTR [r15+0x30]
sub eax,0x1
mov rdi,QWORD PTR [rsi]
mov edx,DWORD PTR [rbx-0x2c]
add edx,DWORD PTR [rbx-0x30]
lea ecx,[rax+rdx*1+0x2]
mov r8,QWORD PTR [rdi+rcx*8]
movzx ecx,WORD PTR [r12+0x6]
sub eax,ecx
lea ecx,[rax+0x1]
mov DWORD PTR [rbx-0x24],ecx
cmp ecx,DWORD PTR [rbx-0x28]
jae <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE.cold+X>
lea edx,[rcx+rdx*1+0x2]
mov rcx,QWORD PTR [rbp-0x4c0]
add eax,0x2
mov r12,QWORD PTR [r15+0x20]
mov QWORD PTR [rdi+rdx*8],r8
mov DWORD PTR [rcx-0x24],eax
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
cs nop WORD PTR [rax+rax*1+0x0]
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
lea rdx,[rip+0x1629c1] # <_ZZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextEE7kLabels>
jmp QWORD PTR [rdx+rax*8]
