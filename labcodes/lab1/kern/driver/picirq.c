#include <defs.h>
#include <x86.h>
#include <picirq.h>

//根据维基百科，这段代码处理的应该是老版PIC(Programmable interrupt controller)(intel 8259s family)卡的硬件中断问题
//BUT No PCs have been built with 8259s in at least ten years. 也就是说这的确是古董级别的卡了
//后来这个卡被APIC(Advanced Programmable Interrupt Controller)取代了。后来还有没有发展，我就不知道了
//参考网址如下：
//Intel-8259：  https://en.wikipedia.org/wiki/Intel_8259
//IRQ：         https://en.wikipedia.org/wiki/Interrupt_request_(PC_architecture)
//PIC：         https://en.wikipedia.org/wiki/Programmable_interrupt_controller
//APIC：        https://en.wikipedia.org/wiki/Advanced_Programmable_Interrupt_Controller
//PIC2：        http://retired.beyondlogic.org/interrupts/interupt.htm
//IMR：         https://en.wikipedia.org/wiki/Programmable_interrupt_controller 
//被这个芯片以及相关概念折腾了一天的一个收获是，以后遇到这种与特定硬件结合紧密的概念，最好先去查一下硬件资料，比如刚开始看了
//一大堆仍然不是很明白的概念，查了Intel-8259的介绍就清楚多了。


// I/O Addresses of the two programmable interrupt controllers
#define IO_PIC1             0x20    // Master (IRQs 0-7)
#define IO_PIC2             0xA0    // Slave (IRQs 8-15)

#define IRQ_SLAVE           2       // IRQ at which slave connects to master

// Current IRQ mask.
// Initial IRQ mask has interrupt 2 enabled (for slave 8259A).
static uint16_t irq_mask = 0xFFFF & ~(1 << IRQ_SLAVE);
static bool did_init = 0;

static void
pic_setmask(uint16_t mask) {
    irq_mask = mask;
    if (did_init) {
        outb(IO_PIC1 + 1, mask);
        outb(IO_PIC2 + 1, mask >> 8);
    }
}

void
pic_enable(unsigned int irq) {
    pic_setmask(irq_mask & ~(1 << irq));
}

/* pic_init - initialize the 8259A interrupt controllers */
void
pic_init(void) {
    did_init = 1;

    // mask all interrupts，根据上文网址PIC2提供的内容，向0x21h写数据，就是Interrupt Mask Register(IMR)
    outb(IO_PIC1 + 1, 0xFF);
    outb(IO_PIC2 + 1, 0xFF);

    // Set up master (8259A-1)  设置主卡，因为PIC卡有两个，master 和 slave

    // ICW1:  0001g0hi
    //    g:  0 = edge triggering, 1 = level triggering
    //    边缘出发还是条件触发，说白了就是中断触发的具体方式不同，作为用户没有了解detail的必要。
    //    见： https://en.wikipedia.org/wiki/Interrupt#Level-triggered
    //    h:  0 = cascaded PICs, 1 = master only
    //    因为slave PIC的信号需要映射到master PIC的第二位，所以第二位写1就相当于把slave PIC屏蔽了
    //    i:  0 = no ICW4, 1 = ICW4 required
    //    当PIC被reset后，必须用2到4个ICW（Initialization Command Word）来初始化，分别命名为：ICW{1,2,3,4}
    //    当ICW1的最低位设为0时，就表明不需要再用ICW4进行初始化了。
    //    见http://retired.beyondlogic.org/interrupts/interupt.htm
    outb(IO_PIC1, 0x11);

    // ICW2:  Vector offset 写完ICW1后，后三个ICW就向x021端口写入了。
    // 中断描述符表是由BIOS放入内存的，共256项，每一项8个字节（在实模式下是4个字节，那时候中断描述符表还叫中断向量表）
    // 中断描述符表的前32项由INTEL硬件使用，因此这里不能使用。其中0-19号表项处理不能屏蔽中断和异常
    // 20-31号表项目前还没有使用，保留中。除此之外：IDT may reside anywhere in physical memory 而且
    // hardware interrupts may be mapped to any of the vectors by way of a programmable interrupt controller.
    // 意思是在PIC里，硬件中断处理例程的地址可以被装载到任意index的表项里面去。
    // IDT的起始地址存在IDTR中，这是CPU的一个寄存器，与GDTR相似，这个寄存器有48位。
    outb(IO_PIC1 + 1, IRQ_OFFSET);

    // ICW3:  (master PIC) bit mask of IR lines connected to slaves
    //        (slave PIC) 3-bit # of slave's connection to master
    outb(IO_PIC1 + 1, 1 << IRQ_SLAVE);

    // ICW4:  000nbmap 
    //    n:  1 = special fully nested mode
    //    b:  1 = buffered mode
    //    m:  0 = slave PIC, 1 = master PIC
    //        (ignored when b is 0, as the master/slave role
    //         can be hardwired).
    //    a:  1 = Automatic EOI mode
    //    EOI：就是当中断处理进程处理完毕后发送的一个信号，让PIC重置相关寄存器
    //    https://en.wikipedia.org/wiki/End_of_interrupt
    //    p:  0 = MCS-80/85 mode, 1 = intel x86 mode
    outb(IO_PIC1 + 1, 0x3);

    // Set up slave (8259A-2)
    outb(IO_PIC2, 0x11);    // ICW1
    outb(IO_PIC2 + 1, IRQ_OFFSET + 8);  // ICW2
    outb(IO_PIC2 + 1, IRQ_SLAVE);       // ICW3
    // NB Automatic EOI mode doesn't tend to work on the slave.
    // Linux source code says it's "to be investigated".
    outb(IO_PIC2 + 1, 0x3);             // ICW4

    // OCW3:  0ef01prs
    // OCW（Operation Control Word），是指中断控制命令咯
    //   ef:  0x = NOP, 10 = clear specific mask, 11 = set specific mask
    //    p:  0 = no polling, 1 = polling mode
    //   rs:  0x = NOP, 10 = read IRR, 11 = read ISR
    outb(IO_PIC1, 0x68);    // clear specific mask
    //不对吧，0x68是01101000b，第ef位是11，应该是set specific mask才对啊
    //这里本来的意思是清除在本文件47、48行设置的mask吗？
    outb(IO_PIC1, 0x0a);    // read IRR by default

    outb(IO_PIC2, 0x68);    // OCW3
    outb(IO_PIC2, 0x0a);    // OCW3

    if (irq_mask != 0xFFFF) {
        pic_setmask(irq_mask);
    }

    /* 在这里再详述一下对中断机制的理解：
    * 中断分为硬件中断和软件中断，软件中断有我们经常所说的异常、系统调用等，这一般是由特殊的代码引起的，是同步的行为。
    * 如系统调用，调用后就直接陷入系统内核进行处理了，处理完之后再返回调用处继续工作。这和异步的硬件中断其实相差很大。
    * 在这里我们讨论这段代码主要处理的硬件中断。
    * 所谓硬件中断，一般是因为IO设备引起的，比如键盘、鼠标、网络设备等。
    * 早期的计算机（就是ucore这段代码试图驱动的计算机硬件），共设计了16条硬件中断line，因为line8到15映射到line2
    * 所以实际可用的是15条中断线，分别对应15种硬件中断，比如时钟中断、串口、并口、键盘等
    * 其中中断线0-7由PIC1控制，8-15由PIC2控制。
    * 当相关硬件准备好之后，就会将相应的中断线置为活动。此时所有的中断数据首先经过一个中断屏蔽器，如果被屏蔽了，就over
    * 如果没有屏蔽，再进入中断排队器，一个一个来嘛。这个排队有一些不同的实现方法，基本是引脚数字越小越优先这个样子。
    * 排队之后把这个最终的中断向量放到中断控制器的IO端口，也就是0x20或者0xA0，之后把引发信号放到处理器的INTR引脚 
    * 之后就等着CPU来临幸。CPU检查到INTR引脚有信号，就会来检查相关端口，读出中断向量。
    * https://blog.csdn.net/baidu_24256693/article/details/64920137
    * 这有一篇中文的，辅助理解。
    * 这张图极好http://images.cnitblog.com/blog2015/687284/201504/031610264356459.jpg
    * 
    * 只不过，后来这个标准被APIC替代了。https://wiki.osdev.org/APIC APIC的维基百科看得我晕头转向，还是这个好
    * 
    */
}

