endbr64
mov rax,QWORD PTR [r15+0x20]
mov edi,DWORD PTR [rax-0x24]
test edi,edi
je <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE.cold+X>
mov rdx,QWORD PTR [rax-0x40]
mov rcx,QWORD PTR [rdx+0xd0]
mov rdx,QWORD PTR [rdx+0xd8]
sub rdx,rcx
sar rdx,0x3
cmp rbx,rdx
jae <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE.cold+X>
lea r8,[rcx+rbx*8]
cmp WORD PTR [r8+0x4],0x0
jne <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE+X>
mov rsi,QWORD PTR [r15+0x30]
mov ecx,DWORD PTR [rax-0x30]
mov rdx,QWORD PTR [rsi]
lea r9d,[rcx+0x1]
mov rdx,QWORD PTR [rdx+r9*8]
test rdx,rdx
je <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE+X>
cmp rdx,0x141
je <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE+X>
mov rdx,QWORD PTR [r15+0x10]
mov r12d,0x0
lea r9d,[rdi-0x1]
movzx edx,BYTE PTR [rdx]
mov WORD PTR [rax-0xe],r12w
mov DWORD PTR [rax-0xc],r9d
test dl,dl
mov QWORD PTR [rax-0x8],rbx
sete BYTE PTR [rax-0x10]
jne <_ZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextE+X>
mov r12,QWORD PTR [r15+0x20]
mov edx,0x1
mov WORD PTR [rax-0xe],dx
mov rax,QWORD PTR [r15+0x18]
cmp rax,r12
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
nop DWORD PTR [rax+0x0]
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
lea rdx,[rip+0x161e01] # <_ZZN7protoST15ExecutionEngine7runLoopEPN5proto12ProtoContextEE7kLabels>
jmp QWORD PTR [rdx+rax*8]
