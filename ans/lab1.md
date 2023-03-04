# Lab1

> 姓名：谈子铭
>
> 学号：520021910607

> 思考题 1：阅读 `_start` 函数的开头，尝试说明 ChCore 是如何让其中一个核首先进入初始化流程，并让其他核暂停执行的。
>
> 提示：可以在 [Arm Architecture Reference Manual](https://documentation-service.arm.com/static/61fbe8f4fa8173727a1b734e) 找到 `mpidr_el1` 等系统寄存器的详细信息。

ARM架构使用 `mpidr_el1 `寄存器，来标识 core，每个核心会使用不同的标号来识别。

> `mpidr_el1` 系统寄存器的0至7位会存储；Affinity level 0. The level identifies individual threads within a multithreaded core.

```assembly
BEGIN_FUNC(_start)
   mrs    x8, mpidr_el1	/* 将 mpidr_el1 中存储的 core ID 移动到寄存器 x8 */
   and    x8, x8, #0xFF	/* mask */
   cbz    x8, primary	/* 将寄存器 x8 中的值与 0 比较，如果为 0 便跳转至 primary */
   /* hang all secondary processors before we introduce smp */
   b 	.	/* b .表示一直在当前指令处原地跳转，也就是死循环 */

primary:
	/* Turn to el1 from other exception levels. */
   ......
```

也即只有 core ID 为 0 的 cpu 核心会跳转到 primary 的部分并执行，也即进入初始化流程，其余核心都不会执行 primary，而会被 `b .` 指令 loop 住，暂停进一步执行。

> 练习题 2：在 `arm64_elX_to_el1` 函数的 `LAB 1 TODO 1` 处填写一行汇编代码，获取 CPU 当前异常级别。
>
> 提示：通过 `CurrentEL` 系统寄存器可获得当前异常级别。通过 GDB 在指令级别单步调试可验证实现是否正确。

```assembly
BEGIN_FUNC(arm64_elX_to_el1)
   /* LAB 1 TODO 1 BEGIN */
   mrs x9, CurrentEL   /* 将 CurrentEL 中存储的当前异常级别移动到寄存器 x9 */
   /* LAB 1 TODO 1 END */

   // Check the current exception level.
   cmp x9, CURRENTEL_EL1
   beq .Ltarget
   cmp x9, CURRENTEL_EL2
   beq .Lin_el2
   // Otherwise, we are in EL3.
   ......
```

`mrs x9, CurrentEL` 通过 `CurrentEL` 系统寄存器获得当前异常等级，将值移动到通用寄存器 x9 中。通过 GDB 调试发现此时 CPU 异常级别为 EL3。

> 练习题 3：在 `arm64_elX_to_el1` 函数的 `LAB 1 TODO 2` 处填写大约 4 行汇编代码，设置从 EL3 跳转到 EL1 所需的 `elr_el3` 和 `spsr_el3` 寄存器值。具体地，我们需要在跳转到 EL1 时暂时屏蔽所有中断、并使用内核栈（`sp_el1` 寄存器指定的栈指针）。

```assembly
......
// Otherwise, we are in EL3.

/* 设置当前级别的控制寄存器，以控制低一级别的执行状态等行为 */
// Set EL2 to 64bit and enable the HVC instruction.
mrs x9, scr_el3
mov x10, SCR_EL3_NS | SCR_EL3_HCE | SCR_EL3_RW
orr x9, x9, x10
msr scr_el3, x9

/* 设置 `elr_elx`（异常链接寄存器）和 `spsr_elx`（保存的程序状态寄存器），分别控制异常返回后执行的指令地址，和返回后应恢复的程序状态（包括异常返回后的异常级别） */
/* LAB 1 TODO 2 BEGIN */
adr x9, .Ltarget
msr elr_el3, x9	/* 将 x9 中存储的值移动到 EL3 的 exception link register，也即设置返回地址为 Ltarget */
mov x9, SPSR_ELX_DAIF | SPSR_ELX_EL1H	/* 设置异常返回后的exception level和栈指针 EL1H，以及屏蔽中断 DAIF（D: debug; A: error; I: interrupt; F: fast interrupt） */
msr spsr_el3, x9	/* 设置状态寄存器 SPSR */
/* LAB 1 TODO 2 END */
......
/* 调用 `eret` 指令，进行异常返回 */
```

之后从 EL3 切换至 EL1。通过 GDB 跟踪内核代码的执行过程，发现正确从 `arm64_elX_to_el1` 函数返回到 `_start` ，代码正确。

> 思考题 4：结合此前 ICS 课的知识，并参考 `kernel.img` 的反汇编（通过 `aarch64-linux-gnu-objdump -S` 可获得），说明为什么要在进入 C 函数之前设置启动栈。如果不设置，会发生什么？

通过 `aarch64-linux-gnu-objdump -S` 获得`kernel.img` 的反汇编的 `init_c` 部分

```objective-c
0000000000088398 <init_c>:
   88398:       a9bf7bfd        stp     x29, x30, [sp, #-16]!
   ......
   883ec:       d503201f        nop
   883f0:       a8c17bfd        ldp     x29, x30, [sp], #16
   883f4:       d65f03c0        ret
```

C函数调用时候会正常进行传参压栈等（调用者和被调用者保存寄存器等）。为了调用C必须先初始化栈，这样才能正常进行函数调用，也即在此之后就可以抛弃汇编使用C。进入 `init_c` 后，函数会保存stack pointer和link  pointer以便结束后恢复并返回至原函数。这些寄存器都是保存在栈上的，如果没有初始化 `sp`，那么 `sp` 中就是一个随机值，往未知的的地址存数据会导致UNDEFINED behavior或者带来错误。

> 进入 `init_c` 函数后，第一件事首先通过 `clear_bss` 函数清零了 `.bss` 段，该段用于存储未初始化的全局变量和静态变量（具体请参考附录）。
>
> 思考题 5：在实验 1 中，其实不调用 `clear_bss` 也不影响内核的执行，请思考不清理 `.bss` 段在之后的何种情况下会导致内核无法工作。

`.bss` 段存放未初始化的全局变量和静态变量，由于没有初始值，因此在 ELF 中不需要真的为该分段分配空间，而是只需要记录目标内存地址和大小，在加载时需要初始化为 0。

正常情况下bss段（未初始化的静态和全局变量，c默认是将其全部初始化为0）被加载到内存中时会由操作系统初始化成0，但因为本身就在写操作系统，所以就需要自己进行这一步初始化。

不清理的话bss段中这些静态变量和全局变量就会带有随机的初始值，这会导致UNDEFINED behavior或者错误。

> 练习题 6：在 `kernel/arch/aarch64/boot/raspi3/peripherals/uart.c` 中 `LAB 1 TODO 3` 处实现通过 UART 输出字符串的逻辑。

```c
void uart_send_string(char *str)
{
        /* LAB 1 TODO 3 BEGIN */
        while(str[0] != '\0'){
                early_uart_send(str[0]);
                ++str;
        }
        early_uart_send('\0');
        /* LAB 1 TODO 3 END */
}
```

> 练习题 7：在 `kernel/arch/aarch64/boot/raspi3/init/tools.S` 中 `LAB 1 TODO 4` 处填写一行汇编代码，以启用 MMU。

```assembly
/* Enable MMU */
/* LAB 1 TODO 4 BEGIN */
orr     x8, x8, #SCTLR_EL1_M
/* LAB 1 TODO 4 END */
/* Disable alignment checking */
bic     x8, x8, #SCTLR_EL1_A
```

通过配置 `M` 字段启用 MMU