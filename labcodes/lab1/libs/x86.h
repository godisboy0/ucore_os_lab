#ifndef __LIBS_X86_H__
#define __LIBS_X86_H__

#include <defs.h>

#define do_div(n, base) ({                                        \
    unsigned long __upper, __low, __high, __mod, __base;        \
    __base = (base);                                            \
    asm("" : "=a" (__low), "=d" (__high) : "A" (n));            \
    __upper = __high;                                            \
    if (__high != 0) {                                            \
        __upper = __high % __base;                                \
        __high = __high / __base;                                \
    }                                                            \
    asm("divl %2" : "=a" (__low), "=d" (__mod)                    \
        : "rm" (__base), "0" (__low), "1" (__upper));            \
    asm("" : "=A" (n) : "a" (__low), "d" (__high));                \
    __mod;                                                        \
 })

static inline uint8_t inb(uint16_t port) __attribute__((always_inline));
static inline void insl(uint32_t port, void *addr, int cnt) __attribute__((always_inline));
static inline void outb(uint16_t port, uint8_t data) __attribute__((always_inline));
static inline void outw(uint16_t port, uint16_t data) __attribute__((always_inline));
static inline uint32_t read_ebp(void) __attribute__((always_inline));

/* Pseudo-descriptors used for LGDT, LLDT(not used) and LIDT instructions. */
// 段描述符寄存器和中断描述符寄存器的数据结构。前16位是长度，后32位是基址。
struct pseudodesc {
    uint16_t pd_lim;        // Limit
    uint32_t pd_base;        // Base address
} __attribute__ ((packed));
//__attribute__ ((packed)) 的作用就是告诉编译器取消结构在编译过程中的优化对齐,按照实际占用字节数进行对齐
//主要是因为GDTR和IDTR寄存器的长度都是48位的，如果编译器优化对齐了，就放不进去了。

static inline void lidt(struct pseudodesc *pd) __attribute__((always_inline));
static inline void sti(void) __attribute__((always_inline));
static inline void cli(void) __attribute__((always_inline));
static inline void ltr(uint16_t sel) __attribute__((always_inline));

static inline uint8_t
inb(uint16_t port) {
    //这一段实际上就是把inb这个汇编指令包装了一下，inb src des，大意是从IO端口port中读取一个byte，然后返回
    //“movl %1,%0”是指令模板；“%0”和“%1”代表指令的操作数，称为占位符，内嵌汇编靠它们将C语言表达式与指令操作数相对应。
    //指令模板后面用小括号括起来的是C语言表达式，本例中只有两个：“result”和“input”，他们按照出现的顺序分别与指令操作
    //数“%0”，“%1，”对应；注意对应顺序：第一个C表达式对应“%0”；第二个表达式对应“%1”，操作数至多有10个。
    //https://blog.csdn.net/ontheroad530/article/details/50446075
    //https://blog.csdn.net/ml_1995/article/details/51044260
    //"=a"，首先“=“是指后面跟的C语言（data）是输出操作数，也就是说会将变量输出到内存中data那个地址去
    //而"a"，"d"，是指在放到内存中去之间，要使用一下寄存器，不同的字母代表不同的寄存器
    //https://blog.csdn.net/qq_15974389/article/details/76416668  
    //这种=a,d叫做限制字符串。  
    uint8_t data;
    asm volatile ("inb %1, %0" : "=a" (data) : "d" (port));
    return data;
}

static inline void
insl(uint32_t port, void *addr, int cnt) {
    asm volatile (
            "cld;"
            "repne; insl;"
            : "=D" (addr), "=c" (cnt)
            : "d" (port), "0" (addr), "1" (cnt)
            : "memory", "cc");

    //https://www.cnblogs.com/baozou/articles/4581507.html
    //http://stanislavs.org/helppc/repne.html
    //repne，即重复字符串操作while CX!= 0 and Zero flag is clear
    //insl，即从IO端口输入数据到内存中的字符串，也就是我们从硬盘要做的读操作
    //=D限制字符是指将addr的数值写入EDI寄存器，同时也写入内存=c就是将cnt写入ecx中，同时也写入内存中
    //EDI寄存器的作用是指示写内存的destination，每次字符操作后自增或自减（取决于CLD设置的方向）
    //https://www.linuxprobe.com/gcc-how-to.html
    //"0","1" 这个也是一种约束符，用于指定与第0/1个输出变量相同的约束。即指定addr改变后的输出和cnt改变后
    //的输出还被存储在原寄存器，即EDI和ECX
    //"memory","cc"是指修饰寄存器列表，一些指令会破坏一些硬件寄存器内容。我们不得不在修饰寄存器中列出这些寄存器
    //即汇编函数内第三个 ’:’ 之后的域。这可以通知 gcc 我们将会自己使用和修改这些寄存器，这样 gcc 就不会假设存入这些寄存器的值是有效的。
    //如果指令隐式或显式地使用了任何其他寄存器，（并且寄存器没有出现在输出或者输出约束列表里），那么就需要在修饰寄存器列表中指定这些寄存器。
    //所以"memory","cc"是通知gcc，这段汇编将修改条件寄存器CC和以不可知的方式修改内存。这样在这段内联汇编以外
    //的部分，gcc就不会假设cc的值是有效的，也不会保持缓存于寄存器的内存制，

    //所以，综上，这段代码的意思就是从port指定的端口里，读取cnt个long（4字节）的数据，到addr指定位置
}

static inline void
outb(uint16_t port, uint8_t data) {
    asm volatile ("outb %0, %1" :: "a" (data), "d" (port));
}

static inline void
outw(uint16_t port, uint16_t data) {
    asm volatile ("outw %0, %1" :: "a" (data), "d" (port));
}

static inline uint32_t
read_ebp(void) {
    uint32_t ebp;
    asm volatile ("movl %%ebp, %0" : "=r" (ebp));
    return ebp;
}

static inline void
lidt(struct pseudodesc *pd) {
    asm volatile ("lidt (%0)" :: "r" (pd));
}

static inline void
sti(void) {
    asm volatile ("sti");
}

static inline void
cli(void) {
    asm volatile ("cli");
}

static inline void
ltr(uint16_t sel) {
    asm volatile ("ltr %0" :: "r" (sel));
}

static inline int __strcmp(const char *s1, const char *s2) __attribute__((always_inline));
static inline char *__strcpy(char *dst, const char *src) __attribute__((always_inline));
static inline void *__memset(void *s, char c, size_t n) __attribute__((always_inline));
static inline void *__memmove(void *dst, const void *src, size_t n) __attribute__((always_inline));
static inline void *__memcpy(void *dst, const void *src, size_t n) __attribute__((always_inline));

#ifndef __HAVE_ARCH_STRCMP
#define __HAVE_ARCH_STRCMP
static inline int
__strcmp(const char *s1, const char *s2) {
    int d0, d1, ret;
    asm volatile (
            "1: lodsb;"
            "scasb;"
            "jne 2f;"
            "testb %%al, %%al;"
            "jne 1b;"
            "xorl %%eax, %%eax;"
            "jmp 3f;"
            "2: sbbl %%eax, %%eax;"
            "orb $1, %%al;"
            "3:"
            : "=a" (ret), "=&S" (d0), "=&D" (d1)
            : "1" (s1), "2" (s2)
            : "memory");
    return ret;
}

#endif /* __HAVE_ARCH_STRCMP */

#ifndef __HAVE_ARCH_STRCPY
#define __HAVE_ARCH_STRCPY
static inline char *
__strcpy(char *dst, const char *src) {
    int d0, d1, d2;
    asm volatile (
            "1: lodsb;"
            "stosb;"
            "testb %%al, %%al;"
            "jne 1b;"
            : "=&S" (d0), "=&D" (d1), "=&a" (d2)
            : "0" (src), "1" (dst) : "memory");
    return dst;
}
#endif /* __HAVE_ARCH_STRCPY */

#ifndef __HAVE_ARCH_MEMSET
#define __HAVE_ARCH_MEMSET
static inline void *
__memset(void *s, char c, size_t n) {
    int d0, d1;
    asm volatile (
            "rep; stosb;"
            : "=&c" (d0), "=&D" (d1)
            : "0" (n), "a" (c), "1" (s)
            : "memory");
    return s;
}
#endif /* __HAVE_ARCH_MEMSET */

#ifndef __HAVE_ARCH_MEMMOVE
#define __HAVE_ARCH_MEMMOVE
static inline void *
__memmove(void *dst, const void *src, size_t n) {
    if (dst < src) {
        return __memcpy(dst, src, n);
    }
    int d0, d1, d2;
    asm volatile (
            "std;"
            "rep; movsb;"
            "cld;"
            : "=&c" (d0), "=&S" (d1), "=&D" (d2)
            : "0" (n), "1" (n - 1 + src), "2" (n - 1 + dst)
            : "memory");
    return dst;
}
#endif /* __HAVE_ARCH_MEMMOVE */

#ifndef __HAVE_ARCH_MEMCPY
#define __HAVE_ARCH_MEMCPY
static inline void *
__memcpy(void *dst, const void *src, size_t n) {
    int d0, d1, d2;
    asm volatile (
            "rep; movsl;"
            "movl %4, %%ecx;"
            "andl $3, %%ecx;"
            "jz 1f;"
            "rep; movsb;"
            "1:"
            : "=&c" (d0), "=&D" (d1), "=&S" (d2)
            : "0" (n / 4), "g" (n), "1" (dst), "2" (src)
            : "memory");
    return dst;
}
#endif /* __HAVE_ARCH_MEMCPY */

#endif /* !__LIBS_X86_H__ */

