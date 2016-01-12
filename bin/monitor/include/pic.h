/* Priority Interrupt Controller Emulation 
   Copyright (C) 1994, J. Lawrence Stephan  jlarry@ssnet.com 

   This software is distributed under the terms of the GNU General Public 
   License, there are NO WARRANTIES, expressed or implied.  Use at your own 
   risk.  Use of this software as a part of any product is permitted only 
   if that  product is also distributed under the terms of the GNU General 
   Public  License.  
   


 Definitions for 8259A programmable Interrupt Controller Emulation 
  

  IRQ priority levels.  These are in order of priority, lowest number has
   highest priority.  Levels 16 - 30 are available for use, but currently
   have no fixed assignment.  They are extras for the convenience of dosemu.
   31 is tentatively reserved for a watchdog timer  (stall alarm).  
   
   IRQ2 does not exist.  A PCs IRQ2 line is actually connected to IRQ9.  The
   bios for IRQ9 does an int1a, then sends an EOI to the second PIC. ( This
   is simulated in the PIC code if IRQ9 points to the bios segment.)  For
   this reason there is no PIC_IRQ2.
  
*/
   
/* Valuse from 16 - 31 are available for assignment if not already 
 * assigned below.  They can be used to activate dos interrupts not
 * associated to the hardware pic controllers, or simply to activate
 * other dosemu code.  This is best used to initiate non-signal dosemu
 * code from within a signal handler.
 */


#ifndef PIC
#define PIC

#include "types.h"

#ifndef INIT
#define INIT(x...)
#endif


#define NEVER 0x80000000
#define PIC_NMI   0        /*  non-maskable interrupt 0x02 */
#define PIC_IRQ0  1        /*  timer -    usually int 0x08 */
#define PIC_IRQ1  2        /*  keyboard - usually int 0x09 */
#define PIC_IRQ8  3        /*  real-time clock "  int 0x70 */
#define PIC_IRQ9  4        /*  int 0x71 revectors to  0x0a */
#define PIC_IRQ10 5        /*             usually int 0x72 */
#define PIC_IRQ11 6        /*             usually int 0x73 */
#define PIC_IRQ12 7        /*             usually int 0x74 */
#define PIC_IRQ13 8        /*             usually int 0x75 */
#define PIC_IRQ14 9        /*  disk       usually int 0x76 */
#define PIC_IRQ15 10       /*             usually int 0x77 */
#define PIC_IRQ3  11       /*  COM 2      usually int 0x0b */
#define PIC_IRQ4  12       /*  COM 1      usually int 0x0c */
#define PIC_IRQ5  13       /*  LPT 2      usually int 0x0d */
#define PIC_IRQ6  14       /*  floppy     usually int 0x0e */
#define PIC_IRQ7  15       /*  LPT 1      usually int 0x0f */
#define PIC_NET   16       /*  packet receive check - no dos equivalent */
#define PIC_IMOUSE 17      /*  internal mouse driver       */
#define PIC_IPX    18      /*  IPX Signal */

#define PIC_IRQALL 0xfffe  /*  bits for all IRQs set. This never changes  */

/* pic_irq_list translates irq numbers to pic_ilevels.  This is not used
   by pic routines; it is simply made available for configuration ease */
extern unsigned long pic_irq_list[] INIT({PIC_IRQ0,  PIC_IRQ1,  PIC_IRQ9,  PIC_IRQ3,
                               PIC_IRQ4,  PIC_IRQ5,  PIC_IRQ6,  PIC_IRQ7,
                               PIC_IRQ8,  PIC_IRQ9,  PIC_IRQ10, PIC_IRQ11,
                               PIC_IRQ12, PIC_IRQ13, PIC_IRQ14, PIC_IRQ15});

/* pic_level_list translates  pic_ilevels to irq numbers.  This is not used
   by pic routines; it is simply made available for configuration ease */
extern unsigned long pic_level_list[] INIT({
                 -1, 0, 1, 8, 9, 10, 11, 12, 13, 14, 15, 3, 4, 5, 6, 7 });

/* Some dos extenders modify interrupt vectors, particularly for IRQs 0-7 */

/* PIC "registers", plus a few more */

extern unsigned long pic_irr;          /* interrupt request register */
extern unsigned long pic_isr;          /* interrupt in-service register */
extern unsigned long pic_iflag;        /* interrupt enable flag: en-/dis- =0/0xfffe */
extern unsigned long pic_icount;       /* iret counter (to avoid filling stack) */
extern unsigned long pic_ilevel INIT(32);    /* current interrupt level */

extern unsigned long pic0_imr INIT(0xf800);  /* interrupt mask register, pic0 */
extern unsigned long pic1_imr INIT(0x07f8);         /* interrupt mask register, pic1 */
extern unsigned long pic_imr INIT(0xfff8);          /* interrupt mask register */
extern unsigned long pice_imr INIT(-1);         /* interrupt mask register, dos emulator */  
extern unsigned long pic_stack[32];     /* list of active irqd */
extern unsigned long pic_sp INIT(0);	       /* pointer to pic_stack */ 
extern unsigned long pic_rflag;        /* flag to control pic_watch */
extern unsigned long pic_vm86_count INIT(0);   /* count of times 'round the vm86 loop*/
extern unsigned long pic_dpmi_count INIT(0);   /* count of times 'round the dpmi loop*/
extern long pic_dos_time;     /* dos time of last interrupt,1193047/sec.*/
extern long pic_sys_time INIT(NEVER);     /* system time set by pic_watch */
/* IRQ definitions.  Each entry specifies the emulator routine to call, and
   the dos interrupt vector to use.  pic_iinfo.func is set by pic_seti(),
   as are the interrupt vectors for all but [1] through [15], which  may be
   changed by the ICW2 command from dos. (Some dos extenders do this.) */
   
struct lvldef {
       void (*func)();
       int    ivec;
       };

/* function definitions - level refers to irq priority level defined above  */
/* port = 0 or 1     value = 0 - 0xff   level = 0 - 31    ivec = 0 - 0xff   */

void write_pic0(Bit32u port, Bit8u value); /* write to PIC 0 */
void write_pic1(Bit32u port, Bit8u value); /* write to PIC 1 */
Bit8u read_pic0(Bit32u port);             /* read from PIC 0 */
Bit8u read_pic1(Bit32u port);             /* read from PIC 1 */
void pic_unmaski(int level);                 /* clear dosemu's irq mask bit */
void pic_maski(int level);                   /*  set  dosemu's irq mask bit */
void pic_seti(unsigned int level, void (*func), unsigned int ivec); 
                                       /* set function and interrupt vector */
void run_irqs();                                      /* run requested irqs */
#define pic_run() if(pic_irr)run_irqs()   /* the right way to call run_irqs */
int do_irq();                                /* run dos portion of irq code */
int pic_request(int inum);                            /* interrupt trigger */
void pic_creq(int inum);                   /* conditional interrupt trigger */
void pic_iret();                             /* interrupt completion notify */
void pic_watch();		       /* interrupt pending watchdog timer */
void do_irq0();						 /* timer interrupt */
int  pic_pending();		   /* inform caller if interrupt is pending */
void pic_sched(int ilevel, int interval);          /* schedule an interrupt */
/* The following are too simple to be anything but in-line */

#define pic_set_mask pic_imr=(pic0_imr|pic1_imr|pice_imr|pic_iflag)
#define pic_sti() pic_iflag=0;pic_set_mask          /*    emulate STI      */
#define pic_cli() pic_iflag=PIC_IRQALL;pic_set_mask /*    emulate CLI      */
#endif

/* Experimental TIMER-IRQ CHAIN code */
extern void timer_int_engine(void);

extern void virt_pic_reset(void);
extern void virt_pic_init(void);

unsigned char get_pic0_base(void);
