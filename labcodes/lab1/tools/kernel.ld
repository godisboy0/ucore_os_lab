/* Simple linker script for the JOS kernel.
   See the GNU ld 'info' manual ("info ld") to learn the syntax. */

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
/* 指定了kern_init是程序入口，也就是说bootmain()之后会调用这个函数 */
ENTRY(kern_init)

SECTIONS {
	/* Load the kernel at this address: "." means the current address */
	. = 0x100000;

	.text : {
		*(.text .stub .text.* .gnu.linkonce.t.*)
	}

	PROVIDE(etext = .);	/* Define the 'etext' symbol to this value */

	.rodata : {
		*(.rodata .rodata.* .gnu.linkonce.r.*)
	}

	/* Include debugging information in kernel memory */
	.stab : {
		PROVIDE(__STAB_BEGIN__ = .);
		*(.stab);	//意思是将所有文件的.stab段合成一整个.stab段的
		PROVIDE(__STAB_END__ = .);
		BYTE(0)		/* Force the linker to allocate space
				   for this section */
	}

	.stabstr : {
		PROVIDE(__STABSTR_BEGIN__ = .);
		*(.stabstr);
		PROVIDE(__STABSTR_END__ = .);
		BYTE(0)		/* Force the linker to allocate space
				   for this section */
	}

	/* Adjust the address for the data segment to the next page */
	. = ALIGN(0x1000);

	/* The data segment */
	.data : {
		*(.data)
	}

	PROVIDE(edata = .);
	/* PROVIDE关键字的意思是在链接器层面提供这个符号，如果链接的文件里有定义这个符号，那就采用链接文件中的符号
	 * 如果没有定义，那就用这里提供的符号http://sourceware.org/binutils/docs/ld/PROVIDE.html
	 * 等于小数点中小数点的意思，是当前位置，也就是.data，elf数据段的段尾之后的第一个字节的地址
	*/

	.bss : {
		*(.bss)
	}

	PROVIDE(end = .);

	/DISCARD/ : {
		*(.eh_frame .note.GNU-stack)
	}
}
