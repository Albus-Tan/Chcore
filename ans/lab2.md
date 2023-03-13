# Lab2

> 姓名：谈子铭
>
> 学号：520021910607

> 思考题 1：请思考多级页表相比单级页表带来的优势和劣势（如果有的话），并计算在 AArch64 页表中分别以 4KB 粒度和 2MB 粒度映射 0～4GB 地址范围所需的物理内存大小（或页表页数量）。

**多级页表相比单级页表带来的优势和劣势**

优势：

1. 有效压缩页表的大小。
2. 节约内存。允许页表中出现空洞（某级页表中的某条目为空，那么该条目对应的下一级页表无需存在；应用程序的虚拟地址空间大部分都未分配）
3. 只有一级页表才需要总是在主存中，操作系统可以在需要时创建，调入调出二级页表，这减少了主存的压力。

劣势：

1. 多级页表的寻址速度较慢，因为需要多次访问不同的页表来获取物理地址，寻址次数增加，访存时间变长。而单级页表只需要访问一次即可得到物理地址。
2. 多级页表的实现较为复杂，需要更多的硬件支持和软件实现。

**计算在 AArch64 页表中分别以 4KB 粒度和 2MB 粒度映射 0～4GB 地址范围所需的物理内存大小（或页表页数量）**

> 4KB = 2^12 B
>
> 2MB = 2^21 B
>
> 4GB = 2^32 B
>
> 每一级的每一个页表占用一个 4KB 物理页，称为页表页（Page Table Page），其中有 512 个条目，每个条目占 64 位。

**以 4KB 粒度映射时：**

需要用来实际存放数据的页数量：2^32 / 2^12 = 2^20

一个页表中可以存放的页表项条目数量：2^12 / 2^3 = 2^9

需要的 L3 级页表数量：2^20 / 2^9 = 2^11

需要的 L2 级页表数量：2^11 / 2^9 = 2^2

需要的 L1 级页表数量：1

需要的 L0 级页表数量：1

总共需要的页表页数量 1+1+4+2048 = 2054

所需物理内存大小：2054 * 4k bytes = 8Mb

**以 2MB 粒度映射时：**

需要用来实际存放数据的页数量：2^32 / 2^21 = 2^11

一个页表中可以存放的页表项条目数量：2^12 / 2^3 = 2^9 = 512

以 2MB 粒度映射，不需要 L3 级页表

需要的 L2 级页表数量：2^11 / 2^9 = 2^2

需要的 L1 级页表数量：1

需要的 L0 级页表数量：1

总共需要的页表页数量 1+1+4 = 6

所需物理内存大小：6 * 4k bytes = 24 kb

> 练习题 2：请在 `init_boot_pt` 函数的 `LAB 2 TODO 1` 处配置内核高地址页表（`boot_ttbr1_l0`、`boot_ttbr1_l1` 和 `boot_ttbr1_l2`），以 2MB 粒度映射。

```c++
/* LAB 2 TODO 1 BEGIN */
vaddr = KERNEL_VADDR + PHYSMEM_START;
/* Step 1: set L0 and L1 page table entry */
boot_ttbr1_l0[GET_L0_INDEX(vaddr)] = ((u64)boot_ttbr1_l1) | IS_TABLE
                                     | IS_VALID | NG;
boot_ttbr1_l1[GET_L1_INDEX(vaddr)] = ((u64)boot_ttbr1_l2) | IS_TABLE
                                     | IS_VALID | NG;

/* Step 2: map PHYSMEM_START ~ PERIPHERAL_BASE with 2MB granularity */
for (; vaddr < KERNEL_VADDR + PERIPHERAL_BASE; vaddr += SIZE_2M) {
        boot_ttbr1_l2[GET_L2_INDEX(vaddr)] =
                (vaddr - KERNEL_VADDR)
                | UXN /* Unprivileged execute never */
                | ACCESSED /* Set access flag */
                | NG /* Mark as not global */
                | INNER_SHARABLE /* Sharebility */
                | NORMAL_MEMORY /* Normal memory */
                | IS_VALID;
}

/* Step 2: map PERIPHERAL_BASE ~ PHYSMEM_END with 2MB granularity */
for (vaddr = KERNEL_VADDR + PERIPHERAL_BASE; vaddr < KERNEL_VADDR + PHYSMEM_END; vaddr += SIZE_2M) {
        boot_ttbr1_l2[GET_L2_INDEX(vaddr)] =
                (vaddr - KERNEL_VADDR)
                | UXN /* Unprivileged execute never */
                | ACCESSED /* Set access flag */
                | NG /* Mark as not global */
                | DEVICE_MEMORY /* Device memory */
                | IS_VALID;
}
/* LAB 2 TODO 1 END */
```

> 思考题 3：请思考在 `init_boot_pt` 函数中为什么还要为低地址配置页表，并尝试验证自己的解释。

因为此时`init_c`，`el1_mmu_activate`等还运行在低地址端，如果不配置低地址段的页表，启动MMU后原有函数的返回地址（即低地址）无法正确映射。

将为低地址配置页表的代码片段注释，利用 `gdb` 运行程序观察得到，MMU无法翻译低地址，又在0x200地址出无限重复跳转。

> 思考题 4：请解释 `ttbr0_el1` 与 `ttbr1_el1` 是具体如何被配置的，给出代码位置，并思考页表基地址配置后为何需要ISB指令。

在 `tools.S` 文件 `el1_mmu_activate` 函数中。

> adrp <Rd>, <label>
>
> 将label所在页且4KB对其的页基地址放入寄存器Rd中。Label表示的地址肯定在这个页基地址确定的页内。
>
> 由于内核启动的时候MMU还未打开，此时获取的在内存中的起始页地址为物理地址。

```assembly
/* Write ttbr with phys addr of the translation table */
adrp    x8, boot_ttbr0_l0
msr     ttbr0_el1, x8	/* 使用用户低地址零级页表的物理首地址 boot_ttbr0_l0 初始化 ttbr0_el1 */
adrp    x8, boot_ttbr1_l0
msr     ttbr1_el1, x8	/* 使用内核高地址零级页表的物理首地址 boot_ttbr1_l0 初始化 ttbr1_el1 */
isb
```

这样页表的物理地址就被写入了 `ttbr0_el1` 与 `ttbr1_el1` 。

**页表基地址配置后为何需要ISB指令**

> **指令同步屏障（Instruction Synchronization Barrier，ISB）指令：**确保所有在ISB指令之后的指令都从指令高速缓存或内存中重新预取。它刷新流水线（flush pipeline）和预取缓冲区后才会从指令高速缓存或者内存中预取ISB指令之后的指令。ISB指令通常用来保证上下文切换（如ASID更改、TLB维护操作等）的效果。

因为在页表基地址配置指令在流水线中完全完成之前，之后的指令可能已经发出了进行了内存访问的请求（在流水线中），内存访问时会使用到页表基地址，这些请求仍旧使用着旧的页表基地址，可能会导致不可预测的行为。

使用ISB指令会确保其之前的指令完全执行完（走完流水线）；这样在ISB之后的访存指令使用到的一定是更新后的页表基地址，保证地址转换的正确。

> 练习题 5：完成 `kernel/mm/buddy.c` 中的 `split_page`、`buddy_get_pages`、`merge_page` 和 `buddy_free_pages` 函数中的 `LAB 2 TODO 2` 部分，其中 `buddy_get_pages` 用于分配指定阶大小的连续物理页，`buddy_free_pages` 用于释放已分配的连续物理页。
>
> 提示：
>
> - 可以使用 `kernel/include/common/list.h` 中提供的链表相关函数如 `init_list_head`、`list_add`、`list_del`、`list_entry` 来对伙伴系统中的空闲链表进行操作
> - 可使用 `get_buddy_chunk` 函数获得某个物理内存块的伙伴块
> - 更多提示见代码注释

实现时需要注意 `merge_page` 时如果找到的 buddy 起始地址更小（buddy 在 page 前面），应当使用找到的 buddy 作为对应 page 的代表（起始地址）。同时 `merge_page` 时应当注意 page 的 order 到最大时不再尝试寻找其 buddy。

详细实现见代码，结果如下：

```shell
......
[INFO] [ChCore] uart init finished
[TEST] Jump to kernel high memory: OK
[INFO] physmem_map: [0xc0000, 0x3d000000)
[TEST] Init buddy: OK
[TEST] Check invalid order: OK
[TEST] Allocate & free order 0: OK
[TEST] Allocate & free each order: OK
[TEST] Allocate & free all orders: OK
[TEST] Allocate & free all memory: OK
[TEST] Buddy tests finished
[INFO] [ChCore] mm init finished
[TEST] kmalloc: OK
......
```

> 练习题 6：完成 `kernel/arch/aarch64/mm/page_table.c` 中的 `get_next_ptp`、 `query_in_pgtbl`、`map_range_in_pgtbl`、`unmap_range_in_pgtbl` 函数中的 `LAB 2 TODO 3` 部分，后三个函数分别实现页表查询、映射、取消映射操作，其中映射和取消映射以 4KB 页为粒度。
>
> 提示：
>
> - 暂时不用考虑 TLB 刷新，目前实现的只是页表作为内存上的数据结构的管理操作，还没有真的设置到页表基址寄存器（TTBR）
> - 实现中可以使用 `get_next_ptp`、`set_pte_flags`、`GET_LX_INDEX` 等已经给定的函数和宏
> - 更多提示见代码注释

详细实现见代码

> 练习题 7：完成 `kernel/arch/aarch64/mm/page_table.c` 中的 `map_range_in_pgtbl_huge` 和 `unmap_range_in_pgtbl_huge` 函数中的 `LAB 2 TODO 4` 部分，实现大页（2MB、1GB 页）支持。
>
> 提示：可假设取消映射的地址范围一定是某次映射的完整地址范围，即不会先映射一大块，再取消映射其中一小块。

详细实现见代码

> 思考题 8：阅读 Arm Architecture Reference Manual，思考要在操作系统中支持写时拷贝（Copy-on-Write，CoW）[^cow]需要配置页表描述符的哪个/哪些字段，并在发生缺页异常（实际上是 permission fault）时如何处理。

> 思考题 9：为了简单起见，在 ChCore 实验中没有为内核页表使用细粒度的映射，而是直接沿用了启动时的粗粒度页表，请思考这样做有什么问题。

> 挑战题 10：使用前面实现的 `page_table.c` 中的函数，在内核启动后重新配置内核页表，进行细粒度的映射。