#include <defs.h>
#include <stdio.h>
#include <string.h>
#include <console.h>
#include <kdebug.h>
#include <picirq.h>
#include <trap.h>
#include <clock.h>
#include <intr.h>
#include <pmm.h>
#include <kmonitor.h>
int kern_init(void) __attribute__((noreturn));
//https://gcc.gnu.org/onlinedocs/gcc-4.6.4/gcc/Function-Attributes.html
//GNU关于__attribute__的介绍，这是用来指定属性的， 用在这里是指定了该函数永不返回
//用来提示编译器做一些或者不做一些工作，具体就不深究了
void grade_backtrace(void);
static void lab1_switch_test(void);

//在kernel.ld这个汇编器使用的文件中，用ENTRY(kern_init)指明了kern_init是整个内核引导程序的入口
int
kern_init(void) {
    extern char edata[], end[];
    //extern声明变量定义在其他地方，定义了两个字符数组。实际上定义在tools的kernel.ld这个链接文档中

    memset(edata, 0, end - edata);
    //将这一段全部写成0，从edata开始，一直写end-edata这么多数量
    //定义在string.c里。考虑到kernel.ld这个链接配置文件中对于edata和end的定义
    //这一段实际是把.bbs段全部写0，.bbs段是定义而没有赋值的全局变量和静态变量
    //也就是说之后遇到的所有已定义但没有复制的全局变量和静态变量，其初始值都是0

    cons_init();                // init the console，实际上初始化了CGA显示器和Keyboard输入

    const char *message = "(THU.CST) os is loading ...";
    cprintf("%s\n\n", message);
    //cprintf()的函数调用还是蛮深的，在下面画出来
    /*
                              (进行格式化)     (计算写入字符数)     (写入三个终端)   | -> lpt_putc()    -> lpt_putc_sub()
    cprintf() -> vcprintf() -> vprintfmt() ->     cputch()   ->  cons_putc() -> | -> cga_putc()
                                                                                | -> serial_putc() -> serial_putc_sub()
    其中，最后的xxx_putc()都是处理一个退格的问题，xxx_putc_sub()是具体负责写入设备的具体工作
    cga_putc()因为直接操作线性显存地址，所以不需要cga_putc_sub()。
    cons_putc()先后调用了三个输出设备，应该是默认如果有接入相关输出设备，都应该输出的意思。
    这个函数调用栈还是很值得借鉴的，很有意思。如何拆解函数功能？还得磨练啊。
    */

    print_kerninfo();

    //下面这个函数是用来追踪函数调用栈的，实际上就是lab1的一个实验操作。最后是让你把整个函数调用栈打印出来，哈哈。
    grade_backtrace();

    pmm_init();                 // init physical memory management, 这个函数很有意思，相当于把GDT重新设置一下，并重新加载了GDTR

    pic_init();                 // init interrupt controller
    idt_init();                 // init interrupt descriptor table

    clock_init();               // init clock interrupt
    intr_enable();              // enable irq interrupt

    //LAB1: CAHLLENGE 1 If you try to do it, uncomment lab1_switch_test()
    // user/kernel mode switch test
    //lab1_switch_test();

    /* do nothing */
    while (1);
}

void __attribute__((noinline))
grade_backtrace2(int arg0, int arg1, int arg2, int arg3) {
    mon_backtrace(0, NULL, NULL);
}

void __attribute__((noinline))
grade_backtrace1(int arg0, int arg1) {
    grade_backtrace2(arg0, (int)&arg0, arg1, (int)&arg1);
}

//有时函数比较短，会被gcc静默优化成inline模式，__attribute__((noinline))指定强制不inline
void __attribute__((noinline))
grade_backtrace0(int arg0, int arg1, int arg2) {
    grade_backtrace1(arg0, arg2);
}

void
grade_backtrace(void) {
    grade_backtrace0(0, (int)kern_init, 0xffff0000);
}

static void
lab1_print_cur_status(void) {
    static int round = 0;
    uint16_t reg1, reg2, reg3, reg4;
    asm volatile (
            "mov %%cs, %0;"
            "mov %%ds, %1;"
            "mov %%es, %2;"
            "mov %%ss, %3;"
            : "=m"(reg1), "=m"(reg2), "=m"(reg3), "=m"(reg4));
    cprintf("%d: @ring %d\n", round, reg1 & 3);
    cprintf("%d:  cs = %x\n", round, reg1);
    cprintf("%d:  ds = %x\n", round, reg2);
    cprintf("%d:  es = %x\n", round, reg3);
    cprintf("%d:  ss = %x\n", round, reg4);
    round ++;
}

static void
lab1_switch_to_user(void) {
    //LAB1 CHALLENGE 1 : TODO
}

static void
lab1_switch_to_kernel(void) {
    //LAB1 CHALLENGE 1 :  TODO
}

static void
lab1_switch_test(void) {
    lab1_print_cur_status();
    cprintf("+++ switch to  user  mode +++\n");
    lab1_switch_to_user();
    lab1_print_cur_status();
    cprintf("+++ switch to kernel mode +++\n");
    lab1_switch_to_kernel();
    lab1_print_cur_status();
}

