# Project1:
## 地址空间
本实验当前阶段各关键内存/镜像布局（物理地址）说明：

| 区域/符号 | 物理地址 | 大小 / 说明 | 产生方式 |
|-----------|----------|-------------|----------|
| BIOS 功能入口 (bios_func_entry) | 0x50150000 | 固定（课程环境提供） | 通过 `jalr` 调用 BIOS API |
| Bootblock (第 0 扇区) | 0x50200000 | 512B | QEMU/U-Boot 读取到内存并执行 |
| os_size_loc | 0x502001FC | 2 字节 (uint16, 单位=扇区) | createimage 在镜像末 4B 中写入，bootblock 用 `lh` 读取 |
| Kernel 加载基址 (kernel) | 0x50201000 | 若干扇区（os_size 指定） | bootblock 通过 BIOS_SDREAD 搬运 |
| `_start` (head.S) | 0x50201000 | 内核入口 (.entry_function 放在 .text 前) | 链接脚本 `TEXT_START` |
| 内核栈顶 (KERNEL_STACK) | 0x50500000 | 向下增长 | head.S: `la sp, KERNEL_STACK` |

## 任务1：第一个引导块的制作
`msg_print_boot`定义：
```asm
.data
msg_print_boot: .string "It's kouyixin's bootblock...\n\r"
```

使用 BIOS_PUTSTR 功能号，通过 BIOS 打印字符串 "It's kouyixin's bootblock...":
```asm
...
la a0, msg_print_boot    # 加载字符串地址到 a0
li a7, BIOS_PUTSTR       # 功能号：BIOS_PUTSTR
la t0, bios_func_entry   # BIOS 功能入口地址
jalr t0                  # 跳转到 BIOS
...
```
注意：关于 API 规则可以参看 [common.c](./arch/riscv/bios/common.c) 文件。

## 任务2：加载和初始化内存

### [bootblock.S](./arch/riscv/boot/bootblock.S)
将起始于 SD 卡第二个扇区的 kernel 代码段移动至内存并跳转到 kernel：
```asm
...
la a0, kernel            # 目标内存地址
la t1, os_size_loc
lh a1, 0(t1)             # 读取 OS 大小
li a2, 1                 # 起始块号
li a7, BIOS_SDREAD       # 功能号
la t0, bios_func_entry
jalr t0

la t0, kernel
jalr t0
...
```
需要注意的是，内存扇区是从 0 开始计数，所以第二扇区使用的起始块号是 1，笔者在此卡住很久 QAQ。

对于读取 OS 大小，如果使用 `lw` 会报错 "size 太大超出扇区的范围"，这是因为 `os_size_loc` 位于镜像的第一个扇区的倒数第 4 个字节（物理地址 `0x502001FC`），它存储的是 2 字节的扇区数（单位为扇区）。因此，必须使用 `lh` 指令读取这 2 字节数据，确保读取到的值正确：

---

### [head.S](./arch/riscv/kernel/head.S)
清空 BSS 段，设置栈指针，跳转到内核 main 函数：
```asm
...
  la t0, __bss_start
  la t1, __BSS_END__
  bge t0, t1, set_stack  
clear_bss:
  sw zero, 0(t0)
  addi t0, t0, 4
  blt t0, t1, clear_bss

  /* TODO: [p1-task2] setup C environment */
set_stack:
  la sp, KERNEL_STACK

step_main:
  jal ra, main
...
```

---

### [main.c](./init/main.c)
打印 "bss check: t version: X" 之后，调用跳转表 API 读取键盘输入，并回显到屏幕上：
```C
...
while (1)
{   
    int ch = bios_getchar();
    if (ch != -1) {
        bios_putchar(ch);
    }
    // asm volatile("wfi");
}
...
```
如果键盘没有任何键被按下，会返回-1；如果有某个键被按下，则返回对应的 ASCII 码。因此，在做键盘输入相关的动作时需要处理掉-1 的情况，避免将-1当作真正的输入直接使用，否则屏幕上会看到很奇怪的输出（持续输出一个乱码）。




