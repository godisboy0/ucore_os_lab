# bootloader部分
+ 三个文件

    由三个文件组成，asm.h，bootasm.S，以及bootmain.C

    用中文完整总结一下asm.h，bootasm.S，和bootmain.c的功能吧。

    实际上，asm.h只是定义了GDT的相关数据结构。

    之后bootasm.S这个文件用来打开A20总线，seta20.1和seta20.2这两个代码片段就是完成这个功能

    开启了A20总线以后，就可以将cr0寄存器中的最低一位置0，这样就打开了保护模式

    之后这段代码开始准备GDT，总共准备了三个段，所以内存中有三个段描述符。因为ucore的设计是所有段都重叠

    所以段选择子的base都是0。然后初始化一个堆栈，就把工作交给bootmain.c继续完成了

    bootmain.c的工作就是从硬盘中把系统引导的相关扇区加载到内存中

    加载的方式是先加载elfheader，之后检验加载的是不是标准的elf文件，之后根据program header的数据
    
    将整个elf文件，也就是系统引导文件，我们的ucore，加载到内存中的指定位置去，之后将控制权交给elf文件

    至此，bootloader的功能就完成了，1、使能保护模式；2、完成分段功能；3、加载系统引导文件。

+ 相关知识点
    + 内嵌汇编。各种坑啊
    https://www.linuxprobe.com/gcc-how-to.html 这个贴讲的比较全面
    https://blog.csdn.net/ml_1995/article/details/51044260 这也是全面讲解
    https://blog.csdn.net/qq_15974389/article/details/76416668 gcc内嵌的各种限制字符
    https://blog.csdn.net/ontheroad530/article/details/50446075 gcc内嵌的各种限制字符2
    + A20总线问题
    https://wiki.osdev.org/A20_Line 权威且清晰的介绍
    https://www.cnblogs.com/maruixin/p/3175894.html 具体怎么打开A20地址总线
    + 汇编语言中的.word，.byte
    实际上就是在内存的这个位置，直接生成一个word长度的数据，其内容由后面的值指定
    比如说.word 0,0; 就是指直接在内存的当前位置，放入两个word的0（每个word16位）
    + 硬盘的控制
    ucore实际上用的是IDE的硬盘接口控制方式，据说现在这种接口已经不太常用了
    这个硬盘接口通过一系列IO端口的不同值来获取指令或显示状态，具体操作见：
    https://blog.csdn.net/guzhou_diaoke/article/details/8479033
    + 一些汇编语句
    cli cld 清除某些标志位，比如cli关中断，cld设置字符串操作方向
    这个详见https://www.cnblogs.com/baozou/articles/4581507.html
    ins port dat; out port data；in中从port读取数据，out中向port写入数据
    repne; repe; 重复字符串操作如果条件满足，见http://stanislavs.org/helppc/repne.html
