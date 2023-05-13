# Lab6

> 姓名：谈子铭
>
> 学号：520021910607

> 思考题 1：请自行查阅资料，并阅读`userland/servers/sd`中的代码，回答以下问题:
>
> - circle中还提供了SDHost的代码。SD卡，EMMC和SDHost三者之间的关系是怎么样的？
> - 请**详细**描述Chcore是如何与SD卡进行交互的？即Chcore发出的指令是如何输送到SD卡上，又是如何得到SD卡的响应的。(提示: IO设备常使用MMIO的方式映射到内存空间当中)
> - 请简要介绍一下SD卡驱动的初始化流程。
> - 在驱动代码的初始化当中，设置时钟频率的意义是什么？为什么需要调用`TimeoutWait`进行等待?

**SD卡，EMMC和SDHost三者之间的关系是怎么样的？**

- **SD卡**（Secure Digital Card）是一种**可移动存储设备**，可以插入到支持SD卡接口的设备中，用于在便携式设备中存储数据
- **EMMC**（Embedded MultiMediaCard）是一种集成了控制器和闪存芯片的**嵌入式存储设备**，它的接口与SD卡相同，但通常具有更高的存储容量和更快的读写速度
- **EMMC控制器**是一种集成了闪存存储和控制器功能的存储器解决方案。它负责管理和控制EMMC存储设备，包括数据传输、存储管理和错误处理等任务。EMMC控制器使用SD协议与主机设备通信，并通过该协议与SDHost进行交互
- **SDHost**是嵌入在主机设备（如计算机、嵌入式系统）中的主机控制器，用于与SD卡进行通信。SDHost实现了SD协议的功能，包括初始化SD卡、发送读写命令、接收数据和错误处理等。SDHost控制器与SD卡之间通过物理接口进行连接。

SD卡和EMMC是实际的存储设备。SDHost控制器和EMMC控制器均用于提供和SD卡进行交互的底层接口和功能。Chcore中的SD卡驱动使用EMMC控制器提供的接口实现对SD卡的内容读写操作。

**Chcore是如何与SD卡进行交互的？**

在Chcore中，与SD卡的交互是通过MMIO（Memory Mapped I/O）方式实现的。MMIO是一种将I/O设备映射到内存地址空间的技术，使得CPU可以像访问内存一样访问I/O设备。

Chcore通过以下步骤与SD卡进行交互：

1. 初始化：Chcore调用 `map_mmio` 将寄存器映射到操作系统的内存空间中。使得操作系统可以像访问内存一样直接读写这些寄存器。
2. 写指令：当Chcore需要向SD卡发送指令时，它会将指令的数据和参数写入到预先映射的特定SDHost寄存器中。这些寄存器被映射到内存空间的特定地址范围，使得CPU可以通过内存访问的方式写入数据。具体的，在 `emmc.c/IssueCommandInt` 函数中，通过读写内存的接口将需要设置的寄存器的值写入对应的内存空间地址，从而写入设备的参数寄存器、指令寄存器和块数寄存器中。
3. 发送指令：Chcore通过向SDHost的寄存器写入特定的命令代码来触发指令的发送。SDHost会根据写入的命令代码执行相应的操作，将指令传递给SD卡。
4. SD卡响应：SD卡接收到指令后会执行相应的操作，并将响应数据返回给SDHost。SDHost会将SD卡的响应数据存储在预先映射的寄存器中，等待Chcore读取。
5. 读取响应：Chcore通过从预先映射的寄存器中读取响应数据，获取SD卡对指令的响应。发送指令后，操作系统等待SD卡的响应。SD卡控制器通常会通过中断信号或者轮询方式来通知操作系统指令的执行结果。具体的，在 `emmc.c/IssueCommandInt` 函数中，通过轮询的方式等待 `EMMC_INTERRUPT` 寄存器中对应的bit值被置为1，说明处理完成。再从对应的寄存器中通过内存读的接口在对应寄存器中拿到响应数据。Chcore可以根据响应数据来判断指令执行的结果，并采取相应的处理措施。

整个过程中，Chcore通过将SDHost的寄存器映射到内存空间中的特定地址范围，使得CPU可以通过内存访问的方式与SDHost进行通信。Chcore将指令和数据写入到相应的寄存器中，触发SDHost向SD卡发送指令，并等待中断/轮询查看条件，等SD卡处理完成后，从寄存器中读取SD卡的响应。这种基于MMIO的方式使得Chcore能够与SD卡进行高效的交互，并进行数据的读写操作。

**请简要介绍一下SD卡驱动的初始化流程**

`emmc.c` 文件 `CardInit` 函数

1. 首先调用 `PowerOn()` 函数来执行SD卡的上电操作。如果上电失败，返回-1表示初始化失败。
2. 获取版本号并进行版本检查。
3. 循环执行重置SD卡的 `CardReset()` 函数。

`emmc.c` 文件 `CardReset()` 函数

1. 检查是否定义了 `USE_SDHOST` 宏，如果没有定义 `USE_SDHOST` 宏，则执行其中的代码块
2. 根据树莓派版本执行一些操作
3. 检查是否插入了有效的SD卡，通过`TimeoutWait`检查`EMMC_STATUS`寄存器状态。
4. 获取基本时钟频率，如果无法获取，则将基本时钟频率设置为默认值；设置时钟频率为较慢的速度（通过配置 `EMMC_CONTROL1` 寄存器的相关位实现），之后启用时钟，并通过`TimeoutWait(EMMC_CONTROL1, 2, 1, 1000000) `等待时钟在1s以内稳定
5. 配置中断掩码，禁止发送中断到ARM，把所有中断发送到 `INTERRUPT` 寄存器
6. 初始化设备结构的相关变量
7. 发送一系列初始化命令与SD卡进行通信，发送CMD0命令将SD卡复位到空闲状态。发送CMD8命令检查卡的电压和供电条件。根据CMD5的响应判断卡的类型和版本。最后发送ACMD41命令进行卡初始化，并等待初始化完成。
8. 获取卡片CID、发送CMD3命令进入数据状态、选择卡片、设置块长度等

**在驱动代码的初始化当中，设置时钟频率的意义是什么？为什么需要调用`TimeoutWait`进行等待?**

在驱动代码的初始化中，设置时钟频率的意义是确保与外部设备进行通信时的时序一致性和正确性。时钟频率决定了设备内部操作的速度和时间间隔，对于各种设备和总线接口来说都非常重要。

通过设置适当的时钟频率，可以确保设备在正常工作范围内运行，并与其他设备或系统组件同步。例如，对于一些外部设备接口如串行通信接口（UART）、SPI（串行外设接口）或 I2C（双线串行总线），设置正确的时钟频率可以确保数据传输的稳定性和正确性。SD卡在复位以及初始化操作时应当在较低工作频率工作，等切换至读写模式时才能以最大时钟频率工作。

调用 `TimeoutWait` 是为了等待设备的时钟和状态稳定。在SD卡的初始化过程中，有些操作可能需要一定的时间来完成，发送命令后需要等待SD卡的响应，以确保命令被成功接收和执行。使用 `TimeoutWait` 函数可以设置一个最大等待时间，如果超过这个时间SD卡没有响应，可以认为操作失败或SD卡出现问题。

等待的原因可能有多种，例如SD卡正在执行某个复杂的操作，或者SD卡在某些情况下需要更长的时间来完成特定的操作。通过适当的等待时间，可以确保SD卡的状态正确，并且可以在适当的时机进行后续的操作，如读取或写入数据。

> 练习 1：完成`userland/servers/sd`中的代码，实现SD卡驱动。驱动程序需实现为用户态系统服务，应用程序与驱动通过 IPC 通信。需要实现 `sdcard_readblock` 与 `sdcard_writeblock` 接口，通过 Logical Block Address(LBA) 作为参数访问 SD 卡的块。

参照[circle](https://github.com/rsta2/circle/tree/master/addon/SDCard )对于emmc的实现

> 练习 2：实现naive_fs。

设计文件系统中第一个 block 记录文件名和文件所在block的index（观察测试用例可知最大所需要支持的file大小BLOCK_SIZE即能满足），格式为 `name$index$` ，每一个这样的entry条目占用ENTRY_SIZE大小（观察测试用例可知只需要支持10个文件，也即能够存储的目录条目数大于10即可），文件内容信息存储在对应index的block中；设计第二个block为bitmap记录文件对于各个block的使用情况。

- `naive_fs_access`：调用 sd 卡接口读取第一个 block 中的数据，遍历第一个 block 中的各个 entry，查看是否有对应名字，查到之后到 bitmap 中查看对应文件 block index 的 bitmap 位是否为 1，是则说明存在，否则则可能此文件已经被删除
- `naive_fs_creat`：先调用 `naive_fs_access`，看该文件名的文件是否已经存在，如果不存在就查看 bitmap，看哪个 block 是空的，将对应要创建文件的名字和 block 的 index 作为 entry 格式填入第一个 block 中，并调用 sd 卡接口写回数据
- `naive_fs_unlink`：先在第一个 block 中找到对应文件所在 block 的 index，之后在第二个 block 的 bitmap 中找到对应位，将其置 0，调用 sd 卡接口写回数据
- `naive_fs_pread`：先调用 `naive_fs_access`，看该文件名的文件是否存在，如果存在就在第一个 block 中找到对应文件所在 block 的 index，调用 sd 卡接口读出整个 block 的内容，拿出里面对应 offset 中开始，size 大小的内容，之后返回
- `naive_fs_write`：和上述原理类似，见代码

> 思考题：查阅资料了解 SD 卡是如何进行分区，又是如何识别分区对应的文件系统的？尝试设计方案为 ChCore 提供多分区的 SD 卡驱动支持，设计其解析与挂载流程。本题的设计部分请在实验报告中详细描述，如果有代码实现，可以编写对应的测试程序放入仓库中提交。

1. **SD卡的分区**：
   SD卡的分区是指将物理上的SD卡划分为多个逻辑上独立的区域，每个区域可以被视为一个独立的存储设备。分区的目的是允许在同一个SD卡上存储不同类型的数据或在不同操作系统之间共享数据。SD卡使用主引导记录（MBR）分区表进行分区，位于SD卡的前512字节。其中前446字节为引导程序, 后面跟着共四个分区表信息。分区表每项16个字节，描述一个分区的基本信息。

   SD卡分区的过程通常在计算机上完成，以下是一般的分区过程：

   - 使用分区工具（如磁盘管理工具）将SD卡分区为多个逻辑分区。
   - 为每个分区分配一个唯一的标识符（通常是分区号）。
   - 在SD卡的分区表中记录每个分区的位置、大小和标识符等信息。

   分区后的SD卡可以在计算机或其他设备上以多个独立的存储设备进行访问。每个分区可以独立地格式化为不同的文件系统，例如FAT32、NTFS、exFAT等。

2. **文件系统的识别**：
   文件系统是用于在存储设备上组织和管理文件和目录的一种结构。SD卡上的每个分区可以格式化为不同的文件系统，而文件系统的识别是指在访问SD卡时，如何确定每个分区所使用的文件系统类型。分区表每项16个字节，描述一个分区的基本信息，与分区类型符有关的字节信息如下：

   | 字节位 | 含义                                                         |
| ------ | ------------------------------------------------------------ |
| 5      | 分区类型符。0BH——FAT32基本分区；0BH——FAT32基本分区；07H——NTFS分区； |

   文件系统的识别通过读取分区的引导扇区或特定的文件系统标识来完成。

## 设计方案

为 ChCore 提供多分区的 SD 卡驱动支持需要经历以下步骤：

1. 初始化 SD 卡驱动：首先，需要在 ChCore 的内核中初始化 SD 卡驱动。这包括初始化硬件接口、设置时钟频率、配置数据线和电源线等。
2. 检测和枚举分区：在 SD 卡驱动初始化完成后，需要进行分区的检测和枚举。这可以通过读取 SD 卡的主引导记录（MBR）或分区表（GPT/MBR）来获取分区的信息。驱动程序可以解析分区表，并识别出每个分区的起始扇区、大小、文件系统类型等信息。
3. 创建分区对象：为每个分区创建一个分区对象。分区对象可以是一个数据结构，用于保存分区的相关信息，如起始扇区、大小、文件系统类型等。可以为每个分区创建一个独立的分区对象，并将这些对象保存在一个列表或数组中。
4. 创建挂载点：为每个分区创建挂载点，用于将文件系统挂载到 ChCore 的虚拟文件系统层次结构中。挂载点可以是目录或文件，在虚拟文件系统中具有唯一的路径。使用路径字符串来表示挂载点，例如 "/mnt/partition1" 或 "/mnt/partition2"。可以通过创建目录节点或文件节点，并将其与分区对象关联来实现挂载点的创建。
5. 挂载文件系统：针对每个分区，将其文件系统挂载到 ChCore 的虚拟文件系统层次结构中。这需要根据分区的文件系统类型调用相应的文件系统驱动程序。文件系统驱动程序负责解析文件系统的元数据结构，并将文件和目录节点添加到虚拟文件系统中的相应位置。
6. 实现文件访问接口：在挂载完成后，可以通过虚拟文件系统接口来访问文件系统中的文件和目录。这些接口可以包括打开文件、读取文件内容、创建新文件、删除文件等操作。根据虚拟文件系统的设计，这些操作可以在底层通过文件系统驱动程序来实现。

### 具体设计

- 文件系统：抽象模块的接口 `FileSystem` 类，具体的文件系统实现应继承 `FileSystem` 接口，并提供实现

  ```c++
  class FileSystem {
  public:
      virtual int open_file(const char* path) = 0;
      virtual int read_directory(const char* path) = 0;
      virtual int read_file(const char *name, int offset, int size, char *buffer) = 0;
      // ......等其他文件系统操作方法
  };
  ```

- 挂载点：在 ChCore 的虚拟文件系统中，挂载点是一个特殊的目录，用于将文件系统挂载到指定的路径

  ```c++
  struct MountPoint {
      char path[MAX_PATH_LENGTH];	// 挂载点的路径，即文件系统在 ChCore 中的访问路径
      FileSystem* filesystem;	// 指向文件系统模块的指针，用于访问文件系统的功能
      PartitionInfo partition_info;
  };
  ```

- 预先定义一个挂载点列表，用于存储所有已挂载的文件系统

  ```c++
  // 挂载点列表
  MountPoint mount_points[MAX_MOUNT_POINTS];	// 存储了已挂载的文件系统的挂载点信息
  int num_mount_points = 0;	// 存储了已挂载的文件系统的挂载点信息
  ```

**解析与挂载流程：**是指将文件系统挂载到指定的挂载点上，使得该文件系统中的文件和目录可以在挂载点路径下进行访问

1. 读取SD卡的分区表信息，解析出每个分区的起始扇区、大小、文件系统类型等信息
2. 根据文件系统类型调用相应的初始化函数，如 `fat32_init()` 或 `ext4_init()`，初始化文件系统文件系统数据结构，如超级块、inode表等，并将其与分区关联
3. 为每个分区创建挂载点对象，将文件系统模块与挂载点关联（通过 `mount()` 函数将文件系统挂载到ChCore的虚拟文件系统层次结构中），之后将挂载点信息添加到挂载点管理模块，记录挂载点的信息
4. 执行文件系统访问时候，用户通过挂载点路径访问SD卡上的文件和目录，ChCore会通过虚拟文件系统层次结构，将文件访问操作转发到对应的文件系统模块和分区

挂载函数：

```c++
void mount(const char* path, int partition_index) {
    // 根据 partition_index 找到文件系统模块和分区信息
    FileSystem* filesystem = find_filesystem(partition_index);
    PartitionInfo partition_info = get_partition_info(partition_index);

    // 创建挂载点对象
    MountPoint mount_point;
    strcpy(mount_point.path, path);
    mount_point.filesystem = filesystem;
    mount_point.partition_info = partition_info;

    // 将挂载点对象添加到挂载点列表中
    if (num_mount_points < MAX_MOUNT_POINTS) {
        mount_points[num_mount_points++] = mount_point;
    } else {
        // 处理挂载点列表已满的情况
        // 可以报错或执行替换策略等
    }
}
```

文件系统访问函数：

- `file_system_access()` 函数根据给定的路径进行文件系统访问操作。
- 需要通过路径解析算法找到对应的挂载点对象。
- 使用挂载点对象的文件系统接口方法进行文件访问操作。

```c++
void file_system_access(const char* path) {
    // 根据路径解析算法找到挂载点对象
    MountPoint* mount_point = find_mount_point(path);

    if (mount_point != nullptr) {
        // 使用文件系统模块进行文件访问操作
        FileSystem* filesystem = mount_point->filesystem;
        // 解析路径，获取相对于挂载点的相对路径
		const char* relative_path = get_relative_path(path, mount_point->path);

		// 使用文件系统模块进行文件访问操作
		filesystem->open_file(partition_info, relative_path);
    } else {
        // 处理
    }
}
```

```c++
// 查找挂载点函数
MountPoint* find_mount_point(const char* path) {
    // 遍历挂载点列表，匹配路径进行查找
    for (int i = 0; i < num_mount_points; i++) {
        MountPoint* mount_point = &mount_points[i];
        if (strcmp(mount_point->path, path) == 0) {
            return mount_point;
        }
    }
    return NULL;  // 未找到对应的挂载点
}
```

