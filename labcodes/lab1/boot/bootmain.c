#include <defs.h>
#include <x86.h>
#include <elf.h>

/* *********************************************************************
 * This a dirt simple boot loader, whose sole job is to boot
 * an ELF kernel image from the first IDE hard disk.
 *
 * DISK LAYOUT
 *  * This program(bootasm.S and bootmain.c) is the bootloader.
 *    It should be stored in the first sector of the disk.
 *
 *  * The 2nd sector onward holds the kernel image.
 *
 *  * The kernel image must be in ELF format.
 *
 * BOOT UP STEPS
 *  * when the CPU boots it loads the BIOS into memory and executes it
 *
 *  * the BIOS intializes devices, sets of the interrupt routines, and
 *    reads the first sector of the boot device(e.g., hard-drive)
 *    into memory and jumps to it.
 *
 *  * Assuming this boot loader is stored in the first sector of the
 *    hard-drive, this code takes over...
 *
 *  * control starts in bootasm.S -- which sets up protected mode,
 *    and a stack so C code then run, then calls bootmain()
 *
 *  * bootmain() in this file takes over, reads in the kernel and jumps to it.
 * 
 * */

#define SECTSIZE        512
#define ELFHDR          ((struct elfhdr *)0x10000)      // scratch space

/* waitdisk - wait for disk ready */
static void
waitdisk(void) {
    while ((inb(0x1F7) & 0xC0) != 0x40)
        /* do nothing */;
    //根据函数的定义，实际上这个函数的意思就是一直从0x1F7这个IO端口读一个byte，
    //检查它的第五位和第六位，看是不是xx10xxxx这样，如果不是就一直循环。
    //0x1f7是0号硬盘状态寄存器，https://blog.csdn.net/nkuzbp/article/details/7421904
    //这个位是干嘛的呢，见https://www.history-of-my-life.com/wp-content/uploads/2018/04/硬盘状态寄存位.png
    //https://blog.csdn.net/guzhou_diaoke/article/details/8479033
    //这两个位的实际功能就是说明目前选中了硬盘的主盘，master drive，实际上是为载入第一个扇区做准备
}

/* readsect - read a single sector at @secno into @dst */
static void
readsect(void *dst, uint32_t secno) {
    // wait for disk to be ready
    waitdisk();

    outb(0x1F2, 1);                         // count = 1
    outb(0x1F3, secno & 0xFF);
    outb(0x1F4, (secno >> 8) & 0xFF);
    outb(0x1F5, (secno >> 16) & 0xFF);
    outb(0x1F6, ((secno >> 24) & 0xF) | 0xE0);
    outb(0x1F7, 0x20);                      // cmd 0x20 - read sectors
    //0x1F2指定要读取的扇区数量，1F3是指从哪个扇区开始读，1F5和1F6分别是是指哪个柱面的高位字节和低位字节
    //也就是用secno唯一地指定了某个扇区。
    //因为1F7在读这个寄存器时（inb）是状态寄存器，写这个寄存器时是磁盘操作命令寄存器，写入0x20就是让他开始读
    //在知乎上看到人说这几个寄存器只是为了兼容IDE硬盘的缘故才保留，现在已经不怎么用了，谁知道呢。
    //从这里也看出，如果要兼容其他硬盘接口，就去找相应接口的标准文档吧。
    //这里实际上涉及了一个secno和具体柱面、扇区的一个转换。也挺有意思。
    //具体转换方式见https://blog.csdn.net/guzhou_diaoke/article/details/8479033

    // wait for disk to be ready
    waitdisk();

    // read a sector
    insl(0x1F0, dst, SECTSIZE / 4);
    //就是从0x1F0这个端口（硬盘），读取512/4个双字(insl)长的数据（刚好512字节），到dst里面去
    //读取的位置是之前用outb写的这些IO寄存器中用secno指定的。
}

/* *
 * readseg - read @count bytes at @offset from kernel into virtual address @va,
 * might copy more than asked.
 * */
static void
readseg(uintptr_t va, uint32_t count, uint32_t offset) {
    //根据上下文，va是要写的地址，count是指要写多少块扇区进去，offset是指开始读取的地方距离扇区开始的偏移量

    uintptr_t end_va = va + count;
    //结束的地址，实际上用来控制循环的，读够了就停了。

    // round down to sector boundary
    va -= offset % SECTSIZE;
    //如果offset不是整扇区，那么va也做适当的调整，以对齐，作用不明，猜测是为了防止一次没读完，接着读的情况

    // translate from bytes to sectors; kernel starts at sector 1
    uint32_t secno = (offset / SECTSIZE) + 1;
    //指定从哪个扇区开始读，内核都是从第一个扇区开始

    // If this is too slow, we could read lots of sectors at a time.
    // We'd write more to memory than asked, but it doesn't matter --
    // we load in increasing order.
    for (; va < end_va; va += SECTSIZE, secno ++) {
        readsect((void *)va, secno);
    }
    //然后开始读就是了
}

/* bootmain - the entry of bootloader */
void
bootmain(void) {
    // read the 1st page off disk
    readseg((uintptr_t)ELFHDR, SECTSIZE * 8, 0);
    //从硬盘的第一个扇区，读8个扇区，装载到ELFHDR指定的内存位置来。

    // is this a valid ELF?
    if (ELFHDR->e_magic != ELF_MAGIC) {
        goto bad;
    }
    //发现装载进来的文件不是个ELF格式，那就跳到出错处理去

    struct proghdr *ph, *eph;

    // load each program segment (ignores ph flags)
    ph = (struct proghdr *)((uintptr_t)ELFHDR + ELFHDR->e_phoff);
    eph = ph + ELFHDR->e_phnum;
    for (; ph < eph; ph ++) {
        readseg(ph->p_va & 0xFFFFFF, ph->p_memsz, ph->p_offset);
        //这就是elf的具体文件格式了，elf的program header里存着物理文件中的内存应该被装载到哪个内存地址去
    }

    // call the entry point from the ELF header
    // note: does not return
    ((void (*)(void))(ELFHDR->e_entry & 0xFFFFFF))();
    //之后就交给elf，也就是系统，我们的ucore，来完成接下来的工作。

bad:
    outw(0x8A00, 0x8A00);
    outw(0x8A00, 0x8E00);

    /* do nothing */
    while (1);
}