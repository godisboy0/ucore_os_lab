#include <asm.h>

# 这是个汇编代码段

# Start the CPU: switch to 32-bit protected mode, jump into C.
# The BIOS loads this code from the first sector of the hard disk into
# memory at physical address 0x7c00 and starts executing in real mode
# with %cs=0 %ip=7c00.

.set PROT_MODE_CSEG,        0x8                     # kernel code segment selector CS寄存器的值
.set PROT_MODE_DSEG,        0x10                    # kernel data segment selector DS寄存器的值
.set CR0_PE_ON,             0x1                     # protected mode enable flag   为FLAG准备的值

# start address should be 0:7c00, in real mode, the beginning address of the running bootloader
.globl start
# .globl指示告诉汇编器，_start这个符号要被链接器用到，所以要在目标文件的符号表中标记它是一个全局符号
# _start就像C程序的main函数一样特殊，是整个程序的入口，链接器在链接时会查找目标文件中的_start符号代表的地址
# 把它设置为整个程序的入口地址
start:
.code16                                             # Assemble for 16-bit mode
    cli                                             # Disable interrupts
    cld                                             # String operations increment
    # CLI：Clear Interupt；STI Set Interupt；CLD：Clear Director；STD：Set Director
    # 都是汇编代码的命令，关中断就是为了避免这个程序被打断，CLD的意思见https://www.cnblogs.com/baozou/articles/4581507.html

    # Set up the important data segment registers (DS, ES, SS).
    # 实际就是把ax,ds,es,ss寄存器全都清零了。mov src des
    xorw %ax, %ax                                   # Segment number zero
    movw %ax, %ds                                   # -> Data Segment
    movw %ax, %es                                   # -> Extra Segment
    movw %ax, %ss                                   # -> Stack Segment

    # Enable A20:
    #  For backwards compatibility with the earliest PCs, physical
    #  address line 20 is tied low, so that addresses higher than
    #  1MB wrap around to zero by default. This code undoes this.
    #  这里有个蛋疼的设定，查了半天才明白，见https://wiki.osdev.org/A20_Line
    #  实际上就是因为8086中，地址总线只有20根，从A0到A19，这样的话F800:8000 to F800:FFFF
    #  这个地址段实际上会因为计算结果的高位1被舍弃，而fallback（wrap around）到0x00000000 to 0x00007FFF
    #  一些“短视”的程序员利用了这种fallback，但随后，计算机的位数扩展了，比如80386，扩展到了32位，这样的话
    #  高位的1就不会被舍弃，那些利用了fallback的程序就无法运行，为了解决这个问题，80386在实模式下
    #  A20地址线被锁定为0，以兼容旧程序。
    #  为打开保护模式，激活4G内存，就需要解除A20的锁定，也就是上面这段英文说的，undoes this

    #  怎么解除呢，由于历史原因A20地址位由键盘控制器芯片8042管理。所以要给8042发命令激活A20
    #  8042有两个IO端口：0x60和0x64， 激活流程为： 发送0xd1命令到0x64端口 --> 发送0xdf到0x60，done！
    
seta20.1:

    inb $0x64, %al                                  # Wait for not busy(8042 input buffer empty).
    testb $0x2, %al
    jnz seta20.1
    # 发送命令之前，要等待键盘输入缓冲区为空，这通过8042的状态寄存器的第2bit来观察，而状态寄存器的值可以读0x64端口得到。
    # inb 端口，寄存器 意思是从0x64端口读一个byte到al寄存器，al是AX寄存器的低8位，jnz是指jump if not zero
    # 上面的指令的意思就是，如果状态寄存器的第2位为1，就跳到seta20.1符号处执行（实际上是循环执行本段），直到第2位为0

    movb $0xd1, %al                                 # 0xd1 -> port 0x64
    outb %al, $0x64                                 # 0xd1 means: write data to 8042's P2 port

seta20.2:
    inb $0x64, %al                                  # Wait for not busy(8042 input buffer empty).
    testb $0x2, %al
    jnz seta20.2

    movb $0xdf, %al                                 # 0xdf -> port 0x60
    outb %al, $0x60                                 # 0xdf = 11011111, means set P2's A20 bit(the 1 bit) to 1

    # Switch from real to protected mode, using a bootstrap GDT
    # and segment translation that makes virtual addresses
    # identical to physical addresses, so that the
    # effective memory map does not change during the switch.
    lgdt gdtdesc
    # 这一句是将dgtdesc的值加载到GDTR寄存器中
    movl %cr0, %eax
    orl $CR0_PE_ON, %eax
    movl %eax, %cr0
    # CR0是控制寄存器，这一段实际上就是将控制寄存器的最低一位置1，也就是打开保护模式

    # Jump to next instruction, but in 32-bit code segment.
    # Switches processor into 32-bit mode.
    ljmp $PROT_MODE_CSEG, $protcseg
    
    # 这是指跳到地址$PROT_MODE_CSEG:$protcseg的意思，这是逻辑地址的表示方式
    # $PROT_MODE_CSEG是段选择子的值，0x8，即001000b，根据段选择子的意思，实际上是选GDT表中的第2项
    # 特权级为0，这就清楚了。这样的话就是GDT的base（在ucore中为0），加protcseg的值，确定了一个地址。
    # ljmp，长跳转到这个地址，开始执行。至此，bootloader就使能了保护模式。

.code32                                             # Assemble for 32-bit mode
protcseg:
    # Set up the protected-mode data segment registers
    movw $PROT_MODE_DSEG, %ax                       # Our data segment selector
    movw %ax, %ds                                   # -> DS: Data Segment
    movw %ax, %es                                   # -> ES: Extra Segment
    movw %ax, %fs                                   # -> FS
    movw %ax, %gs                                   # -> GS
    movw %ax, %ss                                   # -> SS: Stack Segment
    # 因为ucore里实际上并不分段，因此一口气把所有剩下的段选择子地址都写成这个了。

    # Set up the stack pointer and call into C. The stack region is from 0--start(0x7c00)
    movl $0x0, %ebp
    movl $start, %esp
    call bootmain
    # 初始化堆栈，然后就跳到bootmain执行了。bootmain负责执行操作系统引导，永不返回。

    # If bootmain returns (it shouldn't), loop.
spin:
    jmp spin

# Bootstrap GDT
.p2align 2                                          # force 4 byte alignment
gdt:
    SEG_NULLASM                                     # null seg
    SEG_ASM(STA_X|STA_R, 0x0, 0xffffffff)           # code seg for bootloader and kernel
    SEG_ASM(STA_W, 0x0, 0xffffffff)                 # data seg for bootloader and kernel
    # 这一段实际上是设置GDT表的具体内容，还真是花了一番功夫才弄明白。首先要理解的就是汇编代码里这么写一个gdt:
    # 意思就是创建了一个gdt变量，gdt同时就是这个变量的地址，而SEG_NUMLLASM是asm.h这个头文件中的一个宏
    # 展开后成为.word 0,0;意思是直接在内存这个位置放两个word（每个16位）长度的0，以此类推
    # 这里实际上是在GDT数组中创建了三个数据，第一个数据全为0
    # 第二个是指可读可执行的代码段，第三个是可写的数据段。
    # 根据课程上老师的解读，他们将分段功能弱化了，并不起到扩展地址或者权限控制的实际作用，只是一个形式。
    # 因此这两个段的base都是0x0，limit都是4G。

gdtdesc:
    .word 0x17                                      # sizeof(gdt) - 1
    .long gdt                                       # address gdt
    # long是32位，实际上就是把gdt的地址放到这个位置