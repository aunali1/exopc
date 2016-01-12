#ifndef MEMORY_H_
#define MEMORY_H_

#include "config.h"
#include "bios_offsets.h"
#include <xok/mmu.h>

#ifdef __cplusplus
#define asmlinkage extern "C"
#else
#define asmlinkage
#endif

#ifdef __ELF__
#ifdef __STDC__
#define CISH_INLINE(x) #x
#define CISH(x) x
#else
#define CISH_INLINE(x) "x"
#define CISH(x) x
#endif
#else
#ifdef __STDC__
#define CISH_INLINE(x) "_"#x
#define CISH(x) _##x
#else
#define CISH_INLINE(x) "_x"
#define CISH(x) _/**/x
#endif
#endif

#define BOOT_ADDR     0x7c00


#define INT_OFF(i) ((i) << 4)

#define INT09_SEG	BIOSSEG
#define INT09_OFF	0xe987		/* for 100% IBM compatibility */
#define INT09_ADD	((INT09_SEG << 4) + INT09_OFF)

#define LASTSCAN_SEG	(BIOSSEG + 0x100)
#define LASTSCAN_OFF	0x0
#define LASTSCAN_ADD	(u_short *)((LASTSCAN_SEG << 4) + LASTSCAN_OFF)

#define OUTB_SEG	(BIOSSEG+0x100)
#define OUTB_OFF	0x1000
#define OUTB_ADD	(u_short *)((OUTB_SEG << 4) + OUTB_OFF)


/* The packet driver has some code in this segment which needs to be */
/* at BIOSSEG.  therefore use BIOSSEG and compensate for the offset. */
/* Memory required is about 2000 bytes, beware! */
#define PKTDRV_SEG	(BIOSSEG)
#define PKTDRV_OFF	0x3100
#define PKTDRV_ADD	((PKTDRV_SEG << 4) + PKTDRV_OFF)

/* don't change these for now, they're hardwired! */
#define Mouse_SEG       (BIOSSEG)
#define Mouse_OFF       0x20f0
#define Mouse_ROUTINE_OFF  0x2140
#define Mouse_ADD      ((Mouse_SEG << 4)+Mouse_OFF)
#define Mouse_ROUTINE  ((Mouse_SEG << 4)+Mouse_ROUTINE_OFF)

/* intercept-stub for dosdebugger (catches INT21/AX=4B00 */
#define DBGload_SEG BIOSSEG
#define DBGload_OFF (0x8000 - 128)

#define XMSControl_SEG  ROMBIOSSEG
#define XMSControl_OFF  0
#define XMSControl_ADD  ((XMSControl_SEG << 4)+XMSControl_OFF)
#define XMSTrap_ADD     ((XMSControl_SEG << 4)+XMSControl_OFF+5)

/* EMS origin must be at 0 */
#define EMS_SEG		(ROMBIOSSEG+0x100)
#define EMS_OFF		0x0000
#define EMS_ADD		((EMS_SEG << 4) + EMS_OFF)

#define EMM_BASE_ADDRESS        (config.ems_frame << 4)
#define EMM_SEGMENT             (config.ems_frame)

#define INT16_SEG	ROMBIOSSEG
#define INT16_OFF	0x3500
#define INT16_ADD	((INT16_SEG << 4) + INT16_OFF)

#define IPX_SEG		ROMBIOSSEG
#define IPX_OFF		0x3100
#define IPX_ADD		((IPX_SEG << 4) + IPX_OFF)

#define INT08_SEG	ROMBIOSSEG
#define INT08_OFF	0x4000
#define INT08_ADD	((INT08_SEG << 4) + INT08_OFF)

#ifdef NEW_CMOS
#define INT70_SEG	ROMBIOSSEG
#define INT70_OFF	0x4100
#define INT70_ADD	((INT70_SEG << 4) + INT70_OFF)
#endif

#define INT10_SEG	ROMBIOSSEG
#define INT10_OFF	0x4200
#define INT10_ADD	((INT10_SEG << 4) + INT10_OFF)

/* int10 watcher for mouse support */
/* This was in BIOSSEG (a) so we could write old_int10,
 * when it made a difference...
 */
#define INT10_WATCHER_SEG	ROMBIOSSEG
#define INT10_WATCHER_OFF	0x4240

/* This inline interrupt is used for FCB open calls */
#define INTE7_SEG	ROMBIOSSEG
#define INTE7_OFF	0x4500
#define INTE7_ADD	((INTE7_SEG << 4) + INTE7_OFF)

#define PIC_SEG       ROMBIOSSEG
#define PIC_OFF       0x47f0
#define PIC_ADD       ((PIC_SEG << 4) + PIC_OFF)

#define DPMI_SEG	ROMBIOSSEG
#define DPMI_OFF	0x4800		/* need at least 512 bytes */
#define DPMI_ADD	((DPMI_SEG << 4) + DPMI_OFF)

#define VBIOS_START	(config.vbios_seg << 4 )
/* #define VBIOS_SIZE	(64*1024) */
#define VBIOS_SIZE	(config.vbios_size)
#define GFX_CHARS	0xffa6e
#define GFXCHAR_SIZE	1400

/* Memory adresses for all common video adapters */

#define MDA_PHYS_TEXT_BASE  0xB0000
#define MDA_VIRT_TEXT_BASE  0xB0000

#define CGA_PHYS_TEXT_BASE  0xB8000
#define CGA_VIRT_TEXT_BASE  0xB8000

#define EGA_PHYS_TEXT_BASE  0xB8000
#define EGA_VIRT_TEXT_BASE  0xB8000

#define VGA_PHYS_TEXT_BASE  0xB8000
#define VGA_VIRT_TEXT_BASE  0xB8000

#define CO      80 /* A-typical screen width */
#define LI      25 /* Normal rows on a screen */
#define TEXT_SIZE	(((li*co*2)+4095)&(~4095)) /* 4000(4096?) text page size */
#define PAGE_ADDR(pg)	(caddr_t)(virt_text_base + ((pg)*TEXT_SIZE))
#define SCREEN_ADR(s)	((u_short *)(virt_text_base + ((s)*TEXT_SIZE)))

#define GRAPH_BASE 0xA0000
#define GRAPH_SIZE 0x20000

#define BIOS_DATA_SEG   (0x400)	/* for absolute adressing */
#define BIOS_DATA_PTR(off) ((void *) (BIOS_DATA_SEG + off))

/* Bios data area keybuffer */
#if 0
#define KBD_Start (*(unsigned short *)BIOS_DATA_PTR(0x80))
#define KBD_End  (*(unsigned short *)BIOS_DATA_PTR(0x82))
#define KBD_Head (*(unsigned short *)BIOS_DATA_PTR(0x1A))
#define KBD_Tail (*(unsigned short *)BIOS_DATA_PTR(0x1C))
#endif

#define KBD_START BIOS_DATA_PTR(0x80)
#define KBD_END   BIOS_DATA_PTR(0x82)
#define KBD_HEAD  BIOS_DATA_PTR(0x1A)
#define KBD_TAIL  BIOS_DATA_PTR(0x1C)

#define KBDFLAG_ADDR (unsigned short *)BIOS_DATA_PTR(0x17)
#define KEYFLAG_ADDR (unsigned short *)BIOS_DATA_PTR(0x96)

#ifndef __ASM__
/* memcheck memory conflict finder definitions */
int  memcheck_addtype(unsigned char map_char, char *name);
void memcheck_reserve(unsigned char map_char, int addr_start, int size);
void memcheck_reserve_aux(unsigned char map_char, int addr_start, int size, u_int aux);
void memcheck_init(void);
int  memcheck_isfree(int addr_start, int size);
int  memcheck_findhole(int *start_addr, int min_size, int max_size);
void memcheck_dump(void);
#endif

int memmap(void *start, size_t length, int prot, void *offset);

#endif /* MEMORY_H */
