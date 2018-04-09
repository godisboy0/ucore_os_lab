#include <defs.h>
#include <x86.h>
#include <stdio.h>
#include <string.h>
#include <kbdreg.h>
#include <picirq.h>
#include <trap.h>

/* stupid I/O delay routine necessitated by historical PC design flaws */
// 见https://stackoverflow.com/questions/27775517/why-do-we-need-to-delay-when-sending-char-to-serial-port
static void
delay(void) {
    inb(0x84);
    inb(0x84);
    inb(0x84);
    inb(0x84);
}

/***** Serial I/O code *****/
#define COM1            0x3F8

#define COM_RX          0       // In:  Receive buffer (DLAB=0)
#define COM_TX          0       // Out: Transmit buffer (DLAB=0)
#define COM_DLL         0       // Out: Divisor Latch Low (DLAB=1)
#define COM_DLM         1       // Out: Divisor Latch High (DLAB=1)
#define COM_IER         1       // Out: Interrupt Enable Register
#define COM_IER_RDI     0x01    // Enable receiver data interrupt
#define COM_IIR         2       // In:  Interrupt ID Register
#define COM_FCR         2       // Out: FIFO Control Register
#define COM_LCR         3       // Out: Line Control Register
#define COM_LCR_DLAB    0x80    // Divisor latch access bit
#define COM_LCR_WLEN8   0x03    // Wordlength: 8 bits
#define COM_MCR         4       // Out: Modem Control Register
#define COM_MCR_RTS     0x02    // RTS complement
#define COM_MCR_DTR     0x01    // DTR complement
#define COM_MCR_OUT2    0x08    // Out2 complement
#define COM_LSR         5       // In:  Line Status Register
#define COM_LSR_DATA    0x01    // Data available
#define COM_LSR_TXRDY   0x20    // Transmit buffer avail
#define COM_LSR_TSRE    0x40    // Transmitter off

#define MONO_BASE       0x3B4
#define MONO_BUF        0xB0000
#define CGA_BASE        0x3D4
#define CGA_BUF         0xB8000
#define CRT_ROWS        25
#define CRT_COLS        80
#define CRT_SIZE        (CRT_ROWS * CRT_COLS)

#define LPTPORT         0x378

static uint16_t *crt_buf;
static uint16_t crt_pos;
static uint16_t addr_6845;

// 显示器初始化，CGA 是 Color Graphics Adapter 的缩写
// CGA显存按照下面的方式映射：
//   -- 0xB0000 - 0xB7777 单色字符模式
//   -- 0xB8000 - 0xBFFFF 彩色字符模式及 CGA 兼容图形模式
// 6845芯片是IBM PC中的视频控制器
// CPU通过IO地址0x3B4-0x3B5来驱动6845控制单色显示，通过IO地址0x3D4-0x3D5来控制彩色显示。
//    -- 数据寄存器 映射 到 端口 0x3D5或0x3B5 
//    -- 索引寄存器 0x3D4或0x3B4,决定在数据寄存器中的数据表示什么。

/* TEXT-mode CGA/VGA display output */
static void
cga_init(void) {
    volatile uint16_t *cp = (uint16_t *)CGA_BUF;            //CGA_BUF: 0xB8000 (彩色显示的显存物理基址)
    uint16_t was = *cp;                                     //保存当前显存0xB8000处的值
    *cp = (uint16_t) 0xA55A;                                //给这个地址随便写个值，看看能否再读出同样的值
    if (*cp != 0xA55A) {                                    //如果读不出来，说明没有这块显存，即是单色配置
        cp = (uint16_t*)MONO_BUF;                           //设置为单显的显存基址 MONO_BUF： 0xB0000
        addr_6845 = MONO_BASE;                              //设置为单显控制的IO地址，MONO_BASE: 0x3B4
    } else {                                                //如果读出来了，有这块显存，即是彩显配置
        *cp = was;                                          //还原原来显存位置的值
        addr_6845 = CGA_BASE;                               //设置为彩显控制的IO地址，CGA_BASE: 0x3D4 
    }

    // Extract cursor location
    // 6845索引寄存器的index 0x0E（即十进制的14）== 光标位置(高位)
    // 6845索引寄存器的index 0x0F（即十进制的15）== 光标位置(低位)
    // 6845 reg 15 : Cursor Address (Low Byte)
    uint32_t pos;
    outb(addr_6845, 14);                                        
    pos = inb(addr_6845 + 1) << 8;                          //读出了光标位置(高位)
    outb(addr_6845, 15);
    pos |= inb(addr_6845 + 1);                              //读出了光标位置(低位)

    crt_buf = (uint16_t*) cp;                               //crt_buf是CGA显存起始地址
    crt_pos = pos;                                          //crt_pos是CGA当前光标位置
}

static bool serial_exists = 0;

static void
serial_init(void) {

    //通用串行控制器初始化，关于通用串行控制器是什么，可见http://synfare.com/599N105E/hwdocs/serial/serial01.html
    //就是计算机之间或者计算机与外设之间相互连接的一种接口
    //https://en.wikipedia.org/wiki/Serial_port
    //百度就是纯垃圾，找个串行端口的概念差点没累死。要是不能上谷歌，简直要godie
    //https://en.wikipedia.org/wiki/COM_(hardware_interface)

    // Turn off the FIFO
    // https://blog.csdn.net/huangkangying/article/details/8070945
    // 从上面的网址可知，一个串口在总线上有8个端口与之对应，COM1是0x3F8 - 0x3FF，COM2是0x2F8 - 0x2FF，etc.
    // 这一点其实上面的#define里也有隐含的说明了
    // 其中第2个，也就是下面的COM1+2，在读取和写入的时候功能不一样。在写入的时候就是FIFO控制器，在读取的时候是中断指示器。
    // 往这个端口写0是disable，写0xC7是enable FIFO
    // http://synfare.com/599N105E/hwdocs/serial/serial04.html
    // 上面这个网址介绍了读取是作为中断指示器时各bit的意义
    outb(COM1 + COM_FCR, 0);

    // Set speed; requires DLAB latch
    // 把LCR（线控寄存器）的最高位配置为1，也就是set to 1 for setting/reading the divisor for baud rate on 0x3F8 and 0x3F9
    // 设置波特率的意思
    outb(COM1 + COM_LCR, COM_LCR_DLAB);
    outb(COM1 + COM_DLL, (uint8_t) (115200 / 9600));
    outb(COM1 + COM_DLM, 0);

    // 8 data bits, 1 stop bit, parity off; turn off DLAB latch
    // 设置一次传输的数据长度吧？我猜的。
    outb(COM1 + COM_LCR, COM_LCR_WLEN8 & ~COM_LCR_DLAB);

    // No modem controls
    outb(COM1 + COM_MCR, 0);
    // Enable rcv interrupts，IER中断使能寄存器，如果设置的话会使用中断的方式来通信
    outb(COM1 + COM_IER, COM_IER_RDI);

    // Clear any preexisting overrun indications and interrupts
    // Serial port doesn't exist if COM_LSR returns 0xFF
    serial_exists = (inb(COM1 + COM_LSR) != 0xFF);
    (void) inb(COM1+COM_IIR);
    (void) inb(COM1+COM_RX);

    if (serial_exists) {
        pic_enable(IRQ_COM1);
    }
}

static void
lpt_putc_sub(int c) {
    //https://en.wikipedia.org/wiki/Parallel_port
    //并行端口的意思，一般用于打印机。在ucore里应该就是并行端口打印机的意思，不知道为什么还要向打印机输出字符= =
    //http://retired.beyondlogic.org/spp/parallel.htm 可见这个端口地址是BIOS赋值的
    //向BASE+2地址写入00001101b的意思是选择打印机、初始化打印机、关闭自动换行（auto line feed），最低位是Strobe，还没搞明白啥意思
    int i;
    for (i = 0; !(inb(LPTPORT + 1) & 0x80) && i < 12800; i ++) {
        delay();
    }
    outb(LPTPORT + 0, c);
    outb(LPTPORT + 2, 0x08 | 0x04 | 0x01);
    outb(LPTPORT + 2, 0x08);
}

/* lpt_putc - copy console output to parallel port 只是为什么要复制？不太理解啊*/
static void
lpt_putc(int c) {
    //"\b"是退格
    if (c != '\b') {
        lpt_putc_sub(c);
    }
    else {
    //退格功能的实现可以说很有才了
        lpt_putc_sub('\b');
        lpt_putc_sub(' ');
        lpt_putc_sub('\b');
    }
}

/* cga_putc - print character to console */
static void
cga_putc(int c) {
    // set black on white
    // https://wiki.osdev.org/Text_UI
    // With the rise of graphical UI's, text based user interfaces still remain practical
    // in hobbyist operating system projects. 哈哈哈……这嘲讽！太不友善了。
    // 根据这一个解释，这样的显存被设置成一个线性数组，里面一个地址对应屏幕一个位置，position = (y_position * characters_per_line) + x_position;
    // 向这个位置写数据，就直接写到了(x,y)。根据前面的cga_init()，显然ucore要驱动的屏幕设置为80x25的了。
    // 不过这个的确解释下面这个if是干嘛的，也就是说如果C存在高位，意思就是存在颜色代码，那么将前景色直接置为111b。
    // 其他颜色不管。111b就是白色咯。
    if (!(c & ~0xFF)) {
        c |= 0x0700;
    }
    
    //下面的crt是指显示器缓存中字符的位置，crt是前面定义的全局变量，在init()函数中整个.bbs段写0，他也就写0了。
    //以后这种全局变量的中值就不会再提示说是写0了，\n是换行符，\r是回车符（回到这一行首）。
    //因此遇到\n直接加一行，遇到\r就要减去这一行已经输入的字符数量了
    switch (c & 0xff) {
    case '\b':
        if (crt_pos > 0) {
            crt_pos --;
            crt_buf[crt_pos] = (c & ~0xff) | ' ';
        }
        break;
    case '\n':
        crt_pos += CRT_COLS;
    case '\r':
        crt_pos -= (crt_pos % CRT_COLS);
        break;
    default:
        crt_buf[crt_pos ++] = c;     // write the character
        break;
    }

    // What is the purpose of this? 你这一个问号合适吗？你都不知道？
    // 看起来是整个屏幕满了之后实现屏幕滚动的功能，就是把缓存区整个往上翻一行。这功能还是很明白的吧。
    if (crt_pos >= CRT_SIZE) {
        int i;
        memmove(crt_buf, crt_buf + CRT_COLS, (CRT_SIZE - CRT_COLS) * sizeof(uint16_t));
        for (i = CRT_SIZE - CRT_COLS; i < CRT_SIZE; i ++) {
            crt_buf[i] = 0x0700 | ' ';
        }
        crt_pos -= CRT_COLS;
    }

    // move that little blinky thing
    // little blinky thing？就是指光标。移动光标位置。我感觉这肯定不是清华老师写的，虽然他看起来也逗比，但不像这种幽默风格🙄 
    // 看来outb的输出数据中如果有高位，会直接被舍弃的。
    outb(addr_6845, 14);
    outb(addr_6845 + 1, crt_pos >> 8);
    outb(addr_6845, 15);
    outb(addr_6845 + 1, crt_pos);
}

static void
serial_putc_sub(int c) {
    int i;
    for (i = 0; !(inb(COM1 + COM_LSR) & COM_LSR_TXRDY) && i < 12800; i ++) {
        delay();
    }
    outb(COM1 + COM_TX, c);
}

/* serial_putc - print character to serial port */
static void
serial_putc(int c) {
    if (c != '\b') {
        serial_putc_sub(c);
    }
    else {
        serial_putc_sub('\b');
        serial_putc_sub(' ');
        serial_putc_sub('\b');
    }
}

/* *
 * Here we manage the console input buffer, where we stash characters
 * received from the keyboard or serial port whenever the corresponding
 * interrupt occurs.
 * */

#define CONSBUFSIZE 512

static struct {
    uint8_t buf[CONSBUFSIZE];
    uint32_t rpos;  //指示读取的字符的位置，readpos我猜
    uint32_t wpos;  //指示写入的字符的位置，writepos的意思我猜
} cons;

/* *
 * cons_intr - called by device interrupt routines to feed input
 * characters into the circular console input buffer.
 * */
//下面程序是个处理硬件中断的通用程序，用来从硬件读取输入（feed input），放入环形的终端缓存
static void
cons_intr(int (*proc)(void)) {
    //接受一个不接受参数，返回int的函数的指针为参数。
    int c;
    while ((c = (*proc)()) != -1) {
        //等于-1说明键盘不可读，返回0表示从0x60端口读出来的值是e0，即需要再读一个byte来确定到底输入了什么
        //如果不等于0的话，那就把这个值放到终端的缓存里面去。
        if (c != 0) {
            cons.buf[cons.wpos ++] = c;
            if (cons.wpos == CONSBUFSIZE) {
                //果然是环形缓存
                cons.wpos = 0;
            }
        }
    }
}

/* serial_proc_data - get data from serial port */
static int
serial_proc_data(void) {
    if (!(inb(COM1 + COM_LSR) & COM_LSR_DATA)) {
        return -1;
    }
    int c = inb(COM1 + COM_RX);
    if (c == 127) {
        c = '\b';
    }
    return c;
}

/* serial_intr - try to feed input characters from serial port */
void
serial_intr(void) {
    if (serial_exists) {
        cons_intr(serial_proc_data);
    }
}

/***** Keyboard input code *****/

#define NO              0

#define SHIFT           (1<<0)
#define CTL             (1<<1)
#define ALT             (1<<2)

#define CAPSLOCK        (1<<3)
#define NUMLOCK         (1<<4)
#define SCROLLLOCK      (1<<5)

#define E0ESC           (1<<6)

static uint8_t shiftcode[256] = {
    //用的是scancode set1无疑
    [0x1D] CTL,
    [0x2A] SHIFT,
    [0x36] SHIFT,
    [0x38] ALT,
    [0x9D] CTL,
    [0xB8] ALT
};

static uint8_t togglecode[256] = {
    [0x3A] CAPSLOCK,
    [0x45] NUMLOCK,
    [0x46] SCROLLLOCK
};

static uint8_t normalmap[256] = {
    NO,   0x1B, '1',  '2',  '3',  '4',  '5',  '6',  // 0x00
    '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
    'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',  // 0x10
    'o',  'p',  '[',  ']',  '\n', NO,   'a',  's',
    'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',  // 0x20
    '\'', '`',  NO,   '\\', 'z',  'x',  'c',  'v',
    'b',  'n',  'm',  ',',  '.',  '/',  NO,   '*',  // 0x30
    NO,   ' ',  NO,   NO,   NO,   NO,   NO,   NO,
    NO,   NO,   NO,   NO,   NO,   NO,   NO,   '7',  // 0x40
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
    '2',  '3',  '0',  '.',  NO,   NO,   NO,   NO,   // 0x50
    [0xC7] KEY_HOME,    [0x9C] '\n' /*KP_Enter*/,
    [0xB5] '/' /*KP_Div*/,  [0xC8] KEY_UP,
    [0xC9] KEY_PGUP,    [0xCB] KEY_LF,
    [0xCD] KEY_RT,      [0xCF] KEY_END,
    [0xD0] KEY_DN,      [0xD1] KEY_PGDN,
    [0xD2] KEY_INS,     [0xD3] KEY_DEL
};

static uint8_t shiftmap[256] = {
    NO,   033,  '!',  '@',  '#',  '$',  '%',  '^',  // 0x00
    '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',  // 0x10
    'O',  'P',  '{',  '}',  '\n', NO,   'A',  'S',
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',  // 0x20
    '"',  '~',  NO,   '|',  'Z',  'X',  'C',  'V',
    'B',  'N',  'M',  '<',  '>',  '?',  NO,   '*',  // 0x30
    NO,   ' ',  NO,   NO,   NO,   NO,   NO,   NO,
    NO,   NO,   NO,   NO,   NO,   NO,   NO,   '7',  // 0x40
    '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
    '2',  '3',  '0',  '.',  NO,   NO,   NO,   NO,   // 0x50
    [0xC7] KEY_HOME,    [0x9C] '\n' /*KP_Enter*/,
    [0xB5] '/' /*KP_Div*/,  [0xC8] KEY_UP,
    [0xC9] KEY_PGUP,    [0xCB] KEY_LF,
    [0xCD] KEY_RT,      [0xCF] KEY_END,
    [0xD0] KEY_DN,      [0xD1] KEY_PGDN,
    [0xD2] KEY_INS,     [0xD3] KEY_DEL
};

#define C(x) (x - '@')

static uint8_t ctlmap[256] = {
    NO,      NO,      NO,      NO,      NO,      NO,      NO,      NO,
    NO,      NO,      NO,      NO,      NO,      NO,      NO,      NO,
    C('Q'),  C('W'),  C('E'),  C('R'),  C('T'),  C('Y'),  C('U'),  C('I'),
    C('O'),  C('P'),  NO,      NO,      '\r',    NO,      C('A'),  C('S'),
    C('D'),  C('F'),  C('G'),  C('H'),  C('J'),  C('K'),  C('L'),  NO,
    NO,      NO,      NO,      C('\\'), C('Z'),  C('X'),  C('C'),  C('V'),
    C('B'),  C('N'),  C('M'),  NO,      NO,      C('/'),  NO,      NO,
    [0x97] KEY_HOME,
    [0xB5] C('/'),      [0xC8] KEY_UP,
    [0xC9] KEY_PGUP,    [0xCB] KEY_LF,
    [0xCD] KEY_RT,      [0xCF] KEY_END,
    [0xD0] KEY_DN,      [0xD1] KEY_PGDN,
    [0xD2] KEY_INS,     [0xD3] KEY_DEL
};

static uint8_t *charcode[4] = {
    normalmap,
    shiftmap,
    ctlmap,
    ctlmap
};

/* *
 * kbd_proc_data - get data from keyboard
 *
 * The kbd_proc_data() function gets data from the keyboard.
 * If we finish a character, return it, else 0. And return -1 if no data.
 * */
static int
kbd_proc_data(void) {
    int c;
    uint8_t data;
    static uint32_t shift;

    if ((inb(KBSTATP) & KBS_DIB) == 0) {
        //读取键盘端口0x64的值，如果最后一位为0，则说明键盘的输出缓存状态为空。
        //0x64这个端口读时为键盘状态寄存器，写时为键盘命令寄存器。
        //https://wiki.osdev.org/%228042%22_PS/2_Controller#Buffer_Naming_Perspective
        //这是古老的PS/2端口了，也就是那种圆口。现在计算机早都淘汰了，然而依然通过兼容处理保持了兼容。
        return -1;
    }

    data = inb(KBDATAP);

    //http://kbd-project.org/docs/scancodes/scancodes-1.html 这些古老的文档啊，真是找死我了
    //Below I'll only mention the scancode for key press (`make'). The scancode for key release (`break')
    //is obtained from it by setting the high order bit (adding 0x80 = 128). 这两句话够理解这一段看不明白的代码了
    //再根据下面的代码推断，ucore使用的scancode应该是scancode set1，已经弃用，但好的是，反正大部分机子会有翻译将set2翻译成set1
    //之所以这么推断，是因为代码里键盘的break code就是make code直接加上0xC8，这是scancode set1的做法
    //https://www.w3.org/2002/09/tests/keys.html 同时发现了这个好玩的页面
    //于是虽然还是搞不懂为什么这里写E0是escape character，但是我理解这里的E0就是扩展代码的。比如e0-38是右alt，e0-1d是右ctrl，etc.
    //这样下面的这一段if/else组合含义就比较明显了。如果是E0的话，需要读入下一个code才知道键盘到底发生了什么。如果下一个是0x80，那就说明
    //这是个键盘break code（键盘释放的意思），如果不是说明是个make（键盘按下码）
    //总之这一段就是处理e0的，因为e0总是要再读一个byte才知道到底表示什么键盘活动
    if (data == 0xE0) {
        // E0 escape character
        shift |= E0ESC;         //EOESC 0x40
        return 0;
    } else if (data & 0x80) {
        // Key released 看shiftcode数组，看来对code set1支持也不是很全
        data = (shift & E0ESC ? data : data & 0x7F);
        shift &= ~(shiftcode[data] | E0ESC);
        return 0;
    } else if (shift & E0ESC) {
        // Last character was an E0 escape; or with 0x80
        data |= 0x80;
        shift &= ~E0ESC;
    }

    shift |= shiftcode[data];
    //togglecode是一些锁定键，如CAPSLOCK,NUMSLOCK,SCROLLLOCK
    shift ^= togglecode[data];

    //处理ctrl shift等特殊键，转化大小写
    c = charcode[shift & (CTL | SHIFT)][data];
    if (shift & CAPSLOCK) {
        if ('a' <= c && c <= 'z')
            c += 'A' - 'a';
        else if ('A' <= c && c <= 'Z')
            c += 'a' - 'A';
    }

    // Process special keys
    // Ctrl-Alt-Del: reboot
    if (!(~shift & (CTL | ALT)) && c == KEY_DEL) {
        cprintf("Rebooting!\n");
        outb(0x92, 0x3); // courtesy of Chris Frost，不会返回的函数
    }
    return c;
}

/* kbd_intr - try to feed input characters from keyboard */
static void
kbd_intr(void) {
    cons_intr(kbd_proc_data);
}

static void
kbd_init(void) {
    // drain the kbd buffer
    // kbd是指键盘keyboard，所以这个意思是清空键盘缓存
    kbd_intr();
    pic_enable(IRQ_KBD);
}

/* cons_init - initializes the console devices */
void
cons_init(void) {
    cga_init();
    serial_init();
    kbd_init();
    if (!serial_exists) {
        cprintf("serial port does not exist!!\n");
    }
}

/* cons_putc - print a single character @c to console devices */
void
cons_putc(int c) {
    lpt_putc(c);
    cga_putc(c);
    serial_putc(c);
}

/* *
 * cons_getc - return the next input character from console,
 * or 0 if none waiting.
 * */
int
cons_getc(void) {
    int c;

    // poll for any pending input characters,
    // so that this function works even when interrupts are disabled
    // (e.g., when called from the kernel monitor).
    serial_intr();
    kbd_intr();

    // grab the next character from the input buffer.
    if (cons.rpos != cons.wpos) {
        c = cons.buf[cons.rpos ++];
        if (cons.rpos == CONSBUFSIZE) {
            cons.rpos = 0;
        }
        return c;
    }
    return 0;
}

