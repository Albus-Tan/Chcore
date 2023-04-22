# Lab4

> 姓名：谈子铭
>
> 学号：520021910607

> 思考题 1：阅读汇编代码`kernel/arch/aarch64/boot/raspi3/init/start.S`。说明ChCore是如何选定主CPU，并阻塞其他其他CPU的执行的。

```assembly
BEGIN_FUNC(_start)
	mrs	x8, mpidr_el1
	and	x8, x8,	#0xFF
	cbz	x8, primary

	/* Wait for bss clear */
wait_for_bss_clear:
	adr	x0, clear_bss_flag
	ldr	x1, [x0]
	cmp     x1, #0
	bne	wait_for_bss_clear

	/* Turn to el1 from other exception levels. */
	bl 	arm64_elX_to_el1

	/* Prepare stack pointer and jump to C. */
	......
wait_until_smp_enabled:
	/* CPU ID should be stored in x8 from the first line */
	mov	x1, #8
	mul	x2, x8, x1
	ldr	x1, =secondary_boot_flag
	add	x1, x1, x2
	ldr	x3, [x1]
	cbz	x3, wait_until_smp_enabled

	/* Set CPU id */
	mov	x0, x8
	bl 	secondary_init_c

primary:
	......
```

```c++
/*
 * Non-primary CPUs will spin until they see the secondary_boot_flag becomes
 * non-zero which is set in kernel (see enable_smp_cores).
 *
 * The secondary_boot_flag is initialized as {NOT_BSS, 0, 0, ...}.
 */
long secondary_boot_flag[PLAT_CPU_NUMBER] = {NOT_BSS};
```

进入 `_start` 函数后 `mrs x8, mpidr_el1` 将当前 cpuid 放在了 `x8` 寄存器中，并在 `cbz x8, primary` 中通过判断当前 cpuid 是否为 0 来选择主 CPU，即 cpuid 为 0 的 CPU 是主 CPU 并跳转执行 `primary` 处的代码，其他核心则会在 `wait_for_bss_clear` 处循环，等待 `clear_bss` 将 `clear_bss_flag` 置0后才继续执行。

当 0 号主 CPU 在 `init_c` 中调用 `clear_bss` 后其余核继续执行，将异常级别降低并设置栈后，继续在 `wait_until_smp_enabled` 循环，等待 `secondary_boot_flag` 对应位置变为非0。在终止等待之后设置 cpuid 并运行 `secondary_init_c` 从而激活该CPU。  

Chcore 通过控制 `secondary_boot_flag` 数组中其他 CPU 对应元素始终为 0 而让其他 CPU 不断循环等待来阻塞其他 CPU 执行。具体而言，其他 CPU 循环等待直至 `secondary_boot_flag[cpuid] != 0` 时才跳转到 `secondary_init_c` 进行初始化。主 CPU 在完成自己的初始化后调用 `enable_smp_cores`，在此设置 `secondary_boot_flag[cpuid] = 1`，让副 CPU 可以继续执行完成初始化。而每个副 CPU 初始化完成后会设置 `cpu_status[cpuid] = cpu_run`，因此只有只有在上个设置好后才可以设置下个副 CPU 的 `secondary_boot_flag[cpuid]`，保证了副 CPU 有序逐个的初始化。

> 思考题 2：阅读汇编代码`kernel/arch/aarch64/boot/raspi3/init/start.S, init_c.c`以及`kernel/arch/aarch64/main.c`，解释用于阻塞其他CPU核心的`secondary_boot_flag`是物理地址还是虚拟地址？是如何传入函数`enable_smp_cores`中，又该如何赋值的（考虑虚拟地址/物理地址）？

```c++
/* init_c.c */
/*
 * The secondary_boot_flag is initialized as {NOT_BSS, 0, 0, ...}
 */
# define NOT_BSS (0xBEEFUL)
long secondary_boot_flag[PLAT_CPU_NUMBER] = {NOT_BSS};
void init_c(void)
{
    ......
    start_kernel(secondary_boot_flag);
}
```

```assembly
/* head.S */
/* Args in x0 (boot_flag) should be passed to main */
BEGIN_FUNC(start_kernel)
    /* Save x0 */
    str     x0, [sp, #-8]!
	......
    /* Restore x0 */
    ldr     x0, [sp], #8

    bl      main
END_FUNC(start_kernel)
```

```c++
/* main.c */
void main(paddr_t boot_flag)
{
    ......
    /* Other cores are busy looping on the addr, wake up those cores */
    enable_smp_cores(boot_flag);
    ......
}
```

```c++
/* smp.c */
void enable_smp_cores(paddr_t boot_flag)
{
        int i = 0;
        long *secondary_boot_flag;

        /* Set current cpu status */
        cpu_status[smp_get_cpu_id()] = cpu_run;
        secondary_boot_flag = (long *)phys_to_virt(boot_flag);
    	......
}
```

`init_c ` 中将 `secondary_boot_flag `传入 `start_kernal` 中，`start_kernal` 又通过设置 `x0` 寄存器的值将它传入 `main` 中，`main` 再将它传入 `enable_smp_cores ` 中。

`secondary_boot_flag` 是物理地址，因为此时 `secondary CPU` 的 `MMU` 还没有初始化，仍然在使用物理地址。通过 `main` 函数传参传入了物理地址 `boot_flag`，并在 `enable_smp_cores` 函数中通过 `secondary_boot_flag = (long*)phys_to_virt(boot_flag)` 从物理地址转换成虚拟地址，然后通过 `secondary_boot_flag[i] = 1` 进行相关赋值操作，最后调用 `flush_dcache_area` 将修改的内容flush掉。

> 练习题 4：本练习分为以下几个步骤：
>
> 1. 请熟悉排号锁的基本算法，并在`kernel/arch/aarch64/sync/ticket.c`中完成`unlock`和`is_locked`的代码。
> 2. 在`kernel/arch/aarch64/sync/ticket.c`中实现`kernel_lock_init`、`lock_kernel`和`unlock_kernel`。
> 3. 在适当的位置调用`lock_kernel`。
> 4. 判断什么时候需要放锁，添加`unlock_kernel`。（注意：由于这里需要自行判断，没有在需要添加的代码周围插入TODO注释）。
>

在处理完异常返回时放锁：

```assembly
/* kernel/arch/aarch64/irq/irq_entry.S */
sync_el0_64:
	...
	bl unlock_kernel
	exception_exit
	...
el0_syscall:
	...
	bl lock_kernel
	...
	bl unlock_kernel
	exception_exit
	...
BEGIN_FUNC(__eret_to_thread)
	mov	sp, x0
	dmb ish
	bl unlock_kernel
	exception_exit
END_FUNC(__eret_to_thread)
```

> 思考题 5：在`el0_syscall`调用`lock_kernel`时，在栈上保存了寄存器的值。这是为了避免调用`lock_kernel`时修改这些寄存器。在`unlock_kernel`时，是否需要将寄存器的值保存到栈中，试分析其原因。

不需要。调用 `lock_kernel` 时保存调用者保存寄存器主要是因为寄存器中有调用 syscall 对应的函数的参数，不能覆盖。调用 `unlock_kernel` 时，syscall 已经执行完成，之后将继续执行 `exception_exit` ，会从内核栈中恢复用户态的寄存器并回归到用户态。这个过程中不会再使用这些寄存器，故不需要保存。

> 思考题 6：为何`idle_threads`不会加入到等待队列中？请分析其原因？

`idle_threads` 是每个CPU核心上的空闲线程，作用仅仅是在没有线程等待时上去顶替，这样做可以防止 CPU 核心没有要调度的线程时在内核态忙等（这样它持有的大内核锁将锁住整个内核，导致卡住），其本质并非是想要交由内核调度运行的可执行线程，不应被分配时间片，也就不应该加入到等待队列中接受调度。而如果让 `idle_threads` 进入等待队列，由于我们现在使用的是 RR 策略，因此所有在等待队列中的进程都拥有相同的优先级，就会“空转”浪费很多时间和资源。

> 思考题 8：如果异常是从内核态捕获的，CPU核心不会在`kernel/arch/aarch64/irq/irq_entry.c`的`handle_irq`中获得大内核锁。但是，有一种特殊情况，即如果空闲线程（以内核态运行）中捕获了错误，则CPU核心还应该获取大内核锁。否则，内核可能会被永远阻塞。请思考一下原因。

- 因为运行在内核态的空闲线程运行时并没有拿到大内核锁，但是 `handle_irq` 在返回时会`eret_to_thread(switch_context())` 而导致释放大内核锁（`lock->owner++`）。在没有拿大内核锁的情况下释放大内核锁，可能会导致排号锁的 `owner > next` ，导致整个排号锁以及调度都出错。
- 空闲线程的作用就是为了防止 CPU 发现没有能调度的线程而卡在内核态，所以当空闲线程出现了错误时应该获取大内核锁并由内核去解决错误，如果忽视这个错误很有可能导致空闲线程挂掉，之后 CPU 再次调度时如果发现没有能调度的线程时就会永远阻塞。
