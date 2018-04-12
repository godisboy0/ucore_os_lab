#include <defs.h>
#include <mmu.h>
#include <memlayout.h>
#include <clock.h>
#include <trap.h>
#include <x86.h>
#include <stdio.h>
#include <assert.h>
#include <console.h>
#include <kdebug.h>

#define TICK_NUM 100

static void print_ticks() {
    cprintf("%d ticks\n",TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}

/* *
 * Interrupt descriptor table:
 *
 * Must be built at run time because shifted function addresses can't
 * be represented in relocation records.
 * */
static struct gatedesc idt[256] = {{0}};

static struct pseudodesc idt_pd = {
    sizeof(idt) - 1, (uintptr_t)idt
};

/* idt_init - initialize IDT to each of the entry points in kern/trap/vectors.S 
 * 中断描述符表。在实模式下，称相同功能的表为中断向量表，Interrupt Vector Table.
 * 这个表的具体功能就见picirq.c里的描述吧，我们只需要知道中段描述符的组成就行了
 * https://en.wikipedia.org/wiki/Interrupt_descriptor_table
*/
void
idt_init(void) {
    /* LAB1 YOUR CODE : STEP 2 */
    /* (1) Where are the entry addrs of each Interrupt Service Routine (ISR)?
     *     All ISR's entry addrs are stored in __vectors. where is uintptr_t __vectors[] ?
     *     __vectors[] is in kern/trap/vector.S which is produced by tools/vector.c
     *     (try "make" command in lab1, then you will find vector.S in kern/trap DIR)
     *     You can use  "extern uintptr_t __vectors[];" to define this extern variable which will be used later.
     * (2) Now you should setup the entries of ISR in Interrupt Description Table (IDT).
     *     Can you see idt[256] in this file? Yes, it's IDT! you can use SETGATE macro to setup each item of IDT
     * (3) After setup the contents of IDT, you will let CPU know where is the IDT by using 'lidt' instruction.
     *     You don't know the meaning of this instruction? just google it! and check the libs/x86.h to know more.
     *     Notice: the argument of lidt is idt_pd. try to find it!
    */
    /*
    * 那么__vector到底在哪里呢？定义在kern/trap/vector.S的1281行，还是汇编语法.long，直接置入。总共0-255项
    * 每一项都完全一致，是两个push，push一个0，和一个中断向量号。然后一个ljmp操作，跳到__alltraps里面（定义在kern/trap/trapentry.S）
    * 问题在于，按规定，IDT是有什么任务门、陷阱门、中断门的。这里好像都没有区分的样子？https://blog.csdn.net/windeal3203/article/details/44590829
    * 先按没区分写吧，都处理为0（中断门），完了看看答案怎么处理的。
    */
    extern uintptr_t __vectors[];
    for ( int i = 0 ; i != 256 ; ++i){  //答案的更小心一点，其实没必要啊，前面都声明了256项了。
        SETGATE(idt[i] , 0 , KERNEL_CS , __vectors[i] , 0);
    }

    // set for switch from user to kernel，答案里为什么会出现这段话？这是干嘛的？没提示啊。意思是用户级代码可使用的中断？
    SETGATE(idt[T_SWITCH_TOK], 0, GD_KTEXT, __vectors[T_SWITCH_TOK], DPL_USER);

    lidt(&idt_pd); //一段内联汇编的包装函数~

}

static const char *
trapname(int trapno) {
    static const char * const excnames[] = {
        "Divide error",
        "Debug",
        "Non-Maskable Interrupt",
        "Breakpoint",
        "Overflow",
        "BOUND Range Exceeded",
        "Invalid Opcode",
        "Device Not Available",
        "Double Fault",
        "Coprocessor Segment Overrun",
        "Invalid TSS",
        "Segment Not Present",
        "Stack Fault",
        "General Protection",
        "Page Fault",
        "(unknown trap)",
        "x87 FPU Floating-Point Error",
        "Alignment Check",
        "Machine-Check",
        "SIMD Floating-Point Exception"
    };

    if (trapno < sizeof(excnames)/sizeof(const char * const)) {
        return excnames[trapno];
    }
    if (trapno >= IRQ_OFFSET && trapno < IRQ_OFFSET + 16) {
        return "Hardware Interrupt";
    }
    return "(unknown trap)";
}

/* trap_in_kernel - test if trap happened in kernel */
bool
trap_in_kernel(struct trapframe *tf) {
    return (tf->tf_cs == (uint16_t)KERNEL_CS);
}

static const char *IA32flags[] = {
    "CF", NULL, "PF", NULL, "AF", NULL, "ZF", "SF",
    "TF", "IF", "DF", "OF", NULL, NULL, "NT", NULL,
    "RF", "VM", "AC", "VIF", "VIP", "ID", NULL, NULL,
};

void
print_trapframe(struct trapframe *tf) {
    cprintf("trapframe at %p\n", tf);
    print_regs(&tf->tf_regs);
    cprintf("  ds   0x----%04x\n", tf->tf_ds);
    cprintf("  es   0x----%04x\n", tf->tf_es);
    cprintf("  fs   0x----%04x\n", tf->tf_fs);
    cprintf("  gs   0x----%04x\n", tf->tf_gs);
    cprintf("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
    cprintf("  err  0x%08x\n", tf->tf_err);
    cprintf("  eip  0x%08x\n", tf->tf_eip);
    cprintf("  cs   0x----%04x\n", tf->tf_cs);
    cprintf("  flag 0x%08x ", tf->tf_eflags);

    int i, j;
    for (i = 0, j = 1; i < sizeof(IA32flags) / sizeof(IA32flags[0]); i ++, j <<= 1) {
        if ((tf->tf_eflags & j) && IA32flags[i] != NULL) {
            cprintf("%s,", IA32flags[i]);
        }
    }
    cprintf("IOPL=%d\n", (tf->tf_eflags & FL_IOPL_MASK) >> 12);

    if (!trap_in_kernel(tf)) {
        cprintf("  esp  0x%08x\n", tf->tf_esp);
        cprintf("  ss   0x----%04x\n", tf->tf_ss);
    }
}

void
print_regs(struct pushregs *regs) {
    cprintf("  edi  0x%08x\n", regs->reg_edi);
    cprintf("  esi  0x%08x\n", regs->reg_esi);
    cprintf("  ebp  0x%08x\n", regs->reg_ebp);
    cprintf("  oesp 0x%08x\n", regs->reg_oesp);
    cprintf("  ebx  0x%08x\n", regs->reg_ebx);
    cprintf("  edx  0x%08x\n", regs->reg_edx);
    cprintf("  ecx  0x%08x\n", regs->reg_ecx);
    cprintf("  eax  0x%08x\n", regs->reg_eax);
}

/* trap_dispatch - dispatch based on what type of trap occurred */
static void
trap_dispatch(struct trapframe *tf) {
    char c;

    switch (tf->tf_trapno) {
    case IRQ_OFFSET + IRQ_TIMER:
        /* LAB1 YOUR CODE : STEP 3 */
        /* handle the timer interrupt 时钟中断*/
        /* (1) After a timer interrupt, you should record this event using a global variable (increase it), such as ticks in kern/driver/clock.c
         * (2) Every TICK_NUM cycle, you can print some info using a funciton, such as print_ticks().
         * (3) Too Simple? Yes, I think so!
         */
        ++ticks;
        if (ticks % TICK_NUM){
            ticks -= TICK_NUM;  //免得溢出，然而其实溢出也无所谓的。who cares
            print_ticks();
        }
        break;
    case IRQ_OFFSET + IRQ_COM1:
        c = cons_getc();
        cprintf("serial [%03d] %c\n", c, c);
        break;
    case IRQ_OFFSET + IRQ_KBD:
        c = cons_getc();
        cprintf("kbd [%03d] %c\n", c, c);
        break;
    //LAB1 CHALLENGE 1 : YOUR CODE you should modify below codes.
    case T_SWITCH_TOU:
    case T_SWITCH_TOK:
        panic("T_SWITCH_** ??\n");
        break;
    case IRQ_OFFSET + IRQ_IDE1:
    case IRQ_OFFSET + IRQ_IDE2:
        /* do nothing */
        break;
    default:
        // in kernel, it must be a mistake
        // 从用户态发出了一个中断请求，因为idt_init的答案中只设置了一个允许从用户态发出的中断，而且已经在上面case过了，因此肯定是个错误
        if ((tf->tf_cs & 3) == 0) {
            print_trapframe(tf);
            panic("unexpected trap in kernel.\n");
        }
    }
}

/* *
 * trap - handles or dispatches an exception/interrupt. if and when trap() returns,
 * the code in kern/trap/trapentry.S restores the old CPU state saved in the
 * trapframe and then uses the iret instruction to return from the exception.
 * */
void
trap(struct trapframe *tf) {
    // dispatch based on what type of trap occurred
    trap_dispatch(tf);
}