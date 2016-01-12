/* cmos.c, for DOSEMU
 *   by Robert Sanders, gt8134b@prism.gatech.edu
 *
 */

#include <time.h>
#include <sys/time.h>

#include "debug.h"
#include "emu.h"
#include "config.h"
#include "port.h"
#include "cmos.h"
#include "disks.h"


struct CMOS cmos;

#define EXTMEM_SIZE ((config.xms_size>config.ems_size)?config.xms_size : \
		     config.ems_size)
#define PEXTMEM_SIZE (((config.xms_size>config.ems_size)?config.xms_size : \
		       config.ems_size)/1024)
#define SET_CMOS(byte,val)  do { cmos.subst[byte] = (val); cmos.flag[byte] = 1; } while(0)


static int cmos_chksum(void)
{
    int i, sum = 0;
    
    /* return the checksum over bytes 0x10-0x20. These are static values,
     * so no need to call cmos_read()
     */
    
    for (i = 0x10; i < 0x21; i++)
	sum += cmos.subst[i];
    
    return sum;
}

Bit8u cmos_read(Bit32u port)
{
    unsigned char holder = 0;
    
    trace_cmos("CMOS read from address 0x%02x\n", cmos.address);
    
    switch (cmos.address) {
    case CMOS_SEC:
    case CMOS_MIN:
    case CMOS_HOUR:
    case CMOS_DOW:		/* day of week */
    case CMOS_DOM:		/* day of month */
    case CMOS_MONTH:
    case CMOS_YEAR:
	return (cmos_date(cmos.address));
	
    case CMOS_SECALRM:
    case CMOS_MINALRM:
    case CMOS_HOURALRM:
	dprintf("CMOS alarm read %d unimplemented\n", cmos.address);
	leaveemu(ERR_UNIMPL);
	
	return cmos.subst[cmos.address];
	
    case CMOS_CHKSUML:
	return (cmos_chksum() & 0xff);
	
    case CMOS_CHKSUMM:
	return (cmos_chksum() >> 8);
    }
    
    /* date functions return, so hereafter all values should be those set
     * either at boot time or changed by DOS programs...
     */
    
    if (cmos.flag[cmos.address]) {/* this reg has been written to */
	holder = cmos.subst[cmos.address];

	if (cmos.address == CMOS_STATUSA) {
	    struct timeval tp;
	    gettimeofday(&tp, 0);
	    if (tp.tv_usec < 3000) { /* update cycle in progress */
		holder |= 0x80;
	    }
	}
	debug_cmos("CMOS substituting written value 0x%02x for read\n", holder);
    }
#ifdef DANGEROUS_CMOS
    else if (!set_ioperm(0x70, 2, 1)) {
	debug_cmos("CMOS really reading 0x%x!\n", cmos.address);
	port_out((cmos.address & ~0xc0), 0x70);
	holder = port_in(0x71);
	set_ioperm(0x70, 2, 0);
    }
#endif
    else {
	error_cmos("CMOS unknown read 0x%x\n", cmos.address);
	holder = cmos.subst[cmos.address];
    }
    
    debug_cmos("CMOS read address 0x%02x = 0x%02x\n", cmos.address, holder);
    return holder;
}

void cmos_write(Bit32u port, Bit8u byte)
{
    if (port == 0x70)
	cmos.address = byte & ~0xc0;/* get true address */
    else {
	if ((cmos.address != 0xc) && (cmos.address != 0xd)) {
	    debug_cmos("CMOS set address 0x%02x to 0x%02x\n", cmos.address, byte);
	    SET_CMOS(cmos.address, byte);
	}
	else
	    debug_cmos("CMOS write to address 0x%x blocked\n", cmos.address);
    }
}

void cmos_init(void)
{
    emu_iodev_t  io_device;
    
    /* CMOS RAM & RTC */
    io_device.read_portb   = cmos_read;
    io_device.write_portb  = cmos_write;
    io_device.read_portw   = NULL;
    io_device.write_portw  = NULL;
    io_device.handler_name = "CMOS RAM";
    io_device.start_addr   = 0x0070;
    io_device.end_addr     = 0x0071;
    io_device.irq          = EMU_NO_IRQ;
    port_register_handler(io_device);
}

void cmos_reset(void)
{
    int i;
    
    trace("in cmos_reset\n");

    for (i = 0; i < 64; i++)
	cmos.subst[i] = cmos.flag[i] = 0;
    
    /* CMOS floppies...is this correct? */
    SET_CMOS(CMOS_DISKTYPE, 
	     (config.fdisks ? (fdisktab[0].cmos << 4) : 0) |
	     ((config.fdisks > 1) ? fdisktab[1].cmos & 0xf : 0));
    
    /* CMOS equipment byte..top 2 bits are 01 for 2 drives, 00 for 1
     * bit 1 is 1 for math coprocessor installed
     * bit 0 is 1 for floppies installed, 0 for none */
    
    cmos.subst[0x14] = ((config.fdisks ? config.fdisks - 1 : 0) << 5) +
	(config.fdisks ? 1 : 0);
    if (config.mathco)
	cmos.subst[0x14] |= 2;
    cmos.flag[0x14] = 1;
    
    /* CMOS hard disks...type 47 for both. */
    SET_CMOS(CMOS_HDTYPE, (config.hdisks ? 0xf0 : 0) +
	     ((config.hdisks - 1) ? 0xf : 0));
    SET_CMOS(CMOS_HD1EXT, 47);
    if (config.hdisks == 2)
	SET_CMOS(CMOS_HD2EXT, 47);
    else
	SET_CMOS(CMOS_HD2EXT, 0);
    
    /* this is the CMOS status */
    SET_CMOS(CMOS_STATUSA, 0x26);
    SET_CMOS(CMOS_STATUSB, 2);
    
    /* 0xc and 0xd are read only */
    SET_CMOS(CMOS_STATUSC, 0x50);
    SET_CMOS(CMOS_STATUSD, 0x80);
    
    SET_CMOS(CMOS_DIAG, 0);
    
    /* memory counts */
    SET_CMOS(CMOS_BASEMEML, config.mem_size & 0xff);      /* base mem LSB */
    SET_CMOS(CMOS_BASEMEMM, config.mem_size >> 8);        /* base mem MSB */
    
    SET_CMOS(CMOS_EXTMEML, EXTMEM_SIZE & 0xff);
    SET_CMOS(CMOS_EXTMEMM, EXTMEM_SIZE >> 8);
    
    SET_CMOS(CMOS_PEXTMEML, PEXTMEM_SIZE & 0xff);
    SET_CMOS(CMOS_PEXTMEMM, PEXTMEM_SIZE >> 8);
    
    /* say protected mode test 7 passed (?) */
    SET_CMOS(CMOS_SHUTDOWN, 6);
    
    /* information flags...my CMOS returns this */
    SET_CMOS(CMOS_INFO, 0xe1);
    
    trace_cmos("CMOS initialized\n");
}
