/*
 * Copyright (c) 1995, David Greenman
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * Device driver for National Semiconductor DS8390/WD83C690 based ethernet
 *   adapters. By David Greenman, 29-April-1993
 *
 * Currently supports the Western Digital/SMC 8003 and 8013 series,
 *   the SMC Elite Ultra (8216), the 3Com 3c503, the NE1000 and NE2000,
 *   and a variety of similar clones.
 *
 */


/* Tom: add new ports/shm addresses here for the card to probe for 
   them. */

static int ed_ports[] = {0x240, 0x280, 0x300};
static int ed_shm[] = {0xd0000, 0xcc000, 0xd4000, 0xd8000};

#define NEDTOT (NED + EXTRA_ED)

#if defined(EXOPC)
#define __NETWORK_MODULE__

#include <xok/sysinfo.h>
#include <xok_include/assert.h>
#include <xok/defs.h>
#include <xok/mmu.h>
#include <machine/pio.h>
#include <xok/picirq.h>
#include <xok/printf.h>
#include "if_devar.h"
#include <xok/sys_proto.h>
#include <xok/if_edreg.h>
#include <xok/isa_device.h>
#include <xok/pkt.h>
#include <xok/init.h>
#include <xok/cpu.h>
#include <xok/apic.h>
#include <xok/mplock.h>


#define DELAY delay
#define kvtop kva2pa
static int inline splimp () {return 0;}
#define ETHER_CRC_LEN 4

#define MAXSMC	1

/*
 * ed_softc: per line info and status
 */
struct ed_softc {

	struct network *xoknet;	/* corresponding xoknet structure */

        u_char macaddr[ETHER_ADDR_LEN]; /* Ethernet address */
        char   *type_str;       /* pointer to type string */
        u_char  vendor;         /* interface vendor */
        u_char  type;           /* interface type code */
        u_char  gone;           /* HW missing, presumed having a good time */
#define IFF_ALTPHYS IFF_LINK0
#define IFF_LINK0       0x1000          /* per link layer defined bit */
        u_char flags;           /* simulate the interface-level flags */

        u_short asic_addr;      /* ASIC I/O bus address */
        u_short nic_addr;       /* NIC (DS8390) I/O bus address */

/*
 * The following 'proto' variable is part of a work-around for 8013EBT asics
 *      being write-only. It's sort of a prototype/shadow of the real thing.
 */
        u_char  wd_laar_proto;
        u_char  cr_proto;
        u_char  isa16bit;       /* width of access to card 0=8 or 1=16 */
        int     is790;          /* set by the probe code if the card is 790
                                 * based */

/*
 * HP PC LAN PLUS card support.
 */

        u_short hpp_options;    /* flags controlling behaviour of the HP card */
        u_short hpp_id;         /* software revision and other fields */
        caddr_t hpp_mem_start;  /* Memory-mapped IO register address */

        caddr_t mem_start;      /* NIC memory start address */
        caddr_t mem_end;        /* NIC memory end address */
        u_long  mem_size;       /* total NIC memory size */
        caddr_t mem_ring;       /* start of RX ring-buffer (in NIC mem) */

        u_char  mem_shared;     /* NIC memory is shared with host */
        u_char  xmit_busy;      /* transmitter is busy */
        u_char  txb_cnt;        /* number of transmit buffers */
        u_char  txb_inuse;      /* number of TX buffers currently in-use */

        u_char  txb_new;        /* pointer to where new buffer will be added */
        u_char  txb_next_tx;    /* pointer to next buffer ready to xmit */
        u_short txb_len[8];     /* buffered xmit buffer lengths */
        u_char  tx_page_start;  /* first page of TX buffer area */
        u_char  rec_page_start; /* first page of RX ring-buffer */
        u_char  rec_page_stop;  /* last page of RX ring-buffer */
        u_char  next_packet;    /* pointer to next unread RX packet */
};


static void ed_start		__P((struct ed_softc *));
void edintr			__P((int unit));


#else 

#include "ed.h"
#include "bpfilter.h"
#include "pnp.h"

#ifndef EXTRA_ED
# if NPNP > 0
#  define EXTRA_ED 8
# else
#  define EXTRA_ED 0
# endif
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/sockio.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/syslog.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_mib.h>


#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#if NPNP > 0
#include <i386/isa/pnp.h>
#endif

#include <machine/clock.h>
#include <machine/md_var.h>

#include <i386/isa/isa_device.h>
#include <i386/isa/icu.h>
#include <i386/isa/if_edreg.h>

/*
 * ed_softc: per line info and status
 */
struct ed_softc {
	struct arpcom arpcom;	/* ethernet common */

	char   *type_str;	/* pointer to type string */
	u_char  vendor;		/* interface vendor */
	u_char  type;		/* interface type code */
	u_char	gone;		/* HW missing, presumed having a good time */

	u_short asic_addr;	/* ASIC I/O bus address */
	u_short nic_addr;	/* NIC (DS8390) I/O bus address */

/*
 * The following 'proto' variable is part of a work-around for 8013EBT asics
 *	being write-only. It's sort of a prototype/shadow of the real thing.
 */
	u_char  wd_laar_proto;
	u_char	cr_proto;
	u_char  isa16bit;	/* width of access to card 0=8 or 1=16 */
	int     is790;		/* set by the probe code if the card is 790
				 * based */

/*
 * HP PC LAN PLUS card support.
 */

	u_short	hpp_options;	/* flags controlling behaviour of the HP card */
	u_short hpp_id;		/* software revision and other fields */
	caddr_t hpp_mem_start;	/* Memory-mapped IO register address */

	caddr_t mem_start;	/* NIC memory start address */
	caddr_t mem_end;	/* NIC memory end address */
	u_long  mem_size;	/* total NIC memory size */
	caddr_t mem_ring;	/* start of RX ring-buffer (in NIC mem) */

	u_char  mem_shared;	/* NIC memory is shared with host */
	u_char  xmit_busy;	/* transmitter is busy */
	u_char  txb_cnt;	/* number of transmit buffers */
	u_char  txb_inuse;	/* number of TX buffers currently in-use */

	u_char  txb_new;	/* pointer to where new buffer will be added */
	u_char  txb_next_tx;	/* pointer to next buffer ready to xmit */
	u_short txb_len[8];	/* buffered xmit buffer lengths */
	u_char  tx_page_start;	/* first page of TX buffer area */
	u_char  rec_page_start;	/* first page of RX ring-buffer */
	u_char  rec_page_stop;	/* last page of RX ring-buffer */
	u_char  next_packet;	/* pointer to next unread RX packet */
	struct	ifmib_iso_8802_3 mibdata; /* stuff for network mgmt */
};

static struct ed_softc ed_softc[NEDTOT];

static int ed_attach_isa	__P((struct isa_device *));

#endif

static int ed_attach		__P((struct ed_softc *, int, int));


static void ed_init		__P((void *));
#if 0
static int ed_ioctl		__P((struct ifnet *, int, caddr_t));
static int ed_probe		__P((struct isa_device *));
static void ed_start		__P((struct ifnet *));
static void ed_reset		__P((struct ifnet *));
static void ed_watchdog		__P((struct ifnet *));
#endif

static void ed_stop		__P((struct ed_softc *));
static int ed_probe_generic8390	__P((struct ed_softc *));
static int ed_probe_WD80x3	__P((struct isa_device *));
static int ed_probe_3Com	__P((struct isa_device *));
static int ed_probe_Novell	__P((struct isa_device *));
static int ed_probe_Novell_generic __P((struct ed_softc *, int, int, int));
static int ed_probe_HP_pclanp	__P((struct isa_device *));

#if !defined(EXOPC)

#include "pci.h"
#if NPCI > 0
void *ed_attach_NE2000_pci	__P((int, int));
#endif

#include "card.h"
#if NCARD > 0
static int ed_probe_pccard	__P((struct isa_device *, u_char *));
#endif

#endif 

static void	ds_getmcaf	__P((struct ed_softc *, u_long *));

static void	ed_get_packet	__P((struct ed_softc *, char *, u_short, int));

static void	ed_rint		__P((struct ed_softc *));
static void	ed_xmit		__P((struct ed_softc *));
static char *	ed_ring_copy	__P((struct ed_softc *, char *, char *,
				  /* u_short */ int));
#if !defined(EXOPC)
static void	ed_hpp_set_physical_link __P((struct ed_softc *));
static u_short	ed_hpp_write_mbufs __P((struct ed_softc *, struct mbuf *,
					int));
static u_short	ed_pio_write_mbufs __P((struct ed_softc *, struct mbuf *,
					int));
#endif
static void	ed_hpp_readmem	__P((struct ed_softc *, int, unsigned char *,
				    /* u_short */ int));
static void	ed_pio_readmem	__P((struct ed_softc *, int, unsigned char *,
				    /* u_short */ int));
static void	ed_pio_writemem	__P((struct ed_softc *, char *,
				     /* u_short */ int, /* u_short */ int));
int edintr_sc			__P((struct ed_softc *));

static void	ed_setrcr	__P((struct ed_softc *));

static u_long	ds_crc		__P((u_char *ep));

#if !defined (EXOPC)

#if (NCARD > 0) || (NPNP > 0)
#include <sys/kernel.h>
#endif
#if NCARD > 0
#include <sys/select.h>
#include <pccard/cardinfo.h>
#include <pccard/slot.h>

/*
 *	PC-Card (PCMCIA) specific code.
 */
static int	edinit		__P((struct pccard_devinfo *));
static void	edunload	__P((struct pccard_devinfo *));
static int	card_intr	__P((struct pccard_devinfo *));

static struct pccard_device ed_info = {
	"ed",
	edinit,
	edunload,
	card_intr,
	0,			/* Attributes - presently unused */
	&net_imask		/* Interrupt mask for device */
				/* XXX - Should this also include net_imask? */
};

DATA_SET(pccarddrv_set, ed_info);

/*
 *	Initialize the device - called from Slot manager.
 */
static int
edinit(struct pccard_devinfo *devi)
{
	int i;
	u_char  e;
	struct ed_softc *sc = &ed_softc[devi->isahd.id_unit];

	/* validate unit number. */
	if (devi->isahd.id_unit >= NEDTOT)
		return(ENODEV);
	/*
	 * Probe the device. If a value is returned, the
	 * device was found at the location.
	 */
	sc->gone = 0;
	if (ed_probe_pccard(&devi->isahd, devi->misc) == 0)
		return(ENXIO);
	e = 0;
	for (i = 0; i < ETHER_ADDR_LEN; ++i)
		e |= devi->misc[i];
	if (e)
		for (i = 0; i < ETHER_ADDR_LEN; ++i)
			sc->arpcom.ac_enaddr[i] = devi->misc[i];
	if (ed_attach_isa(&devi->isahd) == 0)
		return(ENXIO);

	return(0);
}

/*
 *	edunload - unload the driver and clear the table.
 *	XXX TODO:
 *	This is usually called when the card is ejected, but
 *	can be caused by a modunload of a controller driver.
 *	The idea is to reset the driver's view of the device
 *	and ensure that any driver entry points such as
 *	read and write do not hang.
 */
static void
edunload(struct pccard_devinfo *devi)
{
	struct ed_softc *sc = &ed_softc[devi->isahd.id_unit];
	struct ifnet *ifp = &sc->arpcom.ac_if;

	if (sc->gone) {
		printf("ed%d: already unloaded\n", devi->isahd.id_unit);
		return;
	}
	ifp->if_flags &= ~IFF_RUNNING;
	if_down(ifp);
	sc->gone = 1;
	printf("ed%d: unload\n", devi->isahd.id_unit);
}

/*
 *	card_intr - Shared interrupt called from
 *	front end of PC-Card handler.
 */
static int
card_intr(struct pccard_devinfo *devi)
{
	edintr_sc(&ed_softc[devi->isahd.id_unit]);
	return(1);
}
#endif /* NCARD > 0 */

struct isa_driver eddriver = {
	ed_probe,
	ed_attach_isa,
	"ed",
	1		/* We are ultra sensitive */
};

#endif /* !defined(EXOPC) */

/*
 * Interrupt conversion table for WD/SMC ASIC/83C584
 * (IRQ* are defined in icu.h)
 */

/* XXX -- hacked for exopc - tom */
static unsigned short ed_intr_mask[] = {9, 3, 5, 7, 10, 11, 15, 4};

/*
 * Interrupt conversion table for 83C790
 */

/* XXX -- hacked for exopc - tom */
static unsigned short ed_790_intr_mask[] = { -1, 9, 3, 5, 7, 10, 11, 15};

/*
 * Interrupt conversion table for the HP PC LAN+
 */

/* XXX -- hacked for exopc - tom */
static unsigned short ed_hpp_intr_mask[] = {0, 0, 0, 3, 4, 5, 6, 7, 0, 9, 
					    10, 11, 12, 0, 0, 15};

/*
 * Determine if the device is present
 *
 *   on entry:
 * 	a pointer to an isa_device struct
 *   on exit:
 *	NULL if device not found
 *	or # of i/o addresses used (if found)
 */
static int
ed_probe(isa_dev)
	struct isa_device *isa_dev;
{
	int     nports;

	nports = ed_probe_WD80x3(isa_dev);
	if (nports)
	  goto done;

	nports = ed_probe_3Com(isa_dev);
	if (nports)
	  goto done;

	nports = ed_probe_Novell(isa_dev);
	if (nports)
	  goto done;

	nports = ed_probe_HP_pclanp(isa_dev);
	if (nports)
	  goto done;

	return (0);

 done:
	irq_sethandler (isa_dev->id_irq, (int (*)(u_int))edintr);
	irq_setmask_8259A (irq_mask_8259A & ~(1 << isa_dev->id_irq));
	irq_eoi (isa_dev->id_irq);
	return (1);
}

/*
 * Generic probe routine for testing for the existance of a DS8390.
 *	Must be called after the NIC has just been reset. This routine
 *	works by looking at certain register values that are guaranteed
 *	to be initialized a certain way after power-up or reset. Seems
 *	not to currently work on the 83C690.
 *
 * Specifically:
 *
 *	Register			reset bits	set bits
 *	Command Register (CR)		TXP, STA	RD2, STP
 *	Interrupt Status (ISR)				RST
 *	Interrupt Mask (IMR)		All bits
 *	Data Control (DCR)				LAS
 *	Transmit Config. (TCR)		LB1, LB0
 *
 * We only look at the CR and ISR registers, however, because looking at
 *	the others would require changing register pages (which would be
 *	intrusive if this isn't an 8390).
 *
 * Return 1 if 8390 was found, 0 if not.
 */

static int
ed_probe_generic8390(sc)
	struct ed_softc *sc;
{
	if ((inb(sc->nic_addr + ED_P0_CR) &
	     (ED_CR_RD2 | ED_CR_TXP | ED_CR_STA | ED_CR_STP)) !=
	    (ED_CR_RD2 | ED_CR_STP))
		return (0);
	if ((inb(sc->nic_addr + ED_P0_ISR) & ED_ISR_RST) != ED_ISR_RST)
		return (0);

	return (1);
}

/*
 * Probe and vendor-specific initialization routine for SMC/WD80x3 boards
 */
static int
ed_probe_WD80x3(isa_dev)
	struct isa_device *isa_dev;
{
#if defined(EXOPC)
  	struct network *nw = SYSINFO_PTR_AT(si_networks,isa_dev->id_xoknetno);
        struct ed_softc *sc = (struct ed_softc *) nw->cardstruct;
#else
	struct ed_softc *sc = &ed_softc[isa_dev->id_unit];
#endif
	int     i;
	u_int   memsize, maddr;
	u_char  iptr, isa16bit, sum;

	sc->asic_addr = isa_dev->id_iobase;
	sc->nic_addr = sc->asic_addr + ED_WD_NIC_OFFSET;
	sc->is790 = 0;

#ifdef TOSH_ETHER
	outb(sc->asic_addr + ED_WD_MSR, ED_WD_MSR_POW);
	DELAY(10000);
#endif

	/*
	 * Attempt to do a checksum over the station address PROM. If it
	 * fails, it's probably not a SMC/WD board. There is a problem with
	 * this, though: some clone WD boards don't pass the checksum test.
	 * Danpex boards for one.
	 */
	for (sum = 0, i = 0; i < 8; ++i)
		sum += inb(sc->asic_addr + ED_WD_PROM + i);
	if (sum != ED_WD_ROM_CHECKSUM_TOTAL) {

		/*
		 * Checksum is invalid. This often happens with cheap WD8003E
		 * clones.  In this case, the checksum byte (the eighth byte)
		 * seems to always be zero.
		 */
		if (inb(sc->asic_addr + ED_WD_CARD_ID) != ED_TYPE_WD8003E ||
		    inb(sc->asic_addr + ED_WD_PROM + 7) != 0)
			return (0);
	}
	/* reset card to force it into a known state. */
#ifdef TOSH_ETHER
	outb(sc->asic_addr + ED_WD_MSR, ED_WD_MSR_RST | ED_WD_MSR_POW);
#else
	outb(sc->asic_addr + ED_WD_MSR, ED_WD_MSR_RST);
#endif
	DELAY(100);
	outb(sc->asic_addr + ED_WD_MSR, inb(sc->asic_addr + ED_WD_MSR) & ~ED_WD_MSR_RST);
	/* wait in the case this card is reading it's EEROM */
	DELAY(5000);

	sc->vendor = ED_VENDOR_WD_SMC;
	sc->type = inb(sc->asic_addr + ED_WD_CARD_ID);

	/*
	 * Set initial values for width/size.
	 */
	memsize = 8192;
	isa16bit = 0;
	switch (sc->type) {
	case ED_TYPE_WD8003S:
		sc->type_str = "WD8003S";
		break;
	case ED_TYPE_WD8003E:
		sc->type_str = "WD8003E";
		break;
	case ED_TYPE_WD8003EB:
		sc->type_str = "WD8003EB";
		break;
	case ED_TYPE_WD8003W:
		sc->type_str = "WD8003W";
		break;
	case ED_TYPE_WD8013EBT:
		sc->type_str = "WD8013EBT";
		memsize = 16384;
		isa16bit = 1;
		break;
	case ED_TYPE_WD8013W:
		sc->type_str = "WD8013W";
		memsize = 16384;
		isa16bit = 1;
		break;
	case ED_TYPE_WD8013EP:	/* also WD8003EP */
		if (inb(sc->asic_addr + ED_WD_ICR)
		    & ED_WD_ICR_16BIT) {
			isa16bit = 1;
			memsize = 16384;
			sc->type_str = "WD8013EP";
		} else {
			sc->type_str = "WD8003EP";
		}
		break;
	case ED_TYPE_WD8013WC:
		sc->type_str = "WD8013WC";
		memsize = 16384;
		isa16bit = 1;
		break;
	case ED_TYPE_WD8013EBP:
		sc->type_str = "WD8013EBP";
		memsize = 16384;
		isa16bit = 1;
		break;
	case ED_TYPE_WD8013EPC:
		sc->type_str = "WD8013EPC";
		memsize = 16384;
		isa16bit = 1;
		break;
	case ED_TYPE_SMC8216C: /* 8216 has 16K shared mem -- 8416 has 8K */
	case ED_TYPE_SMC8216T:
		if (sc->type == ED_TYPE_SMC8216C) {
			sc->type_str = "SMC8216/SMC8216C";
		} else {
			sc->type_str = "SMC8216T";
		}

		outb(sc->asic_addr + ED_WD790_HWR,
		    inb(sc->asic_addr + ED_WD790_HWR) | ED_WD790_HWR_SWH);
		switch (inb(sc->asic_addr + ED_WD790_RAR) & ED_WD790_RAR_SZ64) {
		case ED_WD790_RAR_SZ64:
			memsize = 65536;
			break;
		case ED_WD790_RAR_SZ32:
			memsize = 32768;
			break;
		case ED_WD790_RAR_SZ16:
			memsize = 16384;
			break;
		case ED_WD790_RAR_SZ8:
			/* 8216 has 16K shared mem -- 8416 has 8K */
			if (sc->type == ED_TYPE_SMC8216C) {
				sc->type_str = "SMC8416C/SMC8416BT";
			} else {
				sc->type_str = "SMC8416T";
			}
			memsize = 8192;
			break;
		}
		outb(sc->asic_addr + ED_WD790_HWR,
		    inb(sc->asic_addr + ED_WD790_HWR) & ~ED_WD790_HWR_SWH);

		isa16bit = 1;
		sc->is790 = 1;
		break;
#ifdef TOSH_ETHER
	case ED_TYPE_TOSHIBA1:
		sc->type_str = "Toshiba1";
		memsize = 32768;
		isa16bit = 1;
		break;
	case ED_TYPE_TOSHIBA4:
		sc->type_str = "Toshiba4";
		memsize = 32768;
		isa16bit = 1;
		break;
#endif
	default:
		sc->type_str = "";
		break;
	}

	/*
	 * Make some adjustments to initial values depending on what is found
	 * in the ICR.
	 */
	if (isa16bit && (sc->type != ED_TYPE_WD8013EBT)
#ifdef TOSH_ETHER
	  && (sc->type != ED_TYPE_TOSHIBA1) && (sc->type != ED_TYPE_TOSHIBA4)
#endif
	    && ((inb(sc->asic_addr + ED_WD_ICR) & ED_WD_ICR_16BIT) == 0)) {
		isa16bit = 0;
		memsize = 8192;
	}

#if ED_DEBUG
	printf("type = %x type_str=%s isa16bit=%d memsize=%d id_msize=%d\n",
	       sc->type, sc->type_str, isa16bit, memsize, isa_dev->id_msize);
	for (i = 0; i < 8; i++)
		printf("%x -> %x\n", i, inb(sc->asic_addr + i));
#endif

	/*
	 * Allow the user to override the autoconfiguration
	 */
	if (isa_dev->id_msize)
		memsize = isa_dev->id_msize;

	maddr = (u_int) isa_dev->id_maddr & 0xffffff;
	if (maddr < 0xa0000 || maddr + memsize > 0x1000000) {
		printf("ed%d: Invalid ISA memory address range configured: 0x%x - 0x%x\n",
		    isa_dev->id_unit, maddr, maddr + memsize);
		return 0;
	}

	/*
	 * (note that if the user specifies both of the following flags that
	 * '8bit' mode intentionally has precedence)
	 */
	if (isa_dev->id_flags & ED_FLAGS_FORCE_16BIT_MODE)
		isa16bit = 1;
	if (isa_dev->id_flags & ED_FLAGS_FORCE_8BIT_MODE)
		isa16bit = 0;

	/*
	 * If possible, get the assigned interrupt number from the card and
	 * use it.
	 */
	if ((sc->type & ED_WD_SOFTCONFIG) && (!sc->is790)) {

		/*
		 * Assemble together the encoded interrupt number.
		 */
		iptr = (inb(isa_dev->id_iobase + ED_WD_ICR) & ED_WD_ICR_IR2) |
		    ((inb(isa_dev->id_iobase + ED_WD_IRR) &
		      (ED_WD_IRR_IR0 | ED_WD_IRR_IR1)) >> 5);

		/*
		 * If no interrupt specified (or "?"), use what the board tells us.
		 */
		if (isa_dev->id_irq <= 0)
			isa_dev->id_irq = ed_intr_mask[iptr];

		/*
		 * Enable the interrupt.
		 */
		outb(isa_dev->id_iobase + ED_WD_IRR,
		     inb(isa_dev->id_iobase + ED_WD_IRR) | ED_WD_IRR_IEN);
	}
	if (sc->is790) {
		outb(isa_dev->id_iobase + ED_WD790_HWR,
		  inb(isa_dev->id_iobase + ED_WD790_HWR) | ED_WD790_HWR_SWH);
		iptr = (((inb(isa_dev->id_iobase + ED_WD790_GCR) & ED_WD790_GCR_IR2) >> 4) |
			(inb(isa_dev->id_iobase + ED_WD790_GCR) &
			 (ED_WD790_GCR_IR1 | ED_WD790_GCR_IR0)) >> 2);
		outb(isa_dev->id_iobase + ED_WD790_HWR,
		 inb(isa_dev->id_iobase + ED_WD790_HWR) & ~ED_WD790_HWR_SWH);

		/*
		 * If no interrupt specified (or "?"), use what the board tells us.
		 */
		if (isa_dev->id_irq <= 0)
			isa_dev->id_irq = ed_790_intr_mask[iptr];

		/*
		 * Enable interrupts.
		 */
		outb(isa_dev->id_iobase + ED_WD790_ICR,
		  inb(isa_dev->id_iobase + ED_WD790_ICR) | ED_WD790_ICR_EIL);
	}
	if (isa_dev->id_irq <= 0) {
		printf("ed%d: %s cards don't support auto-detected/assigned interrupts.\n",
		    isa_dev->id_unit, sc->type_str);
		return (0);
	}
	sc->isa16bit = isa16bit;
	sc->mem_shared = 1;
	isa_dev->id_msize = memsize;
	sc->mem_start = (caddr_t) isa_dev->id_maddr;

	/*
	 * allocate one xmit buffer if < 16k, two buffers otherwise
	 */
	if ((memsize < 16384) ||
            (isa_dev->id_flags & ED_FLAGS_NO_MULTI_BUFFERING)) {
		sc->txb_cnt = 1;
	} else {
		sc->txb_cnt = 2;
	}
	sc->tx_page_start = ED_WD_PAGE_OFFSET;
	sc->rec_page_start = ED_WD_PAGE_OFFSET + ED_TXBUF_SIZE * sc->txb_cnt;
	sc->rec_page_stop = ED_WD_PAGE_OFFSET + memsize / ED_PAGE_SIZE;
	sc->mem_ring = sc->mem_start + (ED_PAGE_SIZE * sc->rec_page_start);
	sc->mem_size = memsize;
	sc->mem_end = sc->mem_start + memsize;

	/*
	 * Get station address from on-board ROM
	 */
	for (i = 0; i < ETHER_ADDR_LEN; ++i)
#if defined(EXOPC)
	        sc->macaddr[i] = inb(sc->asic_addr + ED_WD_PROM + i);
#else
		sc->arpcom.ac_enaddr[i] = inb(sc->asic_addr + ED_WD_PROM + i);
#endif
	/*
	 * Set upper address bits and 8/16 bit access to shared memory.
	 */
	if (isa16bit) {
		if (sc->is790) {
			sc->wd_laar_proto = inb(sc->asic_addr + ED_WD_LAAR);
		} else {
			sc->wd_laar_proto = ED_WD_LAAR_L16EN |
			    ((kvtop(sc->mem_start) >> 19) & ED_WD_LAAR_ADDRHI);
		}
		/*
		 * Enable 16bit access
		 */
		outb(sc->asic_addr + ED_WD_LAAR, sc->wd_laar_proto |
		    ED_WD_LAAR_M16EN);
	} else {
		if (((sc->type & ED_WD_SOFTCONFIG) ||
#ifdef TOSH_ETHER
		    (sc->type == ED_TYPE_TOSHIBA1) || (sc->type == ED_TYPE_TOSHIBA4) ||
#endif
		    (sc->type == ED_TYPE_WD8013EBT)) && (!sc->is790)) {
			sc->wd_laar_proto = (kvtop(sc->mem_start) >> 19) &
			    ED_WD_LAAR_ADDRHI;
			outb(sc->asic_addr + ED_WD_LAAR, sc->wd_laar_proto);
		}
	}

	/*
	 * Set address and enable interface shared memory.
	 */
	if (!sc->is790) {
#ifdef TOSH_ETHER
		outb(sc->asic_addr + ED_WD_MSR + 1, ((kvtop(sc->mem_start) >> 8) & 0xe0) | 4);
		outb(sc->asic_addr + ED_WD_MSR + 2, ((kvtop(sc->mem_start) >> 16) & 0x0f));
		outb(sc->asic_addr + ED_WD_MSR, ED_WD_MSR_MENB | ED_WD_MSR_POW);

#else
		outb(sc->asic_addr + ED_WD_MSR, ((kvtop(sc->mem_start) >> 13) &
		    ED_WD_MSR_ADDR) | ED_WD_MSR_MENB);
#endif
		sc->cr_proto = ED_CR_RD2;
	} else {
		outb(sc->asic_addr + ED_WD_MSR, ED_WD_MSR_MENB);
		outb(sc->asic_addr + ED_WD790_HWR, (inb(sc->asic_addr + ED_WD790_HWR) | ED_WD790_HWR_SWH));
		outb(sc->asic_addr + ED_WD790_RAR, ((kvtop(sc->mem_start) >> 13) & 0x0f) |
		     ((kvtop(sc->mem_start) >> 11) & 0x40) |
		     (inb(sc->asic_addr + ED_WD790_RAR) & 0xb0));
		outb(sc->asic_addr + ED_WD790_HWR, (inb(sc->asic_addr + ED_WD790_HWR) & ~ED_WD790_HWR_SWH));
		sc->cr_proto = 0;
	}

#if 0
	printf("starting memory performance test at 0x%x, size %d...\n",
		sc->mem_start, memsize*16384);
	for (i = 0; i < 16384; i++)
		bzero(sc->mem_start, memsize);
	printf("***DONE***\n");
#endif

	/*
	 * Now zero memory and verify that it is clear
	 */
	bzero(sc->mem_start, memsize);

	for (i = 0; i < memsize; ++i) {
		if (sc->mem_start[i]) {
			printf("ed%d: failed to clear shared memory at %lx - check configuration\n",
			    isa_dev->id_unit, kvtop(sc->mem_start + i));

			/*
			 * Disable 16 bit access to shared memory
			 */
			if (isa16bit) {
				if (sc->is790) {
					outb(sc->asic_addr + ED_WD_MSR, 0x00);
				}
				outb(sc->asic_addr + ED_WD_LAAR, sc->wd_laar_proto &
				    ~ED_WD_LAAR_M16EN);
			}
			return (0);
		}
	}

#if !defined(EXOPC)
	/*
	 * Disable 16bit access to shared memory - we leave it
	 * disabled so that 1) machines reboot properly when the board
	 * is set 16 bit mode and there are conflicting 8bit
	 * devices/ROMS in the same 128k address space as this boards
	 * shared memory. and 2) so that other 8 bit devices with
	 * shared memory can be used in this 128k region, too.
	 */
	if (isa16bit) {
		if (sc->is790) {
			outb(sc->asic_addr + ED_WD_MSR, 0x00);
		}
		outb(sc->asic_addr + ED_WD_LAAR, sc->wd_laar_proto &
		    ~ED_WD_LAAR_M16EN);
	}
#endif

	return (ED_WD_IO_PORTS);
}

/*
 * Probe and vendor-specific initialization routine for 3Com 3c503 boards
 */
static int
ed_probe_3Com(isa_dev)
	struct isa_device *isa_dev;
{
#if defined(EXOPC)
  	struct network *nw = SYSINFO_PTR_AT(si_networks,isa_dev->id_xoknetno);
        struct ed_softc *sc = (struct ed_softc *) nw->cardstruct;
#else
	struct ed_softc *sc = &ed_softc[isa_dev->id_unit];
#endif

	int     i;
	u_int   memsize;
	u_char  isa16bit;

	sc->asic_addr = isa_dev->id_iobase + ED_3COM_ASIC_OFFSET;
	sc->nic_addr = isa_dev->id_iobase + ED_3COM_NIC_OFFSET;

	/*
	 * Verify that the kernel configured I/O address matches the board
	 * configured address
	 */
	switch (inb(sc->asic_addr + ED_3COM_BCFR)) {
	case ED_3COM_BCFR_300:
		if (isa_dev->id_iobase != 0x300)
			return (0);
		break;
	case ED_3COM_BCFR_310:
		if (isa_dev->id_iobase != 0x310)
			return (0);
		break;
	case ED_3COM_BCFR_330:
		if (isa_dev->id_iobase != 0x330)
			return (0);
		break;
	case ED_3COM_BCFR_350:
		if (isa_dev->id_iobase != 0x350)
			return (0);
		break;
	case ED_3COM_BCFR_250:
		if (isa_dev->id_iobase != 0x250)
			return (0);
		break;
	case ED_3COM_BCFR_280:
		if (isa_dev->id_iobase != 0x280)
			return (0);
		break;
	case ED_3COM_BCFR_2A0:
		if (isa_dev->id_iobase != 0x2a0)
			return (0);
		break;
	case ED_3COM_BCFR_2E0:
		if (isa_dev->id_iobase != 0x2e0)
			return (0);
		break;
	default:
		return (0);
	}

	/*
	 * Verify that the kernel shared memory address matches the board
	 * configured address.
	 */
	switch (inb(sc->asic_addr + ED_3COM_PCFR)) {
	case ED_3COM_PCFR_DC000:
		if (kvtop(isa_dev->id_maddr) != 0xdc000)
			return (0);
		break;
	case ED_3COM_PCFR_D8000:
		if (kvtop(isa_dev->id_maddr) != 0xd8000)
			return (0);
		break;
	case ED_3COM_PCFR_CC000:
		if (kvtop(isa_dev->id_maddr) != 0xcc000)
			return (0);
		break;
	case ED_3COM_PCFR_C8000:
		if (kvtop(isa_dev->id_maddr) != 0xc8000)
			return (0);
		break;
	default:
		return (0);
	}


	/*
	 * Reset NIC and ASIC. Enable on-board transceiver throughout reset
	 * sequence because it'll lock up if the cable isn't connected if we
	 * don't.
	 */
	outb(sc->asic_addr + ED_3COM_CR, ED_3COM_CR_RST | ED_3COM_CR_XSEL);

	/*
	 * Wait for a while, then un-reset it
	 */
	DELAY(50);

	/*
	 * The 3Com ASIC defaults to rather strange settings for the CR after
	 * a reset - it's important to set it again after the following outb
	 * (this is done when we map the PROM below).
	 */
	outb(sc->asic_addr + ED_3COM_CR, ED_3COM_CR_XSEL);

	/*
	 * Wait a bit for the NIC to recover from the reset
	 */
	DELAY(5000);

	sc->vendor = ED_VENDOR_3COM;
	sc->type_str = "3c503";
	sc->mem_shared = 1;
	sc->cr_proto = ED_CR_RD2;

	/*
	 * Hmmm...a 16bit 3Com board has 16k of memory, but only an 8k window
	 * to it.
	 */
	memsize = 8192;

	/*
	 * Get station address from on-board ROM
	 */

	/*
	 * First, map ethernet address PROM over the top of where the NIC
	 * registers normally appear.
	 */
	outb(sc->asic_addr + ED_3COM_CR, ED_3COM_CR_EALO | ED_3COM_CR_XSEL);

	for (i = 0; i < ETHER_ADDR_LEN; ++i)
		sc->macaddr[i] = inb(sc->nic_addr + i);

	/*
	 * Unmap PROM - select NIC registers. The proper setting of the
	 * tranceiver is set in ed_init so that the attach code is given a
	 * chance to set the default based on a compile-time config option
	 */
	outb(sc->asic_addr + ED_3COM_CR, ED_3COM_CR_XSEL);

	/*
	 * Determine if this is an 8bit or 16bit board
	 */

	/*
	 * select page 0 registers
	 */
	outb(sc->nic_addr + ED_P0_CR, ED_CR_RD2 | ED_CR_STP);

	/*
	 * Attempt to clear WTS bit. If it doesn't clear, then this is a 16bit
	 * board.
	 */
	outb(sc->nic_addr + ED_P0_DCR, 0);

	/*
	 * select page 2 registers
	 */
	outb(sc->nic_addr + ED_P0_CR, ED_CR_PAGE_2 | ED_CR_RD2 | ED_CR_STP);

	/*
	 * The 3c503 forces the WTS bit to a one if this is a 16bit board
	 */
	if (inb(sc->nic_addr + ED_P2_DCR) & ED_DCR_WTS)
		isa16bit = 1;
	else
		isa16bit = 0;

	/*
	 * select page 0 registers
	 */
	outb(sc->nic_addr + ED_P2_CR, ED_CR_RD2 | ED_CR_STP);

	sc->mem_start = (caddr_t) isa_dev->id_maddr;
	sc->mem_size = memsize;
	sc->mem_end = sc->mem_start + memsize;

	/*
	 * We have an entire 8k window to put the transmit buffers on the
	 * 16bit boards. But since the 16bit 3c503's shared memory is only
	 * fast enough to overlap the loading of one full-size packet, trying
	 * to load more than 2 buffers can actually leave the transmitter idle
	 * during the load. So 2 seems the best value. (Although a mix of
	 * variable-sized packets might change this assumption. Nonetheless,
	 * we optimize for linear transfers of same-size packets.)
	 */
	if (isa16bit) {
		if (isa_dev->id_flags & ED_FLAGS_NO_MULTI_BUFFERING)
			sc->txb_cnt = 1;
		else
			sc->txb_cnt = 2;

		sc->tx_page_start = ED_3COM_TX_PAGE_OFFSET_16BIT;
		sc->rec_page_start = ED_3COM_RX_PAGE_OFFSET_16BIT;
		sc->rec_page_stop = memsize / ED_PAGE_SIZE +
		    ED_3COM_RX_PAGE_OFFSET_16BIT;
		sc->mem_ring = sc->mem_start;
	} else {
		sc->txb_cnt = 1;
		sc->tx_page_start = ED_3COM_TX_PAGE_OFFSET_8BIT;
		sc->rec_page_start = ED_TXBUF_SIZE + ED_3COM_TX_PAGE_OFFSET_8BIT;
		sc->rec_page_stop = memsize / ED_PAGE_SIZE +
		    ED_3COM_TX_PAGE_OFFSET_8BIT;
		sc->mem_ring = sc->mem_start + (ED_PAGE_SIZE * ED_TXBUF_SIZE);
	}

	sc->isa16bit = isa16bit;

	/*
	 * Initialize GA page start/stop registers. Probably only needed if
	 * doing DMA, but what the hell.
	 */
	outb(sc->asic_addr + ED_3COM_PSTR, sc->rec_page_start);
	outb(sc->asic_addr + ED_3COM_PSPR, sc->rec_page_stop);

	/*
	 * Set IRQ. 3c503 only allows a choice of irq 2-5.
	 */
	switch (isa_dev->id_irq) {
	case 2:
		outb(sc->asic_addr + ED_3COM_IDCFR, ED_3COM_IDCFR_IRQ2);
		break;
	case 3:
		outb(sc->asic_addr + ED_3COM_IDCFR, ED_3COM_IDCFR_IRQ3);
		break;
	case 4:
		outb(sc->asic_addr + ED_3COM_IDCFR, ED_3COM_IDCFR_IRQ4);
		break;
	case 5:
		outb(sc->asic_addr + ED_3COM_IDCFR, ED_3COM_IDCFR_IRQ5);
		break;
	default:
		printf("ed%d: Invalid irq configuration (%d) must be 3-5,9 for 3c503\n",
		       isa_dev->id_unit, ffs(isa_dev->id_irq) - 1);
		return (0);
	}

	/*
	 * Initialize GA configuration register. Set bank and enable shared
	 * mem.
	 */
	outb(sc->asic_addr + ED_3COM_GACFR, ED_3COM_GACFR_RSEL |
	     ED_3COM_GACFR_MBS0);

	/*
	 * Initialize "Vector Pointer" registers. These gawd-awful things are
	 * compared to 20 bits of the address on ISA, and if they match, the
	 * shared memory is disabled. We set them to 0xffff0...allegedly the
	 * reset vector.
	 */
	outb(sc->asic_addr + ED_3COM_VPTR2, 0xff);
	outb(sc->asic_addr + ED_3COM_VPTR1, 0xff);
	outb(sc->asic_addr + ED_3COM_VPTR0, 0x00);

	/*
	 * Zero memory and verify that it is clear
	 */
	bzero(sc->mem_start, memsize);

	for (i = 0; i < memsize; ++i)
		if (sc->mem_start[i]) {
			printf("ed%d: failed to clear shared memory at %lx - check configuration\n",
			       isa_dev->id_unit, kvtop(sc->mem_start + i));
			return (0);
		}
	isa_dev->id_msize = memsize;
	return (ED_3COM_IO_PORTS);
}

/*
 * Probe and vendor-specific initialization routine for NE1000/2000 boards
 */
static int
ed_probe_Novell_generic(sc, port, unit, flags)
	struct ed_softc *sc;
	int port;
	int unit;
	int flags;
{
	u_int   memsize, n;
	u_char  romdata[16], tmp;
	static char test_pattern[32] = "THIS is A memory TEST pattern";
	char    test_buffer[32];

	sc->asic_addr = port + ED_NOVELL_ASIC_OFFSET;
	sc->nic_addr = port + ED_NOVELL_NIC_OFFSET;

	/* XXX - do Novell-specific probe here */

	/* Reset the board */
#ifdef GWETHER
	outb(sc->asic_addr + ED_NOVELL_RESET, 0);
	DELAY(200);
#endif	/* GWETHER */
	tmp = inb(sc->asic_addr + ED_NOVELL_RESET);

	/*
	 * I don't know if this is necessary; probably cruft leftover from
	 * Clarkson packet driver code. Doesn't do a thing on the boards I've
	 * tested. -DG [note that a outb(0x84, 0) seems to work here, and is
	 * non-invasive...but some boards don't seem to reset and I don't have
	 * complete documentation on what the 'right' thing to do is...so we
	 * do the invasive thing for now. Yuck.]
	 */
	outb(sc->asic_addr + ED_NOVELL_RESET, tmp);
	DELAY(5000);

	/*
	 * This is needed because some NE clones apparently don't reset the
	 * NIC properly (or the NIC chip doesn't reset fully on power-up) XXX
	 * - this makes the probe invasive! ...Done against my better
	 * judgement. -DLG
	 */
	outb(sc->nic_addr + ED_P0_CR, ED_CR_RD2 | ED_CR_STP);

	DELAY(5000);

	/* Make sure that we really have an 8390 based board */
	if (!ed_probe_generic8390(sc))
		return (0);

	sc->vendor = ED_VENDOR_NOVELL;
	sc->mem_shared = 0;
	sc->cr_proto = ED_CR_RD2;

	/*
	 * Test the ability to read and write to the NIC memory. This has the
	 * side affect of determining if this is an NE1000 or an NE2000.
	 */

	/*
	 * This prevents packets from being stored in the NIC memory when the
	 * readmem routine turns on the start bit in the CR.
	 */
	outb(sc->nic_addr + ED_P0_RCR, ED_RCR_MON);

	/* Temporarily initialize DCR for byte operations */
	outb(sc->nic_addr + ED_P0_DCR, ED_DCR_FT1 | ED_DCR_LS);

	outb(sc->nic_addr + ED_P0_PSTART, 8192 / ED_PAGE_SIZE);
	outb(sc->nic_addr + ED_P0_PSTOP, 16384 / ED_PAGE_SIZE);

	sc->isa16bit = 0;

	/*
	 * Write a test pattern in byte mode. If this fails, then there
	 * probably isn't any memory at 8k - which likely means that the board
	 * is an NE2000.
	 */
	ed_pio_writemem(sc, test_pattern, 8192, sizeof(test_pattern));
	ed_pio_readmem(sc, 8192, test_buffer, sizeof(test_pattern));

	if (bcmp(test_pattern, test_buffer, sizeof(test_pattern))) {
		/* not an NE1000 - try NE2000 */

		outb(sc->nic_addr + ED_P0_DCR, ED_DCR_WTS | ED_DCR_FT1 | ED_DCR_LS);
		outb(sc->nic_addr + ED_P0_PSTART, 16384 / ED_PAGE_SIZE);
		outb(sc->nic_addr + ED_P0_PSTOP, 32768 / ED_PAGE_SIZE);

		sc->isa16bit = 1;

		/*
		 * Write a test pattern in word mode. If this also fails, then
		 * we don't know what this board is.
		 */
		ed_pio_writemem(sc, test_pattern, 16384, sizeof(test_pattern));
		ed_pio_readmem(sc, 16384, test_buffer, sizeof(test_pattern));

		if (bcmp(test_pattern, test_buffer, sizeof(test_pattern)))
			return (0);	/* not an NE2000 either */

		sc->type = ED_TYPE_NE2000;
		sc->type_str = "NE2000";
	} else {
		sc->type = ED_TYPE_NE1000;
		sc->type_str = "NE1000";
	}

	/* 8k of memory plus an additional 8k if 16bit */
	memsize = 8192 + sc->isa16bit * 8192;

#if 0	/* probably not useful - NE boards only come two ways */
	/* allow kernel config file overrides */
	if (isa_dev->id_msize)
		memsize = isa_dev->id_msize;
#endif

	sc->mem_size = memsize;

	/* NIC memory doesn't start at zero on an NE board */
	/* The start address is tied to the bus width */
	sc->mem_start = (char *) 8192 + sc->isa16bit * 8192;
	sc->mem_end = sc->mem_start + memsize;
	sc->tx_page_start = memsize / ED_PAGE_SIZE;

#ifdef GWETHER
	{
		int     x, i, mstart = 0, msize = 0;
		char    pbuf0[ED_PAGE_SIZE], pbuf[ED_PAGE_SIZE], tbuf[ED_PAGE_SIZE];

		for (i = 0; i < ED_PAGE_SIZE; i++)
			pbuf0[i] = 0;

		/* Clear all the memory. */
		for (x = 1; x < 256; x++)
			ed_pio_writemem(sc, pbuf0, x * 256, ED_PAGE_SIZE);

		/* Search for the start of RAM. */
		for (x = 1; x < 256; x++) {
			ed_pio_readmem(sc, x * 256, tbuf, ED_PAGE_SIZE);
			if (bcmp(pbuf0, tbuf, ED_PAGE_SIZE) == 0) {
				for (i = 0; i < ED_PAGE_SIZE; i++)
					pbuf[i] = 255 - x;
				ed_pio_writemem(sc, pbuf, x * 256, ED_PAGE_SIZE);
				ed_pio_readmem(sc, x * 256, tbuf, ED_PAGE_SIZE);
				if (bcmp(pbuf, tbuf, ED_PAGE_SIZE) == 0) {
					mstart = x * ED_PAGE_SIZE;
					msize = ED_PAGE_SIZE;
					break;
				}
			}
		}

		if (mstart == 0) {
			printf("ed%d: Cannot find start of RAM.\n", unit);
			return 0;
		}
		/* Search for the start of RAM. */
		for (x = (mstart / ED_PAGE_SIZE) + 1; x < 256; x++) {
			ed_pio_readmem(sc, x * 256, tbuf, ED_PAGE_SIZE);
			if (bcmp(pbuf0, tbuf, ED_PAGE_SIZE) == 0) {
				for (i = 0; i < ED_PAGE_SIZE; i++)
					pbuf[i] = 255 - x;
				ed_pio_writemem(sc, pbuf, x * 256, ED_PAGE_SIZE);
				ed_pio_readmem(sc, x * 256, tbuf, ED_PAGE_SIZE);
				if (bcmp(pbuf, tbuf, ED_PAGE_SIZE) == 0)
					msize += ED_PAGE_SIZE;
				else {
					break;
				}
			} else {
				break;
			}
		}

		if (msize == 0) {
			printf("ed%d: Cannot find any RAM, start : %d, x = %d.\n", unit, mstart, x);
			return 0;
		}
		printf("ed%d: RAM start at %d, size : %d.\n", unit, mstart, msize);

		sc->mem_size = msize;
		sc->mem_start = (char *) mstart;
		sc->mem_end = (char *) (msize + mstart);
		sc->tx_page_start = mstart / ED_PAGE_SIZE;
	}
#endif	/* GWETHER */

	/*
	 * Use one xmit buffer if < 16k, two buffers otherwise (if not told
	 * otherwise).
	 */
	if ((memsize < 16384) || (flags & ED_FLAGS_NO_MULTI_BUFFERING))
		sc->txb_cnt = 1;
	else
		sc->txb_cnt = 2;

	sc->rec_page_start = sc->tx_page_start + sc->txb_cnt * ED_TXBUF_SIZE;
	sc->rec_page_stop = sc->tx_page_start + memsize / ED_PAGE_SIZE;

	sc->mem_ring = sc->mem_start + sc->txb_cnt * ED_PAGE_SIZE * ED_TXBUF_SIZE;

	ed_pio_readmem(sc, 0, romdata, 16);
	for (n = 0; n < ETHER_ADDR_LEN; n++)
		sc->macaddr[n] = romdata[n * (sc->isa16bit + 1)];

#ifdef GWETHER
	if (sc->arpcom.ac_enaddr[2] == 0x86) {
		sc->type_str = "Gateway AT";
	}
#endif	/* GWETHER */

	/* clear any pending interrupts that might have occurred above */
	outb(sc->nic_addr + ED_P0_ISR, 0xff);

	return (ED_NOVELL_IO_PORTS);
}

static int
ed_probe_Novell(isa_dev)
	struct isa_device *isa_dev;
{
#if defined(EXOPC)
  	struct network *nw = SYSINFO_PTR_AT(si_networks,isa_dev->id_xoknetno);
        struct ed_softc *sc = (struct ed_softc *) nw->cardstruct;
#else
	struct ed_softc *sc = &ed_softc[isa_dev->id_unit];
#endif

	isa_dev->id_maddr = 0;
	return ed_probe_Novell_generic(sc, isa_dev->id_iobase, 
				       isa_dev->id_unit, isa_dev->id_flags);
}

#if NCARD > 0
/*
 * Probe framework for pccards.  Replicates the standard framework, 
 * minus the pccard driver registration and ignores the ether address
 * supplied (from the CIS), relying on the probe to find it instead.
 */
static int
ed_probe_pccard(isa_dev, ether)
	struct isa_device *isa_dev;
	u_char *ether;
{
	int     nports;

	nports = ed_probe_WD80x3(isa_dev);
	if (nports)
		return (nports);

	nports = ed_probe_Novell(isa_dev);
	if (nports)
		return (nports);

	return (0);
}

#endif /* NCARD > 0 */

#define	ED_HPP_TEST_SIZE	16

/*
 * Probe and vendor specific initialization for the HP PC Lan+ Cards.
 * (HP Part nos: 27247B and 27252A).
 *
 * The card has an asic wrapper around a DS8390 core.  The asic handles 
 * host accesses and offers both standard register IO and memory mapped 
 * IO.  Memory mapped I/O allows better performance at the expense of greater
 * chance of an incompatibility with existing ISA cards.
 *
 * The card has a few caveats: it isn't tolerant of byte wide accesses, only
 * short (16 bit) or word (32 bit) accesses are allowed.  Some card revisions
 * don't allow 32 bit accesses; these are indicated by a bit in the software
 * ID register (see if_edreg.h).
 * 
 * Other caveats are: we should read the MAC address only when the card
 * is inactive.
 *
 * For more information; please consult the CRYNWR packet driver.
 *
 * The AUI port is turned on using the "link2" option on the ifconfig 
 * command line.
 */
static int
ed_probe_HP_pclanp(isa_dev)
	struct isa_device *isa_dev;
{


#if defined(EXOPC)
  	struct network *nw = SYSINFO_PTR_AT(si_networks,isa_dev->id_xoknetno);
        struct ed_softc *sc = (struct ed_softc *) nw->cardstruct;
#else
	struct ed_softc *sc = &ed_softc[isa_dev->id_unit];
#endif

	int n;				/* temp var */
	int memsize;			/* mem on board */
	u_char checksum;		/* checksum of board address */
	u_char irq;			/* board configured IRQ */
	char test_pattern[ED_HPP_TEST_SIZE];	/* read/write areas for */
	char test_buffer[ED_HPP_TEST_SIZE];	/* probing card */


	/* Fill in basic information */
	sc->asic_addr = isa_dev->id_iobase + ED_HPP_ASIC_OFFSET;
	sc->nic_addr = isa_dev->id_iobase + ED_HPP_NIC_OFFSET;
	sc->is790 = 0;
	sc->isa16bit = 0;	/* the 8390 core needs to be in byte mode */

	/* 
	 * Look for the HP PCLAN+ signature: "0x50,0x48,0x00,0x53" 
	 */
	
	if ((inb(sc->asic_addr + ED_HPP_ID) != 0x50) || 
	    (inb(sc->asic_addr + ED_HPP_ID + 1) != 0x48) ||
	    ((inb(sc->asic_addr + ED_HPP_ID + 2) & 0xF0) != 0) ||
	    (inb(sc->asic_addr + ED_HPP_ID + 3) != 0x53))
		return 0;

	/* 
	 * Read the MAC address and verify checksum on the address.
	 */

	outw(sc->asic_addr + ED_HPP_PAGING, ED_HPP_PAGE_MAC);
	for (n  = 0, checksum = 0; n < ETHER_ADDR_LEN; n++)
		checksum += (sc->macaddr[n] = 
			inb(sc->asic_addr + ED_HPP_MAC_ADDR + n));
	
	checksum += inb(sc->asic_addr + ED_HPP_MAC_ADDR + ETHER_ADDR_LEN);

	if (checksum != 0xFF)
		return 0;

	/*
	 * Verify that the software model number is 0.
	 */
	
	outw(sc->asic_addr + ED_HPP_PAGING, ED_HPP_PAGE_ID);
	if (((sc->hpp_id = inw(sc->asic_addr + ED_HPP_PAGE_4)) & 
		ED_HPP_ID_SOFT_MODEL_MASK) != 0x0000)
		return 0;

	/*
	 * Read in and save the current options configured on card.
	 */

	sc->hpp_options = inw(sc->asic_addr + ED_HPP_OPTION);

	sc->hpp_options |= (ED_HPP_OPTION_NIC_RESET | 
                        	ED_HPP_OPTION_CHIP_RESET |
				ED_HPP_OPTION_ENABLE_IRQ);

	/* 
	 * Reset the chip.  This requires writing to the option register
	 * so take care to preserve the other bits.
	 */

	outw(sc->asic_addr + ED_HPP_OPTION, 
		(sc->hpp_options & ~(ED_HPP_OPTION_NIC_RESET | 
			ED_HPP_OPTION_CHIP_RESET)));

	DELAY(5000);	/* wait for chip reset to complete */

	outw(sc->asic_addr + ED_HPP_OPTION,
		(sc->hpp_options | (ED_HPP_OPTION_NIC_RESET |
			ED_HPP_OPTION_CHIP_RESET |
			ED_HPP_OPTION_ENABLE_IRQ)));

	DELAY(5000);

	if (!(inb(sc->nic_addr + ED_P0_ISR) & ED_ISR_RST))
		return 0;	/* reset did not complete */

	/*
	 * Read out configuration information.
	 */

	outw(sc->asic_addr + ED_HPP_PAGING, ED_HPP_PAGE_HW);

	irq = inb(sc->asic_addr + ED_HPP_HW_IRQ);

	/*
 	 * Check for impossible IRQ.
	 */

	if (irq >= (sizeof(ed_hpp_intr_mask) / sizeof(ed_hpp_intr_mask[0])))
		return 0;

	/* 
	 * If the kernel IRQ was specified with a '?' use the cards idea
	 * of the IRQ.  If the kernel IRQ was explicitly specified, it
 	 * should match that of the hardware.
	 */

	if (isa_dev->id_irq <= 0)
		isa_dev->id_irq = ed_hpp_intr_mask[irq];
	else if (isa_dev->id_irq != ed_hpp_intr_mask[irq])
		return 0;

	/*
	 * Fill in softconfig info.
	 */

	sc->vendor = ED_VENDOR_HP;
	sc->type = ED_TYPE_HP_PCLANPLUS;
	sc->type_str = "HP-PCLAN+";

	sc->mem_shared = 0;	/* we DON'T have dual ported RAM */
	sc->mem_start = 0;	/* we use offsets inside the card RAM */

	sc->hpp_mem_start = NULL;/* no memory mapped I/O by default */

	/*
	 * Check if memory mapping of the I/O registers possible.
	 */

	if (sc->hpp_options & ED_HPP_OPTION_MEM_ENABLE)
	{
		u_long mem_addr;

		/*
		 * determine the memory address from the board.
		 */
		
		outw(sc->asic_addr + ED_HPP_PAGING, ED_HPP_PAGE_HW);
		mem_addr = (inw(sc->asic_addr + ED_HPP_HW_MEM_MAP) << 8);

		/*
		 * Check that the kernel specified start of memory and
		 * hardware's idea of it match.
		 */
		
		if (mem_addr != kvtop(isa_dev->id_maddr))
			return 0;

		sc->hpp_mem_start = isa_dev->id_maddr;
	}

	/*
	 * The board has 32KB of memory.  Is there a way to determine
	 * this programmatically?
	 */
	
	memsize = 32768;

	/*
	 * Fill in the rest of the soft config structure.
	 */

	/*
	 * The transmit page index.
	 */

	sc->tx_page_start = ED_HPP_TX_PAGE_OFFSET;

	if (isa_dev->id_flags & ED_FLAGS_NO_MULTI_BUFFERING)
		sc->txb_cnt = 1;
	else
		sc->txb_cnt = 2;

	/*
	 * Memory description
	 */

	sc->mem_size = memsize;
	sc->mem_ring = sc->mem_start + 
		(sc->txb_cnt * ED_PAGE_SIZE * ED_TXBUF_SIZE);
	sc->mem_end = sc->mem_start + sc->mem_size;

	/*
	 * Receive area starts after the transmit area and 
	 * continues till the end of memory.
	 */

	sc->rec_page_start = sc->tx_page_start + 
				(sc->txb_cnt * ED_TXBUF_SIZE);
	sc->rec_page_stop = (sc->mem_size / ED_PAGE_SIZE);


	sc->cr_proto = 0;	/* value works */

	/*
	 * Set the wrap registers for string I/O reads.
	 */

	outw(sc->asic_addr + ED_HPP_PAGING, ED_HPP_PAGE_HW);
	outw(sc->asic_addr + ED_HPP_HW_WRAP,
		((sc->rec_page_start / ED_PAGE_SIZE) |
		 (((sc->rec_page_stop / ED_PAGE_SIZE) - 1) << 8)));

	/*
	 * Reset the register page to normal operation.
	 */

	outw(sc->asic_addr + ED_HPP_PAGING, ED_HPP_PAGE_PERF);

	/*
	 * Verify that we can read/write from adapter memory.
	 * Create test pattern.
	 */

	for (n = 0; n < ED_HPP_TEST_SIZE; n++)
	{
		test_pattern[n] = (n*n) ^ ~n;
	}

#undef	ED_HPP_TEST_SIZE

	/*
	 * Check that the memory is accessible thru the I/O ports.
	 * Write out the contents of "test_pattern", read back
	 * into "test_buffer" and compare the two for any
	 * mismatch.
	 */

	for (n = 0; n < (32768 / ED_PAGE_SIZE); n ++) {

		ed_pio_writemem(sc, test_pattern, (n * ED_PAGE_SIZE), 
				sizeof(test_pattern));
		ed_pio_readmem(sc, (n * ED_PAGE_SIZE), 
			test_buffer, sizeof(test_pattern));

		if (bcmp(test_pattern, test_buffer, 
			sizeof(test_pattern)))
			return 0;
	}

	return (ED_HPP_IO_PORTS);

}

/*
 * HP PC Lan+ : Set the physical link to use AUI or TP/TL.
 */

void
ed_hpp_set_physical_link(struct ed_softc *sc)
{
#if !defined(EXOPC)
	struct ifnet *ifp = &sc->arpcom.ac_if;
#endif
	int lan_page;

	outw(sc->asic_addr + ED_HPP_PAGING, ED_HPP_PAGE_LAN);
	lan_page = inw(sc->asic_addr + ED_HPP_PAGE_0);

#if defined(EXOPC)
	if (sc->flags & IFF_ALTPHYS) {
#else
	if (ifp->if_flags & IFF_ALTPHYS) {
#endif

		/*
		 * Use the AUI port.
		 */

		lan_page |= ED_HPP_LAN_AUI;

		outw(sc->asic_addr + ED_HPP_PAGING, ED_HPP_PAGE_LAN);
		outw(sc->asic_addr + ED_HPP_PAGE_0, lan_page);


	} else {

		/*
		 * Use the ThinLan interface
		 */

		lan_page &= ~ED_HPP_LAN_AUI;

		outw(sc->asic_addr + ED_HPP_PAGING, ED_HPP_PAGE_LAN);
		outw(sc->asic_addr + ED_HPP_PAGE_0, lan_page);

	}

	/*
	 * Wait for the lan card to re-initialize itself
	 */

	DELAY(150000);	/* wait 150 ms */

	/*
	 * Restore normal pages.
	 */

	outw(sc->asic_addr + ED_HPP_PAGING, ED_HPP_PAGE_PERF);

}


/*
 * Install interface into kernel networking data structures
 */
static int
ed_attach(sc, unit, flags)
	struct ed_softc *sc;
	int unit;
	int flags;
{

#if !defined (EXOPC)
	struct ifnet *ifp = &sc->arpcom.ac_if;
#endif

	/*
	 * Set interface to stopped condition (reset)
	 */
	ed_stop(sc);

#if !defined(EXOPC)

	if (!ifp->if_name) {
		/*
		 * Initialize ifnet structure
		 */
		ifp->if_softc = sc;
		ifp->if_unit = unit;
		ifp->if_name = "ed";
		ifp->if_output = ether_output;
		ifp->if_start = ed_start;
		ifp->if_ioctl = ed_ioctl;
		ifp->if_watchdog = ed_watchdog;
		ifp->if_init = ed_init;
		ifp->if_snd.ifq_maxlen = IFQ_MAXLEN;
		ifp->if_linkmib = &sc->mibdata;
		ifp->if_linkmiblen = sizeof sc->mibdata;
		/*
		 * XXX - should do a better job.
		 */
		if (sc->is790)
			sc->mibdata.dot3StatsEtherChipSet =
				DOT3CHIPSET(dot3VendorWesternDigital,
					    dot3ChipSetWesternDigital83C790);
		else
			sc->mibdata.dot3StatsEtherChipSet =
				DOT3CHIPSET(dot3VendorNational, 
					    dot3ChipSetNational8390);
		sc->mibdata.dot3Compliance = DOT3COMPLIANCE_COLLS;

		/*
		 * Set default state for ALTPHYS flag (used to disable the 
		 * tranceiver for AUI operation), based on compile-time 
		 * config option.
		 */
		if (flags & ED_FLAGS_DISABLE_TRANCEIVER)
			ifp->if_flags = (IFF_BROADCAST | IFF_SIMPLEX | 
			    IFF_MULTICAST | IFF_ALTPHYS);
		else
			ifp->if_flags = (IFF_BROADCAST | IFF_SIMPLEX |
			    IFF_MULTICAST);

		/*
		 * Attach the interface
		 */
		if_attach(ifp);
		ether_ifattach(ifp);
	}
	/* device attach does transition from UNCONFIGURED to IDLE state */
#endif

	/*
	 * Print additional info when attached
	 */
#if defined(EXOPC)
	printf("ed%d: ", unit);
#else
	printf("%s%d: address %6D, ", ifp->if_name, ifp->if_unit, sc->arpcom.ac_enaddr, ":");
#endif 

	if (sc->type_str && (*sc->type_str != 0))
		printf("type %s using %s onboard mem %ld bytes ", sc->type_str,
		       sc->mem_shared ? "shmem" : "pio", sc->mem_size);
	else
		printf("type unknown (0x%x) ", sc->type);

	if (sc->vendor == ED_VENDOR_HP)
		printf("(%s %s IO)", (sc->hpp_id & ED_HPP_ID_16_BIT_ACCESS) ?
			"16-bit" : "32-bit",
			sc->hpp_mem_start ? "memory mapped" : "regular");
	else
		printf("%s ", sc->isa16bit ? "(16 bit)" : "(8 bit)");

	printf("%s\n", (((sc->vendor == ED_VENDOR_3COM) ||
			 (sc->vendor == ED_VENDOR_HP)) &&
#if defined(EXOPC)
		(sc->flags & IFF_ALTPHYS)) ? " tranceiver disabled" : "");
#else
		(ifp->if_flags & IFF_ALTPHYS)) ? " tranceiver disabled" : "");
#endif

#if !defined(EXOPC)
	/*
	 * If BPF is in the kernel, call the attach for it
	 */
#if NBPFILTER > 0
	bpfattach(ifp, DLT_EN10MB, sizeof(struct ether_header));
#endif
#endif

	return 1;
}

static int
ed_attach_isa(isa_dev)
	struct isa_device *isa_dev;
{
	int unit = isa_dev->id_unit;
#if defined(EXOPC)
  	struct network *nw = SYSINFO_PTR_AT(si_networks,isa_dev->id_xoknetno);
        struct ed_softc *sc = (struct ed_softc *) nw->cardstruct;
#else
	struct ed_softc *sc = &ed_softc[unit];
#endif
	int flags = isa_dev->id_flags;

	return ed_attach(sc, unit, flags);
}

#if NPCI > 0
void *
ed_attach_NE2000_pci(unit, port)
	int unit;
	int port;
{
	struct ed_softc *sc = malloc(sizeof *sc, M_DEVBUF, M_NOWAIT);
	int isa_flags = 0;

	if (!sc)
		return sc;

	bzero(sc, sizeof *sc);
	if (ed_probe_Novell_generic(sc, port, unit, isa_flags) == 0
	    || ed_attach(sc, unit, isa_flags) == 0) {
		free(sc, M_DEVBUF);
		return NULL;
	}
	return sc;
}
#endif

/*
 * Reset interface.
 */
static void

#if defined(EXOPC)

ed_reset (struct ed_softc *sc) {

#else

ed_reset(ifp)
	struct ifnet *ifp;
{
	struct ed_softc *sc = ifp->if_softc;
#endif

	int     s;

	if (sc->gone)
		return;

	s = splimp();

	/*
	 * Stop interface and re-initialize.
	 */
	ed_stop(sc);
	ed_init(sc);

	(void) splx(s);
}

/*
 * Take interface offline.
 */
static void
ed_stop(sc)
	struct ed_softc *sc;
{
	int     n = 5000;

	if (sc->gone)
		return;
	/*
	 * Stop everything on the interface, and select page 0 registers.
	 */
	outb(sc->nic_addr + ED_P0_CR, sc->cr_proto | ED_CR_STP);

	/*
	 * Wait for interface to enter stopped state, but limit # of checks to
	 * 'n' (about 5ms). It shouldn't even take 5us on modern DS8390's, but
	 * just in case it's an old one.
	 */
	while (((inb(sc->nic_addr + ED_P0_ISR) & ED_ISR_RST) == 0) && --n);
}

/*
 * Device timeout/watchdog routine. Entered if the device neglects to
 *	generate an interrupt after a transmit has been started on it.
 */

#if !defined(EXOPC)
static void 
ed_watchdog(ifp)
	struct ifnet *ifp;
{
	struct ed_softc *sc = ifp->if_softc;

	if (sc->gone)
		return;
	log(LOG_ERR, "ed%d: device timeout\n", ifp->if_unit);
	ifp->if_oerrors++;

	ed_reset(ifp);
}
#endif

/*
 * Initialize device.
 */
static void
ed_init(xsc)
        void *xsc;
{
        struct ed_softc *sc = xsc;
#if !defined(EXOPC)
	struct ifnet *ifp = &sc->arpcom.ac_if;
#endif
	int     i, s;

	if (sc->gone)
		return;

#if !defined(EXOPC)
	/* address not known */
	if (TAILQ_EMPTY(&ifp->if_addrhead)) /* unlikely? XXX */
		return;
#endif

	/*
	 * Initialize the NIC in the exact order outlined in the NS manual.
	 * This init procedure is "mandatory"...don't change what or when
	 * things happen.
	 */
	s = splimp();

	/* reset transmitter flags */
	sc->xmit_busy = 0;
#if !defined(EXOPC)
	ifp->if_timer = 0;
#endif

	sc->txb_inuse = 0;
	sc->txb_new = 0;
	sc->txb_next_tx = 0;

	/* This variable is used below - don't move this assignment */
	sc->next_packet = sc->rec_page_start + 1;

	/*
	 * Set interface for page 0, Remote DMA complete, Stopped
	 */
	outb(sc->nic_addr + ED_P0_CR, sc->cr_proto | ED_CR_STP);

	if (sc->isa16bit) {

		/*
		 * Set FIFO threshold to 8, No auto-init Remote DMA, byte
		 * order=80x86, word-wide DMA xfers,
		 */
		outb(sc->nic_addr + ED_P0_DCR, ED_DCR_FT1 | ED_DCR_WTS | ED_DCR_LS);
	} else {

		/*
		 * Same as above, but byte-wide DMA xfers
		 */
		outb(sc->nic_addr + ED_P0_DCR, ED_DCR_FT1 | ED_DCR_LS);
	}

	/*
	 * Clear Remote Byte Count Registers
	 */
	outb(sc->nic_addr + ED_P0_RBCR0, 0);
	outb(sc->nic_addr + ED_P0_RBCR1, 0);

	/*
	 * For the moment, don't store incoming packets in memory.
	 */
	outb(sc->nic_addr + ED_P0_RCR, ED_RCR_MON);

	/*
	 * Place NIC in internal loopback mode
	 */
	outb(sc->nic_addr + ED_P0_TCR, ED_TCR_LB0);

	/*
	 * Initialize transmit/receive (ring-buffer) Page Start
	 */
	outb(sc->nic_addr + ED_P0_TPSR, sc->tx_page_start);
	outb(sc->nic_addr + ED_P0_PSTART, sc->rec_page_start);
	/* Set lower bits of byte addressable framing to 0 */
	if (sc->is790)
		outb(sc->nic_addr + 0x09, 0);

	/*
	 * Initialize Receiver (ring-buffer) Page Stop and Boundry
	 */
	outb(sc->nic_addr + ED_P0_PSTOP, sc->rec_page_stop);
	outb(sc->nic_addr + ED_P0_BNRY, sc->rec_page_start);

	/*
	 * Clear all interrupts. A '1' in each bit position clears the
	 * corresponding flag.
	 */
	outb(sc->nic_addr + ED_P0_ISR, 0xff);

	/*
	 * Enable the following interrupts: receive/transmit complete,
	 * receive/transmit error, and Receiver OverWrite.
	 *
	 * Counter overflow and Remote DMA complete are *not* enabled.
	 */
	outb(sc->nic_addr + ED_P0_IMR,
	ED_IMR_PRXE | ED_IMR_PTXE | ED_IMR_RXEE | ED_IMR_TXEE | ED_IMR_OVWE);

	/*
	 * Program Command Register for page 1
	 */
	outb(sc->nic_addr + ED_P0_CR, sc->cr_proto | ED_CR_PAGE_1 | ED_CR_STP);

	/*
	 * Copy out our station address
	 */
	for (i = 0; i < ETHER_ADDR_LEN; ++i)
		outb(sc->nic_addr + ED_P1_PAR0 + i, sc->macaddr[i]);

	/*
	 * Set Current Page pointer to next_packet (initialized above)
	 */
	outb(sc->nic_addr + ED_P1_CURR, sc->next_packet);

	/*
	 * Program Receiver Configuration Register and multicast filter. CR is
	 * set to page 0 on return.
	 */
	ed_setrcr(sc);

	/*
	 * Take interface out of loopback
	 */
	outb(sc->nic_addr + ED_P0_TCR, 0);

	/*
	 * If this is a 3Com board, the tranceiver must be software enabled
	 * (there is no settable hardware default).
	 */
	if (sc->vendor == ED_VENDOR_3COM) {
#if defined(EXOPC)
	        if (sc->flags & IFF_ALTPHYS) {
#else
		if (ifp->if_flags & IFF_ALTPHYS) {
#endif
			outb(sc->asic_addr + ED_3COM_CR, 0);
		} else {
			outb(sc->asic_addr + ED_3COM_CR, ED_3COM_CR_XSEL);
		}
	}

	/*
	 * Set 'running' flag, and clear output active flag.
	 */
#if defined(EXOPC)
	sc->flags |= IFF_RUNNING;
	sc->flags &= ~IFF_OACTIVE;

	/*
	 * ...and attempt to start output
	 */
	ed_start (sc);
#else
	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;


	/*
	 * ...and attempt to start output
	 */
	ed_start(ifp);
#endif

	(void) splx(s);
}

/*
 * This routine actually starts the transmission on the interface
 */

static inline void
ed_xmit(sc)
	struct ed_softc *sc;
{
#if !defined(EXOPC)
	struct ifnet *ifp = (struct ifnet *)sc;
#endif
	unsigned short len;

	assert (sc->txb_inuse);

	if (sc->gone)
		return;

	len = sc->txb_len[sc->txb_next_tx];

	/*
	 * Set NIC for page 0 register access
	 */
	outb(sc->nic_addr + ED_P0_CR, sc->cr_proto | ED_CR_STA);

	/*
	 * Set TX buffer start page
	 */
	outb(sc->nic_addr + ED_P0_TPSR, sc->tx_page_start +
	     sc->txb_next_tx * ED_TXBUF_SIZE);

	/*
	 * Set TX length
	 */
	outb(sc->nic_addr + ED_P0_TBCR0, len);
	outb(sc->nic_addr + ED_P0_TBCR1, len >> 8);

	/*
	 * Set page 0, Remote DMA complete, Transmit Packet, and *Start*
	 */
	outb(sc->nic_addr + ED_P0_CR, sc->cr_proto | ED_CR_TXP | ED_CR_STA);
	sc->xmit_busy = 1;

	/*
	 * Point to next transmit buffer slot and wrap if necessary.
	 */
	sc->txb_next_tx++;
	if (sc->txb_next_tx == sc->txb_cnt)
		sc->txb_next_tx = 0;

#if !defined(EXOPC)
	/*
	 * Set a timer just in case we never hear from the board again
	 */
	ifp->if_timer = 2;
#endif

}

/*
 * Start output on interface.
 * We make two assumptions here:
 *  1) that the current priority is set to splimp _before_ this code
 *     is called *and* is returned to the appropriate priority after
 *     return
 *  2) that the IFF_OACTIVE flag is checked before this code is called
 *     (i.e. that the output part of the interface is idle)
 */


#if defined(EXOPC)

static void 
ed_start(struct ed_softc *sc) {

  /* 
   * Start output if there are any tx-buffers full 
   */
  if (!sc->xmit_busy && sc->txb_inuse > 0) {
    ed_xmit (sc);
  }

  /*
   * See if there is room to put another packet in the buffer.
   */
  if (sc->txb_inuse >= sc->txb_cnt) {

    /*
     * No room. Indicate this to the outside world and exit.
     */
    gdt[GD_ED0>>3] = mmu_seg_fault;
    sc->flags |= IFF_OACTIVE;
  } else {

    /*
     * Allow the user to write to the transmit buffers.
     */
    gdt[GD_ED0>>3] = smallseg (STA_W, (ASHDVM + kva2pa (sc->mem_start)
				       + ((sc->txb_new * ED_TXBUF_SIZE)
					  * ED_PAGE_SIZE)),
			       (ED_TXBUF_SIZE * ED_PAGE_SIZE) - 1, 3);
    sc->flags &= ~IFF_OACTIVE;
  }
}

#else 

static void
ed_start(ifp)
	struct ifnet *ifp;
{
	struct ed_softc *sc = ifp->if_softc;
	struct mbuf *m0, *m;
	caddr_t buffer;
	int     len;


	if (sc->gone) {
		printf("ed_start(%p) GONE\n",ifp);
		return;
	}

outloop:

	/*
	 * First, see if there are buffered packets and an idle transmitter -
	 * should never happen at this point.
	 */
	if (sc->txb_inuse && (sc->xmit_busy == 0)) {
		printf("ed: packets buffered, but transmitter idle\n");
		ed_xmit(sc);
	}

	/*
	 * See if there is room to put another packet in the buffer.
	 */
	if (sc->txb_inuse == sc->txb_cnt) {

		/*
		 * No room. Indicate this to the outside world and exit.
		 */


		ifp->if_flags |= IFF_OACTIVE;
		return;
	}

	IF_DEQUEUE(&ifp->if_snd, m);
	if (m == 0) {

		/*
		 * We are using the !OACTIVE flag to indicate to the outside
		 * world that we can accept an additional packet rather than
		 * that the transmitter is _actually_ active. Indeed, the
		 * transmitter may be active, but if we haven't filled all the
		 * buffers with data then we still want to accept more.
		 */
		ifp->if_flags &= ~IFF_OACTIVE;
		return;
	}

	/*
	 * Copy the mbuf chain into the transmit buffer
	 */

	m0 = m;

	/* txb_new points to next open buffer slot */
	buffer = sc->mem_start + (sc->txb_new * ED_TXBUF_SIZE * ED_PAGE_SIZE);

	if (sc->mem_shared) {

		/*
		 * Special case setup for 16 bit boards...
		 */
		if (sc->isa16bit) {
			switch (sc->vendor) {

				/*
				 * For 16bit 3Com boards (which have 16k of
				 * memory), we have the xmit buffers in a
				 * different page of memory ('page 0') - so
				 * change pages.
				 */
			case ED_VENDOR_3COM:
				outb(sc->asic_addr + ED_3COM_GACFR,
				     ED_3COM_GACFR_RSEL);
				break;

				/*
				 * Enable 16bit access to shared memory on
				 * WD/SMC boards.
				 */
			case ED_VENDOR_WD_SMC:{
					outb(sc->asic_addr + ED_WD_LAAR,
					     sc->wd_laar_proto | ED_WD_LAAR_M16EN);
					if (sc->is790) {
						outb(sc->asic_addr + ED_WD_MSR, ED_WD_MSR_MENB);
					}
					break;
				}
			}
		}

		for (len = 0; m != 0; m = m->m_next) {
			bcopy(mtod(m, caddr_t), buffer, m->m_len);
			buffer += m->m_len;
			len += m->m_len;
		}

		/*
		 * Restore previous shared memory access
		 */
		if (sc->isa16bit) {
			switch (sc->vendor) {
			case ED_VENDOR_3COM:
				outb(sc->asic_addr + ED_3COM_GACFR,
				     ED_3COM_GACFR_RSEL | ED_3COM_GACFR_MBS0);
				break;
			case ED_VENDOR_WD_SMC:{
					if (sc->is790) {
						outb(sc->asic_addr + ED_WD_MSR, 0x00);
					}
					outb(sc->asic_addr + ED_WD_LAAR,
					    sc->wd_laar_proto & ~ED_WD_LAAR_M16EN);
					break;
				}
			}
		}
	} else {
		len = ed_pio_write_mbufs(sc, m, (int)buffer);
		if (len == 0)
			goto outloop;
	}
	sc->txb_len[sc->txb_new] = max(len, (ETHER_MIN_LEN-ETHER_CRC_LEN));

	sc->txb_inuse++;

	/*
	 * Point to next buffer slot and wrap if necessary.
	 */
	sc->txb_new++;
	if (sc->txb_new == sc->txb_cnt)
		sc->txb_new = 0;

	if (sc->xmit_busy == 0) {
	        ed_xmit(sc);
	}

	/*
	 * Tap off here if there is a bpf listener.
	 */
#if NBPFILTER > 0
	if (ifp->if_bpf) {
		bpf_mtap(ifp, m0);
	}
#endif

	m_freem(m0);

	/*
	 * Loop back to the top to possibly buffer more packets
	 */
	goto outloop;
}

#endif /* defined(EXOPC) */

/*
 * Ethernet interface receiver interrupt.
 */
static inline void
ed_rint(sc)
	struct ed_softc *sc;
{
#if !defined(EXOPC)
	struct ifnet *ifp = &sc->arpcom.ac_if;
#endif
	u_char  boundry;
	u_short len;
	struct ed_ring packet_hdr;
	char   *packet_ptr;

	if (sc->gone)
		return;

	/*
	 * Set NIC to page 1 registers to get 'current' pointer
	 */
	outb(sc->nic_addr + ED_P0_CR, sc->cr_proto | ED_CR_PAGE_1 | ED_CR_STA);

	/*
	 * 'sc->next_packet' is the logical beginning of the ring-buffer -
	 * i.e. it points to where new data has been buffered. The 'CURR'
	 * (current) register points to the logical end of the ring-buffer -
	 * i.e. it points to where additional new data will be added. We loop
	 * here until the logical beginning equals the logical end (or in
	 * other words, until the ring-buffer is empty).
	 */
	while (sc->next_packet != inb(sc->nic_addr + ED_P1_CURR)) {

		/* get pointer to this buffer's header structure */
		packet_ptr = sc->mem_ring +
		    (sc->next_packet - sc->rec_page_start) * ED_PAGE_SIZE;

		/*
		 * The byte count includes a 4 byte header that was added by
		 * the NIC.
		 */
		if (sc->mem_shared)
			packet_hdr = *(struct ed_ring *) packet_ptr;
		else
			ed_pio_readmem(sc, (int)packet_ptr, (char *) &packet_hdr,
				       sizeof(packet_hdr));
		len = packet_hdr.count;
		if (len > (ETHER_MAX_LEN - ETHER_CRC_LEN + sizeof(struct ed_ring)) ||
		    len < (ETHER_MIN_LEN - ETHER_CRC_LEN + sizeof(struct ed_ring))) {
			/*
			 * Length is a wild value. There's a good chance that
			 * this was caused by the NIC being old and buggy.
			 * The bug is that the length low byte is duplicated in
			 * the high byte. Try to recalculate the length based on
			 * the pointer to the next packet.
			 */
			/*
			 * NOTE: sc->next_packet is pointing at the current packet.
			 */
			len &= ED_PAGE_SIZE - 1;	/* preserve offset into page */
			if (packet_hdr.next_packet >= sc->next_packet) {
				len += (packet_hdr.next_packet - sc->next_packet) * ED_PAGE_SIZE;
			} else {
				len += ((packet_hdr.next_packet - sc->rec_page_start) +
					(sc->rec_page_stop - sc->next_packet)) * ED_PAGE_SIZE;
			}
#if !defined(EXOPC)
			if (len > (ETHER_MAX_LEN - ETHER_CRC_LEN 
				   + sizeof(struct ed_ring)))
				sc->mibdata.dot3StatsFrameTooLongs++;
#endif
		}
		/*
		 * Be fairly liberal about what we allow as a "reasonable" length
		 * so that a [crufty] packet will make it to BPF (and can thus
		 * be analyzed). Note that all that is really important is that
		 * we have a length that will fit into one mbuf cluster or less;
		 * the upper layer protocols can then figure out the length from
		 * their own length field(s).
		 */
		if ((len > sizeof(struct ed_ring)) &&
		    (len <= MCLBYTES) &&
		    (packet_hdr.next_packet >= sc->rec_page_start) &&
		    (packet_hdr.next_packet < sc->rec_page_stop)) {
			/*
			 * Go get packet.
			 */
			ed_get_packet(sc, packet_ptr + sizeof(struct ed_ring),
				      min(ETHER_MAX_LEN, len - sizeof(struct ed_ring)), packet_hdr.rsr & ED_RSR_PHY);
#if !defined(EXOPC)
			ifp->if_ipackets++;
#endif
		} else {
			/*
			 * Really BAD. The ring pointers are corrupted.
			 */
#if defined(EXOPC)
               		printf ("ed: NIC memory corrupt - invalid packet length %d\n", len);
		        ed_reset (sc);
#else
			log(LOG_ERR,
			    "ed%d: NIC memory corrupt - invalid packet length %d\n",
			    ifp->if_unit, len);
			ifp->if_ierrors++;
			ed_reset(ifp);
#endif

			return;
		}

		/*
		 * Update next packet pointer
		 */
		sc->next_packet = packet_hdr.next_packet;

		/*
		 * Update NIC boundry pointer - being careful to keep it one
		 * buffer behind. (as recommended by NS databook)
		 */
		boundry = sc->next_packet - 1;
		if (boundry < sc->rec_page_start)
			boundry = sc->rec_page_stop - 1;

		/*
		 * Set NIC to page 0 registers to update boundry register
		 */
		outb(sc->nic_addr + ED_P0_CR, sc->cr_proto | ED_CR_STA);

		outb(sc->nic_addr + ED_P0_BNRY, boundry);

		/*
		 * Set NIC to page 1 registers before looping to top (prepare
		 * to get 'CURR' current pointer)
		 */
		outb(sc->nic_addr + ED_P0_CR, sc->cr_proto | ED_CR_PAGE_1 | ED_CR_STA);
	}
}

/*
 * Ethernet interface interrupt processor, returns 1 if a real device interrupt
 * occured. 0 otherwise.
 */
int
edintr_sc(sc)
	struct ed_softc *sc;
{
#if !defined(EXOPC)
	struct ifnet *ifp = (struct ifnet *)sc;
#endif
	u_char  isr;
#if defined(EXOPC)  
	int real_intr = 0;
#endif

	if (sc->gone)
		return 0;
	/*
	 * Set NIC to page 0 registers
	 */
	outb(sc->nic_addr + ED_P0_CR, sc->cr_proto | ED_CR_STA);

	/*
	 * loop until there are no more new interrupts
	 */
	while ((isr = inb(sc->nic_addr + ED_P0_ISR)) != 0) {
#if defined(EXOPC)  
		real_intr = 1;
#endif

		/*
		 * reset all the bits that we are 'acknowledging' by writing a
		 * '1' to each bit position that was set (writing a '1'
		 * *clears* the bit)
		 */
		outb(sc->nic_addr + ED_P0_ISR, isr);

		/*
		 * Handle transmitter interrupts. Handle these first because
		 * the receiver will reset the board under some conditions.
		 */
		if (isr & (ED_ISR_PTX | ED_ISR_TXE)) {
			u_char  collisions = inb(sc->nic_addr + ED_P0_NCR) & 0x0f;
			/*
			 * Check for transmit error. If a TX completed with an
			 * error, we end up throwing the packet away. Really
			 * the only error that is possible is excessive
			 * collisions, and in this case it is best to allow
			 * the automatic mechanisms of TCP to backoff the
			 * flow. Of course, with UDP we're screwed, but this
			 * is expected when a network is heavily loaded.
			 */
			(void) inb(sc->nic_addr + ED_P0_TSR);
			if (isr & ED_ISR_TXE) {
				u_char tsr;

				/*
				 * Excessive collisions (16)
				 */
				tsr = inb(sc->nic_addr + ED_P0_TSR);
				if ((tsr & ED_TSR_ABT)	
				    && (collisions == 0)) {

					/*
					 * When collisions total 16, the
					 * P0_NCR will indicate 0, and the
					 * TSR_ABT is set.
					 */
					collisions = 16;
#if !defined(EXOPC)
					sc->mibdata.dot3StatsExcessiveCollisions++;
					sc->mibdata.dot3StatsCollFrequencies[15]++;
#endif
				}
#if !defined(EXOPC)
				if (tsr & ED_TSR_OWC)
					sc->mibdata.dot3StatsLateCollisions++;
				if (tsr & ED_TSR_CDH)
					sc->mibdata.dot3StatsSQETestErrors++;
				if (tsr & ED_TSR_CRS)
					sc->mibdata.dot3StatsCarrierSenseErrors++;
				if (tsr & ED_TSR_FU)
					sc->mibdata.dot3StatsInternalMacTransmitErrors++;

				/*
				 * update output errors counter
				 */
				ifp->if_oerrors++;
#endif
			} else {
#if !defined(EXOPC)
				/*
				 * Update total number of successfully
				 * transmitted packets.
				 */
				ifp->if_opackets++;
#endif
			}

			/*
			 * reset tx busy and output active flags
			 */
			sc->xmit_busy = 0;
#if defined(EXOPC)
			sc->flags &= ~IFF_OACTIVE;
#else
			ifp->if_flags &= ~IFF_OACTIVE;


			/*
			 * clear watchdog timer
			 */
			ifp->if_timer = 0;

			/*
			 * Add in total number of collisions on last
			 * transmission.
			 */
			ifp->if_collisions += collisions;
			switch(collisions) {
			case 0:
			case 16:
				break;
			case 1:
				sc->mibdata.dot3StatsSingleCollisionFrames++;
				sc->mibdata.dot3StatsCollFrequencies[0]++;
				break;
			default:
				sc->mibdata.dot3StatsMultipleCollisionFrames++;
				sc->mibdata.
					dot3StatsCollFrequencies[collisions-1]
						++;
				break;
			}
#endif
			/*
			 * Decrement buffer in-use count if not zero (can only
			 * be zero if a transmitter interrupt occured while
			 * not actually transmitting). If data is ready to
			 * transmit, start it transmitting, otherwise defer
			 * until after handling receiver
			 */

			if (sc->txb_inuse && --sc->txb_inuse) {
				ed_xmit(sc);
			}
		}

		/*
		 * Handle receiver interrupts
		 */
		if (isr & (ED_ISR_PRX | ED_ISR_RXE | ED_ISR_OVW)) {
			/*
			 * Overwrite warning. In order to make sure that a
			 * lockup of the local DMA hasn't occurred, we reset
			 * and re-init the NIC. The NSC manual suggests only a
			 * partial reset/re-init is necessary - but some chips
			 * seem to want more. The DMA lockup has been seen
			 * only with early rev chips - Methinks this bug was
			 * fixed in later revs. -DG
			 */
			if (isr & ED_ISR_OVW) {
#if !defined(EXOPC)
				ifp->if_ierrors++;
#endif
#ifdef DIAGNOSTIC
				log(LOG_WARNING,
				    "ed%d: warning - receiver ring buffer overrun\n",
				    ifp->if_unit);
#endif

				/*
				 * Stop/reset/re-init NIC
				 */
#if defined(EXOPC)
				ed_reset(sc);
#else
				ed_reset(ifp);
#endif
				
			} else {

				/*
				 * Receiver Error. One or more of: CRC error,
				 * frame alignment error FIFO overrun, or
				 * missed packet.
				 */
				if (isr & ED_ISR_RXE) {
					u_char rsr;
					rsr = inb(sc->nic_addr + ED_P0_RSR);
#if !defined(EXOPC)
					if (rsr & ED_RSR_CRC)
						sc->mibdata.dot3StatsFCSErrors++;
					if (rsr & ED_RSR_FAE)
						sc->mibdata.dot3StatsAlignmentErrors++;
					if (rsr & ED_RSR_FO)
						sc->mibdata.dot3StatsInternalMacReceiveErrors++;
					ifp->if_ierrors++;
#endif
#ifdef ED_DEBUG
					printf("ed%d: receive error %x\n", ifp->if_unit,
					       inb(sc->nic_addr + ED_P0_RSR));
#endif
				}

				/*
				 * Go get the packet(s) XXX - Doing this on an
				 * error is dubious because there shouldn't be
				 * any data to get (we've configured the
				 * interface to not accept packets with
				 * errors).
				 */

#if !defined(EXOPC)
				/*
				 * Enable 16bit access to shared memory first
				 * on WD/SMC boards.
				 */
				if (sc->isa16bit &&
				    (sc->vendor == ED_VENDOR_WD_SMC)) {

					outb(sc->asic_addr + ED_WD_LAAR,
					     sc->wd_laar_proto | ED_WD_LAAR_M16EN);
					if (sc->is790) {
						outb(sc->asic_addr + ED_WD_MSR,
						     ED_WD_MSR_MENB);
					}
				}
#endif
#if defined(EXOPC)
				/* record the fact we received a packet */
				sc->xoknet->rcvs++;
#endif
				ed_rint(sc);
#if !defined(EXOPC)
				/* disable 16bit access */
				if (sc->isa16bit &&
				    (sc->vendor == ED_VENDOR_WD_SMC)) {

					if (sc->is790) {
						outb(sc->asic_addr + ED_WD_MSR, 0x00);
					}
					outb(sc->asic_addr + ED_WD_LAAR,
					     sc->wd_laar_proto & ~ED_WD_LAAR_M16EN);
				}
#endif
			}
		}

		/*
		 * If it looks like the transmitter can take more data,
		 * attempt to start output on the interface. This is done
		 * after handling the receiver to give the receiver priority.
		 */
	
#if defined(EXOPC)
		ed_start (sc);
#else
		if ((ifp->if_flags & IFF_OACTIVE) == 0)
			ed_start(ifp);
#endif

		/*
		 * return NIC CR to standard state: page 0, remote DMA
		 * complete, start (toggling the TXP bit off, even if was just
		 * set in the transmit routine, is *okay* - it is 'edge'
		 * triggered from low to high)
		 */
		outb(sc->nic_addr + ED_P0_CR, sc->cr_proto | ED_CR_STA);

		/*
		 * If the Network Talley Counters overflow, read them to reset
		 * them. It appears that old 8390's won't clear the ISR flag
		 * otherwise - resulting in an infinite loop.
		 */
		if (isr & ED_ISR_CNT) {
			(void) inb(sc->nic_addr + ED_P0_CNTR0);
			(void) inb(sc->nic_addr + ED_P0_CNTR1);
			(void) inb(sc->nic_addr + ED_P0_CNTR2);
		}
	}
	return real_intr;
}

void 
edintr(int irq)
{
#if defined(EXOPC)  
  int i, real_intr;

  for (i=0; i<SYSINFO_GET(si_nnetworks); i++) 
  {
    struct network *nw = SYSINFO_PTR_AT(si_networks,i);
    if (nw->irq == irq) 
    {
      struct ed_softc *sc = (struct ed_softc *) nw->cardstruct;

      test_and_set((unsigned long)&(nw->intr_pending));
      while (nw->intr_pending > 0 && 
	     test_and_set((unsigned long)&(nw->in_intr)) == 0)
      {
	while(nw->intr_pending > 0)
	{ 
	  sc->xoknet->intrs++; 
	  nw->intr_pending = 0;
	  real_intr = edintr_sc (sc); 
	
	  if (real_intr > 0) xokpkt_consume ();
	}
	nw->in_intr = 0;
      }
    }
  }

#else /* EXOPC */
  edintr_sc (&ed_softc[unit]);
#endif
}

#if !defined(EXOPC)
/*
 * Process an ioctl request. This code needs some work - it looks
 *	pretty ugly.
 */
static int
ed_ioctl(ifp, command, data)
	register struct ifnet *ifp;
	int     command;
	caddr_t data;
{
	struct ed_softc *sc = ifp->if_softc;
	int     s, error = 0;

	if (sc->gone) {
		ifp->if_flags &= ~IFF_RUNNING;
		return ENXIO;
	}
	s = splimp();

	switch (command) {

	case SIOCSIFADDR:
	case SIOCGIFADDR:
	case SIOCSIFMTU:
		error = ether_ioctl(ifp, command, data);
		break;

	case SIOCSIFFLAGS:

		/*
		 * If the interface is marked up and stopped, then start it.
		 * If it is marked down and running, then stop it.
		 */
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) == 0)
				ed_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING) {
				ed_stop(sc);
				ifp->if_flags &= ~IFF_RUNNING;
			}
		}

#if NBPFILTER > 0

		/*
		 * Promiscuous flag may have changed, so reprogram the RCR.
		 */
		ed_setrcr(sc);
#endif

		/*
		 * An unfortunate hack to provide the (required) software
		 * control of the tranceiver for 3Com boards. The ALTPHYS flag
		 * disables the tranceiver if set.
		 */
		if (sc->vendor == ED_VENDOR_3COM) {
			if (ifp->if_flags & IFF_ALTPHYS) {
				outb(sc->asic_addr + ED_3COM_CR, 0);
			} else {
				outb(sc->asic_addr + ED_3COM_CR, ED_3COM_CR_XSEL);
			}
		} else if (sc->vendor == ED_VENDOR_HP) 
			ed_hpp_set_physical_link(sc);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/*
		 * Multicast list has changed; set the hardware filter
		 * accordingly.
		 */
		ed_setrcr(sc);
		error = 0;
		break;

	default:
		error = EINVAL;
	}
	(void) splx(s);
	return (error);
}
#endif /* !defined(EXOPC) */

/*
 * Given a source and destination address, copy 'amount' of a packet from
 *	the ring buffer into a linear destination buffer. Takes into account
 *	ring-wrap.
 */
static inline char *
ed_ring_copy(sc, src, dst, amount)
	struct ed_softc *sc;
	char   *src;
	char   *dst;
	u_short amount;
{
	u_short tmp_amount;

	/* does copy wrap to lower addr in ring buffer? */
	if (src + amount > sc->mem_end) {
		tmp_amount = sc->mem_end - src;

		/* copy amount up to end of NIC memory */
		if (sc->mem_shared)
			bcopy(src, dst, tmp_amount);
		else
			ed_pio_readmem(sc, (int)src, dst, tmp_amount);

		amount -= tmp_amount;
		src = sc->mem_ring;
		dst += tmp_amount;
	}
	if (sc->mem_shared)
		bcopy(src, dst, amount);
	else
		ed_pio_readmem(sc, (int)src, dst, amount);

	return (src + amount);
}

#if defined(EXOPC)

static void
ed_get_packet (struct ed_softc *sc, char *buf, u_short len, int multicast) {
  int error;
  struct xokpkt *pkt = xokpkt_alloc(len, sc->xoknet->netno, &sc->xoknet->discards, &error);
  ed_ring_copy (sc, buf, pkt->data, len);
  xokpkt_recv (pkt);
}
 
#else /* defined(EXOPC) */ 

/*
 * Retreive packet from shared memory and send to the next level up via
 *	ether_input(). If there is a BPF listener, give a copy to BPF, too.
 */
static void
ed_get_packet(sc, buf, len, multicast)
	struct ed_softc *sc;
	char   *buf;
	u_short len;
	int     multicast;
{
	struct ether_header *eh;
	struct mbuf *m;

	/* Allocate a header mbuf */
	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return;
	m->m_pkthdr.rcvif = &sc->arpcom.ac_if;
	m->m_pkthdr.len = m->m_len = len;

	/*
	 * We always put the received packet in a single buffer -
	 * either with just an mbuf header or in a cluster attached
	 * to the header. The +2 is to compensate for the alignment
	 * fixup below.
	 */
	if ((len + 2) > MHLEN) {
		/* Attach an mbuf cluster */
		MCLGET(m, M_DONTWAIT);

		/* Insist on getting a cluster */
		if ((m->m_flags & M_EXT) == 0) {
			m_freem(m);
			return;
		}
	}

	/*
	 * The +2 is to longword align the start of the real packet.
	 * This is important for NFS.
	 */
	m->m_data += 2;
	eh = mtod(m, struct ether_header *);

	/*
	 * Get packet, including link layer address, from interface.
	 */
	ed_ring_copy(sc, buf, (char *)eh, len);

#if NBPFILTER > 0

	/*
	 * Check if there's a BPF listener on this interface. If so, hand off
	 * the raw packet to bpf.
	 */
	if (sc->arpcom.ac_if.if_bpf) {
		bpf_mtap(&sc->arpcom.ac_if, m);

		/*
		 * Note that the interface cannot be in promiscuous mode if
		 * there are no BPF listeners.  And if we are in promiscuous
		 * mode, we have to check if this packet is really ours.
		 */
		if ((sc->arpcom.ac_if.if_flags & IFF_PROMISC) &&
		    bcmp(eh->ether_dhost, sc->arpcom.ac_enaddr,
		      sizeof(eh->ether_dhost)) != 0 && multicast == 0) {
			m_freem(m);
			return;
		}
	}
#endif

	/*
	 * Remove link layer address.
	 */
	m->m_pkthdr.len = m->m_len = len - sizeof(struct ether_header);
	m->m_data += sizeof(struct ether_header);

	ether_input(&sc->arpcom.ac_if, eh, m);
	return;
}

#endif /* defined(EXOPC) */

/*
 * Supporting routines
 */

/*
 * Given a NIC memory source address and a host memory destination
 *	address, copy 'amount' from NIC to host using Programmed I/O.
 *	The 'amount' is rounded up to a word - okay as long as mbufs
 *		are word sized.
 *	This routine is currently Novell-specific.
 */
static void
ed_pio_readmem(sc, src, dst, amount)
	struct ed_softc *sc;
	int src;
	unsigned char *dst;
	unsigned short amount;
{
	/* HP cards need special handling */
	if (sc->vendor == ED_VENDOR_HP && sc->type == ED_TYPE_HP_PCLANPLUS) {
		ed_hpp_readmem(sc, src, dst, amount);
		return;
	}
		
	/* Regular Novell cards */
	/* select page 0 registers */
	outb(sc->nic_addr + ED_P0_CR, ED_CR_RD2 | ED_CR_STA);

	/* round up to a word */
	if (amount & 1)
		++amount;

	/* set up DMA byte count */
	outb(sc->nic_addr + ED_P0_RBCR0, amount);
	outb(sc->nic_addr + ED_P0_RBCR1, amount >> 8);

	/* set up source address in NIC mem */
	outb(sc->nic_addr + ED_P0_RSAR0, src);
	outb(sc->nic_addr + ED_P0_RSAR1, src >> 8);

	outb(sc->nic_addr + ED_P0_CR, ED_CR_RD0 | ED_CR_STA);

	if (sc->isa16bit) {
		insw(sc->asic_addr + ED_NOVELL_DATA, dst, amount / 2);
	} else
		insb(sc->asic_addr + ED_NOVELL_DATA, dst, amount);

}

/*
 * Stripped down routine for writing a linear buffer to NIC memory.
 *	Only used in the probe routine to test the memory. 'len' must
 *	be even.
 */
static void
ed_pio_writemem(sc, src, dst, len)
	struct ed_softc *sc;
	char   *src;
	unsigned short dst;
	unsigned short len;
{
	int     maxwait = 200;	/* about 240us */

	if (sc->vendor == ED_VENDOR_NOVELL) {

		/* select page 0 registers */
		outb(sc->nic_addr + ED_P0_CR, ED_CR_RD2 | ED_CR_STA);

		/* reset remote DMA complete flag */
		outb(sc->nic_addr + ED_P0_ISR, ED_ISR_RDC);

		/* set up DMA byte count */
		outb(sc->nic_addr + ED_P0_RBCR0, len);
		outb(sc->nic_addr + ED_P0_RBCR1, len >> 8);

		/* set up destination address in NIC mem */
		outb(sc->nic_addr + ED_P0_RSAR0, dst);
		outb(sc->nic_addr + ED_P0_RSAR1, dst >> 8);

		/* set remote DMA write */
		outb(sc->nic_addr + ED_P0_CR, ED_CR_RD1 | ED_CR_STA);

		if (sc->isa16bit)
			outsw(sc->asic_addr + ED_NOVELL_DATA, src, len / 2);
		else
			outsb(sc->asic_addr + ED_NOVELL_DATA, src, len);

		/*
		 * Wait for remote DMA complete. This is necessary because on the
		 * transmit side, data is handled internally by the NIC in bursts and
		 * we can't start another remote DMA until this one completes. Not
		 * waiting causes really bad things to happen - like the NIC
		 * irrecoverably jamming the ISA bus.
		 */
		while (((inb(sc->nic_addr + ED_P0_ISR) & ED_ISR_RDC) != ED_ISR_RDC) && --maxwait);

	} else if ((sc->vendor == ED_VENDOR_HP) && 
		   (sc->type == ED_TYPE_HP_PCLANPLUS)) { 

		/* HP PCLAN+ */

		/* reset remote DMA complete flag */
		outb(sc->nic_addr + ED_P0_ISR, ED_ISR_RDC);

		/* program the write address in RAM */
		outw(sc->asic_addr + ED_HPP_PAGE_0, dst);

		if (sc->hpp_mem_start) {
			u_short *s = (u_short *) src;
			volatile u_short *d = (u_short *) sc->hpp_mem_start;
			u_short *const fence = s + (len >> 1);

			/*
			 * Enable memory mapped access.
			 */

			outw(sc->asic_addr + ED_HPP_OPTION, 
			     sc->hpp_options & 
				~(ED_HPP_OPTION_MEM_DISABLE | 
				  ED_HPP_OPTION_BOOT_ROM_ENB));

			/*
			 * Copy to NIC memory.
			 */

			while (s < fence)
				*d = *s++;

			/*
			 * Restore Boot ROM access.
			 */

			outw(sc->asic_addr + ED_HPP_OPTION,
			     sc->hpp_options);

		} else {
			/* write data using I/O writes */
			outsw(sc->asic_addr + ED_HPP_PAGE_4, src, len / 2);
		}

	}
}

#if !defined(EXOPC)

/*
 * Write an mbuf chain to the destination NIC memory address using
 *	programmed I/O.
 */
static u_short
ed_pio_write_mbufs(sc, m, dst)
	struct ed_softc *sc;
	struct mbuf *m;
	int dst;
{
	struct ifnet *ifp = (struct ifnet *)sc;
	unsigned short total_len, dma_len;
	struct mbuf *mp;
	int     maxwait = 200;	/* about 240us */

	/*  HP PC Lan+ cards need special handling */
	if ((sc->vendor == ED_VENDOR_HP) && 
	    (sc->type == ED_TYPE_HP_PCLANPLUS)) {
		return ed_hpp_write_mbufs(sc, m, dst);
	}

	/* First, count up the total number of bytes to copy */
	for (total_len = 0, mp = m; mp; mp = mp->m_next)
		total_len += mp->m_len;

	dma_len = total_len;
	if (sc->isa16bit && (dma_len & 1))
		dma_len++;

	/* select page 0 registers */
	outb(sc->nic_addr + ED_P0_CR, ED_CR_RD2 | ED_CR_STA);

	/* reset remote DMA complete flag */
	outb(sc->nic_addr + ED_P0_ISR, ED_ISR_RDC);

	/* set up DMA byte count */
	outb(sc->nic_addr + ED_P0_RBCR0, dma_len);
	outb(sc->nic_addr + ED_P0_RBCR1, dma_len >> 8);

	/* set up destination address in NIC mem */
	outb(sc->nic_addr + ED_P0_RSAR0, dst);
	outb(sc->nic_addr + ED_P0_RSAR1, dst >> 8);

	/* set remote DMA write */
	outb(sc->nic_addr + ED_P0_CR, ED_CR_RD1 | ED_CR_STA);

  /*
   * Transfer the mbuf chain to the NIC memory.
   * 16-bit cards require that data be transferred as words, and only words.
   * So that case requires some extra code to patch over odd-length mbufs.
   */

	if (!sc->isa16bit) {
		/* NE1000s are easy */
		while (m) {
			if (m->m_len) {
				outsb(sc->asic_addr + ED_NOVELL_DATA,
				      m->m_data, m->m_len);
			}
			m = m->m_next;
		}
	} else {
		/* NE2000s are a pain */
		unsigned char *data;
		int len, wantbyte;
		unsigned char savebyte[2];

		wantbyte = 0;

		while (m) {
			len = m->m_len;
			if (len) {
				data = mtod(m, caddr_t);
				/* finish the last word */
				if (wantbyte) {
					savebyte[1] = *data;
					outw(sc->asic_addr + ED_NOVELL_DATA, *(u_short *)savebyte);
					data++;
					len--;
					wantbyte = 0;
				}
				/* output contiguous words */
				if (len > 1) {
					outsw(sc->asic_addr + ED_NOVELL_DATA,
					      data, len >> 1);
					data += len & ~1;
					len &= 1;
				}
				/* save last byte, if necessary */
				if (len == 1) {
					savebyte[0] = *data;
					wantbyte = 1;
				}
			}
			m = m->m_next;
		}
		/* spit last byte */
		if (wantbyte) {
			outw(sc->asic_addr + ED_NOVELL_DATA, *(u_short *)savebyte);
		}
	}

	/*
	 * Wait for remote DMA complete. This is necessary because on the
	 * transmit side, data is handled internally by the NIC in bursts and
	 * we can't start another remote DMA until this one completes. Not
	 * waiting causes really bad things to happen - like the NIC
	 * irrecoverably jamming the ISA bus.
	 */
	while (((inb(sc->nic_addr + ED_P0_ISR) & ED_ISR_RDC) != ED_ISR_RDC) && --maxwait);

	if (!maxwait) {
		log(LOG_WARNING, "ed%d: remote transmit DMA failed to complete\n",
		    ifp->if_unit);
		ed_reset(ifp);
		return(0);
	}
	return (total_len);
}

#endif /* !defined(EXOPC) */

/*
 * Support routines to handle the HP PC Lan+ card.
 */

/*
 * HP PC Lan+: Read from NIC memory, using either PIO or memory mapped
 * IO.
 */

static void
ed_hpp_readmem(sc, src, dst, amount)
	struct ed_softc *sc; 
	unsigned short src;
	unsigned char *dst;
	unsigned short amount;
{

	int use_32bit_access = !(sc->hpp_id & ED_HPP_ID_16_BIT_ACCESS);


	/* Program the source address in RAM */
	outw(sc->asic_addr + ED_HPP_PAGE_2, src);

	/*
	 * The HP PC Lan+ card supports word reads as well as
	 * a memory mapped i/o port that is aliased to every 
	 * even address on the board.
	 */

	if (sc->hpp_mem_start) {

		/* Enable memory mapped access.  */
		outw(sc->asic_addr + ED_HPP_OPTION, 
		     sc->hpp_options & 
			~(ED_HPP_OPTION_MEM_DISABLE | 
			  ED_HPP_OPTION_BOOT_ROM_ENB));

		if (use_32bit_access && (amount > 3)) {
			u_long *dl = (u_long *) dst;	
			volatile u_long *const sl = 
				(u_long *) sc->hpp_mem_start;
			u_long *const fence = dl + (amount >> 2);
			
			/* Copy out NIC data.  We could probably write this
			   as a `movsl'. The currently generated code is lousy.
			   */

			while (dl < fence)
				*dl++ = *sl;
		
			dst += (amount & ~3);
			amount &= 3;

		} 

		/* Finish off any words left, as a series of short reads */
		if (amount > 1) {
			u_short *d = (u_short *) dst;	
			volatile u_short *const s = 
				(u_short *) sc->hpp_mem_start;
			u_short *const fence = d + (amount >> 1);
			
			/* Copy out NIC data.  */

			while (d < fence)
				*d++ = *s;
	
			dst += (amount & ~1);
			amount &= 1;
		}

		/*
		 * read in a byte; however we need to always read 16 bits
		 * at a time or the hardware gets into a funny state
		 */

		if (amount == 1) {
			/* need to read in a short and copy LSB */
			volatile u_short *const s = 
				(volatile u_short *) sc->hpp_mem_start;
			
			*dst = (*s) & 0xFF;	
		}

		/* Restore Boot ROM access.  */

		outw(sc->asic_addr + ED_HPP_OPTION,
		     sc->hpp_options);


	} else { 
		/* Read in data using the I/O port */
		if (use_32bit_access && (amount > 3)) {
			insl(sc->asic_addr + ED_HPP_PAGE_4, dst, amount >> 2);
			dst += (amount & ~3);
			amount &= 3;
		}
		if (amount > 1) {
			insw(sc->asic_addr + ED_HPP_PAGE_4, dst, amount >> 1);
			dst += (amount & ~1);
			amount &= 1;
		}
		if (amount == 1) { /* read in a short and keep the LSB */
			*dst = inw(sc->asic_addr + ED_HPP_PAGE_4) & 0xFF;
		}
	}
}

#if !defined(EXOPC)

/*
 * Write to HP PC Lan+ NIC memory.  Access to the NIC can be by using 
 * outsw() or via the memory mapped interface to the same register.
 * Writes have to be in word units; byte accesses won't work and may cause
 * the NIC to behave wierdly. Long word accesses are permitted if the ASIC
 * allows it.
 */

static u_short
ed_hpp_write_mbufs(struct ed_softc *sc, struct mbuf *m, int dst)
{
	int len, wantbyte;
	unsigned short total_len;
	unsigned char savebyte[2];
	volatile u_short * const d = 
		(volatile u_short *) sc->hpp_mem_start;
	int use_32bit_accesses = !(sc->hpp_id & ED_HPP_ID_16_BIT_ACCESS);

	/* select page 0 registers */
	outb(sc->nic_addr + ED_P0_CR, sc->cr_proto | ED_CR_STA);

	/* reset remote DMA complete flag */
	outb(sc->nic_addr + ED_P0_ISR, ED_ISR_RDC);

	/* program the write address in RAM */
	outw(sc->asic_addr + ED_HPP_PAGE_0, dst);

	if (sc->hpp_mem_start) 	/* enable memory mapped I/O */
		outw(sc->asic_addr + ED_HPP_OPTION, sc->hpp_options & 
			~(ED_HPP_OPTION_MEM_DISABLE |
			ED_HPP_OPTION_BOOT_ROM_ENB));

	wantbyte = 0;
	total_len = 0;

	if (sc->hpp_mem_start) {	/* Memory mapped I/O port */
		while (m) {
			total_len += (len = m->m_len);
			if (len) {
				caddr_t data = mtod(m, caddr_t);
				/* finish the last word of the previous mbuf */
				if (wantbyte) {
					savebyte[1] = *data;
					*d = *((ushort *) savebyte);
					data++; len--; wantbyte = 0;
				}
				/* output contiguous words */
				if ((len > 3) && (use_32bit_accesses)) {
					volatile u_long *const dl = 
						(volatile u_long *) d;
					u_long *sl = (u_long *) data;
					u_long *fence = sl + (len >> 2);

					while (sl < fence)
						*dl = *sl++;

					data += (len & ~3);
					len &= 3;
				}
				/* finish off remain 16 bit writes */
				if (len > 1) {
					u_short *s = (u_short *) data;
					u_short *fence = s + (len >> 1);

					while (s < fence)
						*d = *s++;

					data += (len & ~1); 
					len &= 1;
				}
				/* save last byte if needed */
				if (wantbyte = (len == 1)) 
					savebyte[0] = *data;
			}
			m = m->m_next;	/* to next mbuf */
		}
		if (wantbyte) /* write last byte */
			*d = *((u_short *) savebyte);
	} else {
		/* use programmed I/O */
		while (m) {
			total_len += (len = m->m_len);
			if (len) {
				caddr_t data = mtod(m, caddr_t);
				/* finish the last word of the previous mbuf */
				if (wantbyte) {
					savebyte[1] = *data;
					outw(sc->asic_addr + ED_HPP_PAGE_4, 
					     *((u_short *)savebyte));
					data++; 
					len--; 
					wantbyte = 0;
				}
				/* output contiguous words */
				if ((len > 3) && use_32bit_accesses) {
					outsl(sc->asic_addr + ED_HPP_PAGE_4,
						data, len >> 2);
					data += (len & ~3);
					len &= 3;
				}
				/* finish off remaining 16 bit accesses */
				if (len > 1) {
					outsw(sc->asic_addr + ED_HPP_PAGE_4,
					      data, len >> 1);
					data += (len & ~1);
					len &= 1;
				}
				if (wantbyte = (len == 1)) 
					savebyte[0] = *data;

			} /* if len != 0 */
			m = m->m_next;
		}
		if (wantbyte) /* spit last byte */
			outw(sc->asic_addr + ED_HPP_PAGE_4, 
				*(u_short *)savebyte);

	}

	if (sc->hpp_mem_start)	/* turn off memory mapped i/o */
		outw(sc->asic_addr + ED_HPP_OPTION,
		     sc->hpp_options);

	return (total_len);
}

#endif /* !defined(EXOPC) */

static void
ed_setrcr(sc)
	struct ed_softc *sc;
{
#if !defined(EXOPC)
	struct ifnet *ifp = (struct ifnet *)sc;
#endif
	int     i;

	/* set page 1 registers */
	outb(sc->nic_addr + ED_P0_CR, sc->cr_proto | ED_CR_PAGE_1 | ED_CR_STP);

#if defined(EXOPC)
	if (sc->flags & IFF_PROMISC) {
#else
	if (ifp->if_flags & IFF_PROMISC) {
#endif

		/*
		 * Reconfigure the multicast filter.
		 */
		for (i = 0; i < 8; i++)
			outb(sc->nic_addr + ED_P1_MAR0 + i, 0xff);

		/*
		 * And turn on promiscuous mode. Also enable reception of
		 * runts and packets with CRC & alignment errors.
		 */
		/* Set page 0 registers */
		outb(sc->nic_addr + ED_P0_CR, sc->cr_proto | ED_CR_STP);

		outb(sc->nic_addr + ED_P0_RCR, ED_RCR_PRO | ED_RCR_AM |
		     ED_RCR_AB | ED_RCR_AR | ED_RCR_SEP);
	} else {
		/* set up multicast addresses and filter modes */
#if defined(EXOPC)
                if (sc->flags & IFF_MULTICAST) {
#else
		if (ifp->if_flags & IFF_MULTICAST) {
#endif
			u_long  mcaf[2];

#if defined(EXOPC)
			if (sc->flags & IFF_ALLMULTI) {
#else
			if (ifp->if_flags & IFF_ALLMULTI) {
#endif

				mcaf[0] = 0xffffffff;
				mcaf[1] = 0xffffffff;
			} else
				ds_getmcaf(sc, mcaf);

			/*
			 * Set multicast filter on chip.
			 */
			for (i = 0; i < 8; i++)
				outb(sc->nic_addr + ED_P1_MAR0 + i, ((u_char *) mcaf)[i]);

			/* Set page 0 registers */
			outb(sc->nic_addr + ED_P0_CR, sc->cr_proto | ED_CR_STP);

			outb(sc->nic_addr + ED_P0_RCR, ED_RCR_AM | ED_RCR_AB);
		} else {

			/*
			 * Initialize multicast address hashing registers to
			 * not accept multicasts.
			 */
			for (i = 0; i < 8; ++i)
				outb(sc->nic_addr + ED_P1_MAR0 + i, 0x00);

			/* Set page 0 registers */
			outb(sc->nic_addr + ED_P0_CR, sc->cr_proto | ED_CR_STP);

			outb(sc->nic_addr + ED_P0_RCR, ED_RCR_AB);
		}
	}

	/*
	 * Start interface.
	 */
	outb(sc->nic_addr + ED_P0_CR, sc->cr_proto | ED_CR_STA);
}

/*
 * Compute crc for ethernet address
 */
static u_long
ds_crc(ep)
	u_char *ep;
{
#define POLYNOMIAL 0x04c11db6
	register u_long crc = 0xffffffffL;
	register int carry, i, j;
	register u_char b;

	for (i = 6; --i >= 0;) {
		b = *ep++;
		for (j = 8; --j >= 0;) {
			carry = ((crc & 0x80000000L) ? 1 : 0) ^ (b & 0x01);
			crc <<= 1;
			b >>= 1;
			if (carry)
				crc = ((crc ^ POLYNOMIAL) | carry);
		}
	}
	return crc;
#undef POLYNOMIAL
}

/*
 * Compute the multicast address filter from the
 * list of multicast addresses we need to listen to.
 */
static void
ds_getmcaf(sc, mcaf)
	struct ed_softc *sc;
	u_long *mcaf;
{

#if defined(EXOPC)
	assert (0);
#else
	register u_int index;
	register u_char *af = (u_char *) mcaf;
	struct ifmultiaddr *ifma;

	mcaf[0] = 0;
	mcaf[1] = 0;

	for (ifma = sc->arpcom.ac_if.if_multiaddrs.lh_first; ifma;
	     ifma = ifma->ifma_link.le_next) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		index = ds_crc(LLADDR((struct sockaddr_dl *)ifma->ifma_addr))
			>> 26;
		af[index >> 3] |= 1 << (index & 7);
	}
#endif
}

#if !defined(EXOPC)

/*
 * support PnP cards if we are using 'em
 */

#if NPNP > 0

static struct edpnp_ids {
	u_long vend_id;
	char *id_str;
} edpnp_ids[] = {
	{ 0x1980635e, "WSC8019"},
	{ 0 }
};

static char *edpnp_probe(u_long csn, u_long vend_id);
static void edpnp_attach(u_long csn, u_long vend_id, char *name,
	struct isa_device *dev);
static u_long nedpnp = NED;

static struct pnp_device edpnp = {
	"edpnp",
	edpnp_probe,
	edpnp_attach,
	&nedpnp,
	&net_imask
};
DATA_SET (pnpdevice_set, edpnp);

static char *
edpnp_probe(u_long csn, u_long vend_id)
{
	struct edpnp_ids *ids;
	char *s = NULL;

	for(ids = edpnp_ids; ids->vend_id != 0; ids++) {
		if (vend_id == ids->vend_id) {
			s = ids->id_str;
			break;
		}
	}

	if (s) {
		struct pnp_cinfo d;
		read_pnp_parms(&d, 0);
		if (d.enable == 0 || d.flags & 1) {
			printf("CSN %d is disabled.\n", csn);
			return (NULL);
		}

	}

	return (s);
}

static void
edpnp_attach(u_long csn, u_long vend_id, char *name, struct isa_device *dev)
{
	struct pnp_cinfo d;
	struct isa_device *dvp;

	if (dev->id_unit >= NEDTOT)
		return;

	if (read_pnp_parms(&d, 0) == 0) {
		printf("failed to read pnp parms\n");
		return;
	}

	write_pnp_parms(&d, 0);

	enable_pnp_card();

	dev->id_iobase = d.port[0];
	dev->id_irq = (1 << d.irq[0]);
	dev->id_intr = edintr;
	dev->id_drq = -1;

	if (dev->id_driver == NULL) {
		dev->id_driver = &eddriver;
		dvp = find_isadev(isa_devtab_net, &eddriver, 0);
		if (dvp != NULL)
			dev->id_id = dvp->id_id;
	}

	if ((dev->id_alive = ed_probe(dev)) != 0)
		ed_attach_isa(dev);
	else
		printf("ed%d: probe failed\n", dev->id_unit);
}

#endif

#endif /* !defined(EXOPC) */

#if defined(EXOPC)


int xok_ed_xmit (struct ed_softc *sc, struct ae_recv *recv, int xmitintr)
{
   int i, sz = 0;
   char *dst;

  if (sc->txb_inuse >= sc->txb_cnt)
    return -1;

  dst = (char*)(sc->mem_start + ((sc->txb_new * ED_TXBUF_SIZE)
				 * ED_PAGE_SIZE));
  for (i=0; i<recv->n; i++) {
     bcopy (recv->r[i].data, dst, recv->r[i].sz);
     dst += recv->r[i].sz;
     sz += recv->r[i].sz;
   }

  if (sz > ETHER_MAX_LEN) return (-1);

  assert (sc->txb_inuse < sc->txb_cnt);

  sc->txb_len[sc->txb_new] = max (sz, ETHER_MIN_LEN);
  /* Point to next buffer slot and wrap if necessary. */
  if (++sc->txb_new == sc->txb_cnt)
    sc->txb_new = 0;
  sc->txb_inuse++;
  sc->xoknet->xmits++;
  ed_start (sc);

  ((struct xokpkt *) recv)->freeFunc((struct xokpkt*)recv);

  return (0);
}


void exo_ed_init () {
  int i, j;
  struct isa_device dev;
  int next_unit = 0;
  struct ed_softc *cardstruct;

  for (i = 0; i < sizeof (ed_ports)/sizeof (ed_ports[0]) && next_unit < MAXSMC;
       i++) {
    for (j = 0; j < sizeof (ed_shm)/sizeof (ed_shm[0]) && next_unit < MAXSMC;
	 j++) {

      /* build a device struct describing this device */
      dev.id_iobase = ed_ports[i];
      dev.id_irq = 0;
      dev.id_maddr = (caddr_t )ptov(ed_shm[j]);
      dev.id_msize = 0;
      dev.id_unit = next_unit;
      dev.id_xoknetno = SYSINFO_GET(si_nnetworks); 
      dev.id_flags = 0;

      cardstruct = (struct ed_softc *) malloc (sizeof (struct ed_softc));
      bzero (cardstruct, sizeof (struct ed_softc));

      cardstruct->xoknet = xoknet_getstructure 
	(XOKNET_ED, (int(*)(void *, struct ae_recv *, int))xok_ed_xmit, 
	 cardstruct);

      /* probe for it and attach and init it if found */
      if (ed_probe (&dev) != 0) {

	ed_attach_isa (&dev);

        cardstruct->xoknet->irq = dev.id_irq;

	ed_init (cardstruct);

        cardstruct->xoknet->inited = 1;
	cardstruct->xoknet->in_intr = 0;
	cardstruct->xoknet->intr_pending = 0;
        bcopy (cardstruct->macaddr, cardstruct->xoknet->ether_addr, 
	       ETHER_ADDR_LEN);
        sprintf (cardstruct->xoknet->cardname, "ed%d", next_unit);
        assert (strlen(cardstruct->xoknet->cardname) < XOKNET_NAMEMAX);
        next_unit++;
      } else {
        free (cardstruct);
        Sysinfo_si_nnetworks_atomic_dec(si); 
      }
    }
  }
}

#endif


#ifdef __ENCAP__
#include <xok/sysinfoP.h>
#endif

