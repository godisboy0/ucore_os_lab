#include <memlayout.h>

# vectors.S sends all traps here.
.text
.globl __alltraps
__alltraps:
    # push registers to build a trap frame
    # therefore make the stack look like a struct trapframe
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    pushal
    # pushal 保存所有的32位通用寄存器

    # load GD_KDATA into %ds and %es to set up data segments for kernel
    movl $GD_KDATA, %eax
    movw %ax, %ds
    movw %ax, %es

    # push %esp to pass a pointer to the trapframe as an argument to trap()
    pushl %esp

    # call trap(tf), where tf=%esp
    # 要理解为什么pushl的esp就是参数，得了解函数调用惯例，见《程序员的自我修养》第286页。
    # 根据GNU-C的调用惯例，由caller负责将参数压栈，由被调用函数负责将epb压栈，再将ebp赋值为esp
    # 因此在trap函数运行后，实际的栈帧的样子，由高到底应该是ds,es,fs,gs,所有的通用寄存器，tf参数，之后是old ebp
    # 因为old ebp是在被调用函数中压栈的，在这里并没有这个操作
    call trap

    # pop the pushed stack pointer
    popl %esp

    # return falls through to trapret...和下面实际上是连起来运行的，因为没有返回指令，所以就会接着执行下面的指令
.globl __trapret
__trapret:
    # restore registers from stack
    popal

    # restore %ds, %es, %fs and %gs
    popl %gs
    popl %fs
    popl %es
    popl %ds

    # get rid of the trap number and error code
    addl $0x8, %esp
    iret