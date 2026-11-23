endbr64
mov rax,QWORD PTR [r15+0x20]
mov rsi,QWORD PTR [rbp-0x5b8]
mov edx,ebx
mov rdi,QWORD PTR [rax-0x40]
mov r14,rax
call <_ZNK7protoST14BytecodeModule8ivSymbolEPN5proto12ProtoContextEm>
mov rsi,rax
mov rax,QWORD PTR [r15+0x30]
mov edx,DWORD PTR [r14-0x30]
mov rax,QWORD PTR [rax]
add edx,0x1
mov rdi,QWORD PTR [rax+rdx*8]
test rdi,rdi
je <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE+X>
cmp rdi,0x141
je <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE+X>
mov rdx,rsi
mov rsi,QWORD PTR [rbp-0x5b8]
mov ecx,0x1
call <_ZNK5proto11ProtoObject12getAttributeEPNS_12ProtoContextEPKNS_11ProtoStringEb@plt>
mov rdx,rax
test rax,rax
je <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE+X>
mov eax,DWORD PTR [r14-0x24]
cmp eax,DWORD PTR [r14-0x28]
jae <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE.cold+X>
mov edi,DWORD PTR [r14-0x30]
mov rsi,QWORD PTR [r15+0x30]
mov ecx,DWORD PTR [r14-0x2c]
mov r12,QWORD PTR [r15+0x20]
add edi,eax
add eax,0x1
lea edi,[rdi+rcx*1+0x2]
mov rcx,QWORD PTR [rsi]
mov QWORD PTR [rcx+rdi*8],rdx
mov DWORD PTR [r14-0x24],eax
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
mov BYTE PTR [rbp-0x5c0],sil
cmp sil,0xfe
jne <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE+X>
mov rsi,QWORD PTR [r13+0x8]
sub rsi,rdx
xchg ax,ax
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
mov BYTE PTR [rbp-0x5c0],dil
movzx eax,BYTE PTR [rbp-0x5c0]
lea rdx,[rip+0x180e41] # <_ZZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextEE7kLabels>
jmp QWORD PTR [rdx+rax*8]
