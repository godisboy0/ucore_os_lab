#include <mmu.h>
#include <memlayout.h>

#define REALLOC(x) (x - KERNBASE)   
/*KERNBASE定义于memlayout.h，其值为0xC0000000

.text
.globl kern_entry
kern_entry:
    # load pa of boot pgdir
    movl $REALLOC(__boot_pgdir), %eax
    movl %eax, %cr3
    # CR3是页目录基址寄存器，保存页目录表的物理地址
    # https://blog.csdn.net/wyt4455/article/details/8691500 控制寄存器的作用详解

    # enable paging
    movl %cr0, %eax
    orl $(CR0_PE | CR0_PG | CR0_AM | CR0_WP | CR0_NE | CR0_TS | CR0_EM | CR0_MP), %eax
    andl $~(CR0_TS | CR0_EM), %eax
    movl %eax, %cr0
    # 这一段使能了页机制。

    # update eip
    # now, eip = 0x1.....
    leal next, %eax
    # set eip = KERNBASE + 0x1.....
    jmp *%eax
next:

    # unmap va 0 ~ 4M, it's temporary mapping
    xorl %eax, %eax
    movl %eax, __boot_pgdir

    # set ebp, esp
    movl $0x0, %ebp
    # the kernel stack region is from bootstack -- bootstacktop,
    # the kernel stack size is KSTACKSIZE (8KB)defined in memlayout.h
    movl $bootstacktop, %esp
    # now kernel stack is ready , call the first C function
    call kern_init

# should never get here
spin:
    jmp spin

.data
.align PGSIZE
    .globl bootstack
bootstack:
    .space KSTACKSIZE
    .globl bootstacktop
bootstacktop:
# .space size{,value}   用value填充size个字节的内存单元（value默认为0）
# http://www.cnblogs.com/hnrainll/archive/2011/06/09/2075969.html 汇编伪指令大全

# kernel builtin pgdir
# an initial page directory (Page Directory Table, PDT)
# These page directory table and page table can be reused!
.section .data.pgdir
.align PGSIZE
__boot_pgdir:
.globl __boot_pgdir
    # map va 0 ~ 4M to pa 0 ~ 4M (temporary)
    .long REALLOC(__boot_pt1) + (PTE_P | PTE_U | PTE_W)
    .space (KERNBASE / PGSIZE / 1024 * 4) - (. - __boot_pgdir) # pad to PDE of KERNBASE
    # 为什么有这个space，space的值是怎么来的，见这个文件最后的大段注释。
    # map va KERNBASE + (0 ~ 4M) to pa 0 ~ 4M
    .long REALLOC(__boot_pt1) + (PTE_P | PTE_U | PTE_W)
    .space PGSIZE - (. - __boot_pgdir) # pad to PGSIZE

.set i, 0 
__boot_pt1:
# 伪指令REPT的作用是把一组语句重复指定的次数，该重复次数由伪指令后面的数值表达式来确定。也就是说下面这两句代码将被重复执行1024次
# 这么干就在内存里插入了这么多个long字，插入的到底是什么呢？其实是二级页表，pt(page table)，一级页表叫做页目录，pgdir这个样子
.rept 1024
    .long i * PGSIZE + (PTE_P | PTE_W)
    .set i, i + 1
.endr
#endr，就是结束rept的意思，相当于后大括号吧……
/* 
https://blog.csdn.net/longintchar/article/details/52166130 页表格式见这个页面
实际上就是把所有的页表项全部标记为可用，并且可写
*/

/*********   __boot_pgdir中的space   ************
1.为什么要有。
// A linear address 'la' has a three-part structure as follows:
//
// +--------10------+-------10-------+---------12----------+
// | Page Directory |   Page Table   | Offset within Page  |
// |      Index     |     Index      |                     |
// +----------------+----------------+---------------------+
//  \--- PDX(la) --/ \--- PTX(la) --/ \---- PGOFF(la) ----/
//  \----------- PPN(la) -----------/
//
// The PDX, PTX, PGOFF, and PPN macros decompose linear addresses as shown.
// To construct a linear address la from PDX(la), PTX(la), and PGOFF(la),
// use PGADDR(PDX(la), PTX(la), PGOFF(la)).

如上图，实际上线性地址的高10位被用作页目录项的下标，因为是10位，所以页目录项就有1024个表项。
而__boot_pgdir实际上并不打算把所有的页表的地址都一次性搞定，因为目前还用不着，他只写了当前
用得着的两个页表地址，这两个用得着的页表因为不是直接相邻，因此需要中间用space填满。
2.space怎么算出来的
书接上回，space怎么算，就看现在操作系统用得着的页表到底是哪两部分页表。根据要求，页表是将0-4M和
0xC0000000-0xC0000000+4M的线性地址都映射到了同一块物理地址上,KERNBASE / PGSIZE / 1024
得到的其实正好是KERNBASE线性地址高10位的值，也就其对应的页表在页目录中的下标，为什么要乘以四，
因为一个表项是long数据，是4个字节，为什么要减去……这特么还用问。
为什么KERNBASE从0xC0000000开始，明天再研究。