/*-
 * Copyright (c) 1998,1999 S�ren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILI
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/ata/ata-all.c,v 1.30 1999/11/08 21:35:59 sos Exp $
 */

/*Ported to exokernel by Eric Peterson, 12/99*/

#ifndef EXOPC

#include "ata.h"
#include "apm.h"
#include "isa.h"
#include "pci.h"
#include "atadisk.h"
#include "atapicd.h"
#include "atapifd.h"
#include "atapist.h"
#include "opt_global.h"
#include "opt_ata.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/disk.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/devicestat.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>
#if NPCI > 0
#include <pci/pcivar.h>
#include <pci/pcireg.h>
#endif
#include <isa/isavar.h>
#include <isa/isareg.h>
#include <machine/clock.h>
#ifdef __i386__
#include <machine/smp.h>
#include <i386/isa/intr_machdep.h>
#endif

#define NAPM -1
#if NAPM > 0
#include <machine/apm_bios.h>
#endif

#else /*EXOPC*/
/*EXOPC includes*/
#include <xok/types.h>
#include <xok/cdefs.h>
#include <xok/malloc.h>
#include <xok/defs.h>
#include <xok/pci.h>
#include <xok/bios32.h>
#include <xok/picirq.h>
#include <xok/buf.h>
#include <xok/env.h>
#include <xok/scheduler.h>
#include <xok/printf.h>
#include <xok/disk.h>
#include <machine/bus.h>
#include <machine/pio.h>
#include <xok/sysinfo.h>
#include <dev/isa/isareg.h>
#include "ideport.h"

extern void ad_attach(void *notused);


#endif

#include "ata-all.h"
#if 0


#endif
#include "ata-disk.h"

/* misc defines */
#if SMP == 0
#define isa_apic_irq(x) x
#endif
#define IOMASK	0xfffffffc /* XXX SOS 0xfffc */

/* prototypes */
static int32_t ata_probe(int32_t, int32_t, int32_t, device_t, int32_t *);

int ataintr(unsigned int);
static int8_t *active2str(int32_t);

/* local vars */
static int32_t atanlun = 2;
struct ata_softc *atadevicesbyirq[MAX_IRQ];
struct ata_softc *atadevices[MAXATA];

#ifndef EXOPC
/*this doesn't look necessary*/
static devclass_t ata_devclass;
#endif

MALLOC_DEFINE(M_ATA, "ATA generic", "ATA driver generic layer");




#if NISA > 0
static struct isa_pnp_id ata_ids[] = {
    {0x0006d041,	"Generic ESDI/IDE/ATA controller"},	/* PNP0600 */
    {0x0106d041,	"Plus Hardcard II"},			/* PNP0601 */
    {0x0206d041,	"Plus Hardcard IIXL/EZ"},		/* PNP0602 */
    {0x0306d041,	"Generic ATA"},				/* PNP0603 */
    {0}
};

static int
ata_isaprobe(device_t dev)
{
    struct resource *port;
    int rid;
    int32_t ctlr, res;
    int32_t lun;

    /* Check isapnp ids */
    if (ISA_PNP_PROBE(device_get_parent(dev), dev, ata_ids) == ENXIO)
	return (ENXIO);
    
    /* Allocate the port range */
    rid = 0;
    port = bus_alloc_resource(dev, SYS_RES_IOPORT, &rid, 0, ~0, 1, RF_ACTIVE);
    if (!port)
	return (ENOMEM);

    /* check if allready in use by a PCI device */
    for (ctlr = 0; ctlr < atanlun; ctlr++) {
	if (atadevices[ctlr] && atadevices[ctlr]->ioaddr==rman_get_start(port)){
	    printf("ata-isa%d: already registered as ata%d\n", 
		   device_get_unit(dev), ctlr);
	    bus_release_resource(dev, SYS_RES_IOPORT, 0, port);
	    return ENXIO;
	}
    }

    lun = 0;
    res = ata_probe(rman_get_start(port), rman_get_start(port) + ATA_ALTPORT,
		    0, dev, &lun);

    bus_release_resource(dev, SYS_RES_IOPORT, 0, port);

    if (res) {
	isa_set_portsize(dev, res);
	*(int *)device_get_softc(dev) = lun;
	return 0;
    }
    return ENXIO;
}

static int
ata_isaattach(device_t dev)
{
    struct resource *port;
    struct resource *irq;
    void *ih;
    int rid;

    /* Allocate the port range and interrupt */
    rid = 0;
    port = bus_alloc_resource(dev, SYS_RES_IOPORT, &rid, 0, ~0, 1, RF_ACTIVE);
    if (!port)
	return (ENOMEM);

    rid = 0;
    irq = bus_alloc_resource(dev, SYS_RES_IRQ, &rid, 0, ~0, 1, RF_ACTIVE);
    if (!irq) {
	bus_release_resource(dev, SYS_RES_IOPORT, 0, port);
	return (ENOMEM);
    }
    return bus_setup_intr(dev, irq, INTR_TYPE_BIO, ataintr, 
			  atadevices[*(int *)device_get_softc(dev)], &ih);
}

static device_method_t ata_isa_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,	ata_isaprobe),
    DEVMETHOD(device_attach,	ata_isaattach),
    { 0, 0 }
};

static driver_t ata_isa_driver = {
    "ata",
    ata_isa_methods,
    sizeof(int),
};

DRIVER_MODULE(ata, isa, ata_isa_driver, ata_devclass, 0, 0);
#endif /*ISA*/


#if NPCI > 0

#ifndef EXOPC
/*we don't need these probing routines since we're only
  supporting known devices*/
static const char *
ata_pcimatch(device_t dev)
{
    if (pci_get_class(dev) != PCIC_STORAGE)
	return NULL;

    switch (pci_get_devid(dev)) {
    /* supported chipsets */
    case 0x12308086:
	return "Intel PIIX IDE controller";
    case 0x70108086:
	return "Intel PIIX3 IDE controller";
    case 0x71118086:
	return "Intel PIIX4 IDE controller";
    case 0x522910b9:
	return "AcerLabs Aladdin IDE controller";
    case 0x4d33105a:
	return "Promise Ultra/33 IDE controller";
    case 0x4d38105a:
	return "Promise Ultra/66 IDE controller";
    case 0x00041103:
	return "HighPoint HPT366 IDE controller";

   /* unsupported but known chipsets, generic DMA only */
    case 0x05711106: /* 82c586 */
    case 0x05961106: /* 82c596 */
	return "VIA Apollo IDE controller (generic mode)";
    case 0x06401095:
	return "CMD 640 IDE controller (generic mode)";
    case 0x06461095:
	return "CMD 646 IDE controller (generic mode)";
    case 0xc6931080:
	return "Cypress 82C693 IDE controller (generic mode)";
    case 0x01021078:
	return "Cyrix 5530 IDE controller (generic mode)";
    default:
	if (pci_get_class(dev) == PCIC_STORAGE &&
	    (pci_get_subclass(dev) == PCIS_STORAGE_IDE))
	    return "Unknown PCI IDE controller (generic mode)";
    }
    return NULL;
}

static int
ata_pciprobe(device_t dev)
{
    const char *desc = ata_pcimatch(dev);
    if (desc) {
	device_set_desc(dev, desc);
	return 0;
    } 
    else
	return ENXIO;
}

#endif /*!EXOPC*/

void
ata_pciattach(device_t dev, const char *description)
{
#ifndef EXOPC
    int unit = device_get_unit(dev);
#else
    /*not sure what this is, use lun*/
    int unit = atanlun;
#endif
    struct ata_softc *scp;
    u_int32_t type;
    u_int8_t class, subclass;
    u_int32_t cmd;
    int32_t iobase_1, iobase_2, altiobase_1, altiobase_2; 
    int32_t bmaddr_1 = 0, bmaddr_2 = 0, irq1, irq2;
#ifndef EXOPC    
    struct resource *irq = NULL;
#endif    
    int32_t lun;

    /* set up vendor-specific stuff */
    type = dev->vendor | (dev->device << 16);
    class = dev->class>>16;
    subclass = (dev->class>>8) & 0xff;
    cmd = pci_read_config(dev, PCI_COMMAND, 4);

#ifdef ATA_DEBUG
    printf("ata-pci%d: type=%08x class=%02x subclass=%02x cmd=%08x\n",
			  unit, type, class, subclass, cmd/*pci_get_progif(dev)*/);
#endif

    if ((dev->class & 0xff) & PCIP_STORAGE_IDE_MASTERDEV) {
	iobase_1 = IO_WD1;
	altiobase_1 = iobase_1 + ATA_ALTPORT;
	irq1 = 14;
    } 
    else {
	iobase_1 = pci_read_config(dev, 0x10, 4) & PCI_BASE_ADDRESS_IO_MASK;
	altiobase_1 = pci_read_config(dev, 0x14, 4) & PCI_BASE_ADDRESS_IO_MASK;
	bmaddr_1 = pci_read_config(dev, 0x20, 4) & PCI_BASE_ADDRESS_IO_MASK;
	irq1 = pci_read_config(dev, PCI_INTERRUPT_LINE, 4) & 0xff;
    }

    if ((dev->class & 0xff) & PCIP_STORAGE_IDE_MASTERDEV) {
	iobase_2 = IO_WD2;
	altiobase_2 = iobase_2 + ATA_ALTPORT;
	irq2 = 15;
    }
    else {
	iobase_2 = pci_read_config(dev, 0x18, 4) & PCI_BASE_ADDRESS_IO_MASK;
	altiobase_2 = pci_read_config(dev, 0x1c, 4) & PCI_BASE_ADDRESS_IO_MASK;
	bmaddr_2 = (pci_read_config(dev, 0x20, 4) & PCI_BASE_ADDRESS_IO_MASK) + ATA_BM_OFFSET1;
	irq2 = pci_read_config(dev, PCI_INTERRUPT_LINE, 4) & 0xff;
    }

    /* is this controller busmaster DMA capable ? */
    if ((dev->class & 0xff) & PCIP_STORAGE_IDE_MASTERDEV) {
	/* is busmastering support turned on ? */
	if ((pci_read_config(dev, PCI_COMMAND, 4) & 5) == 5) {
	    /* is there a valid port range to connect to ? */
	    if ((bmaddr_1 = pci_read_config(dev, 0x20, 4) & PCI_BASE_ADDRESS_IO_MASK)) {
		bmaddr_2 = bmaddr_1 + ATA_BM_OFFSET1;
		printf("ata-pci%d: Busmastering DMA supported\n", unit);
	    }
	    else
		printf("ata-pci%d: Busmastering DMA not configured\n", unit);
	}
	else
	    printf("ata-pci%d: Busmastering DMA not enabled\n", unit);
    }
    else {
    	if (type == 0x4d33105a || type == 0x4d38105a || type == 0x00041103) {
	    /* Promise and HPT366 controllers support busmastering DMA */
	    printf("ata-pci%d: Busmastering DMA supported\n", unit);
	}
	else {
	    /* we dont know this controller, disable busmastering DMA */
	    bmaddr_1 = bmaddr_2 = 0;
	    printf("ata-pci%d: Busmastering DMA not supported\n", unit);
	}
    }

    /* on the Aladdin activate the ATAPI FIFO */
    if (type == 0x522910b9) {
	pci_write_config(dev, 0x53, 
			 (pci_read_config(dev, 0x53, 1) & ~0x01) | 0x02, 1);
    }

    /* the Promise controllers needs burst mode to be turned on explicitly */
    if (type == 0x4d33105a || type == 0x4d38105a)
	outb(bmaddr_1 + 0x1f, inb(bmaddr_1 + 0x1f) | 0x01);
	
    /* now probe the addresse found for "real" ATA/ATAPI hardware */
    lun = 0;
    if (iobase_1 && ata_probe(iobase_1, altiobase_1, bmaddr_1, dev, &lun)) {
	scp = atadevices[lun];
	if (iobase_1 == IO_WD1)
#ifdef __i386__
	  {
	  atadevicesbyirq[irq1] = scp;
	  scp->irq = irq1;
	  scp->dev = dev;
	  irq_sethandler (irq1, ataintr);
          irq_setmask_8259A (irq_mask_8259A & ~(1 << irq1));
          irq_eoi (irq1);
 
        // si->si_scsictlr_irqs[np->unit] = dev->irq;
        //SYSINFO_A_ASSIGN(si_scsictlr_irqs,np->unit,dev->irq);
	  }
	  /*inthand_add(device_get_nameunit(dev), irq1, ataintr, scp,
	    &bio_imask, INTR_EXCL);*/
#endif
#ifdef __alpha__
	    alpha_platform_setup_ide_intr(0, ataintr, scp);
#endif

#ifndef EXOPC
	else {
	    int rid = 0;
	    void *ih;

	    if (!(irq = bus_alloc_resource(dev, SYS_RES_IRQ, &rid, 0, ~0, 1,
					   RF_SHAREABLE | RF_ACTIVE)))
		printf("ata_pciattach: Unable to alloc interrupt\n");
	    bus_setup_intr(dev, irq, INTR_TYPE_BIO, ataintr, scp, &ih);
	}
#endif

	printf("ata%d at 0x%04x irq %d on ata-pci%d\n",
	       lun, iobase_1, isa_apic_irq(irq1), unit);
    }
    lun = 1;
    if (iobase_2 && ata_probe(iobase_2, altiobase_2, bmaddr_2, dev, &lun)) {
	scp = atadevices[lun];
	if (iobase_2 == IO_WD2)
#ifdef __i386__
	  {
	  atadevicesbyirq[irq2] = scp;
	  scp->irq = irq2;
	  scp->dev = dev;
	  irq_sethandler (irq2, ataintr);
	  irq_setmask_8259A (irq_mask_8259A & ~(1 << irq2));
          irq_eoi (irq2);
	  }

#if 0
	    inthand_add(device_get_nameunit(dev), irq2, ataintr, scp,
			&bio_imask, INTR_EXCL);
#endif
#endif
#ifdef __alpha__
	    alpha_platform_setup_ide_intr(1, ataintr, scp);
#endif
#ifndef EXOPC
	else {
	    int rid = 0;
	    void *ih;

	    if (irq1 != irq2 || irq == NULL) {
	  	if (!(irq = bus_alloc_resource(dev, SYS_RES_IRQ, &rid, 0, ~0, 1,
					       RF_SHAREABLE | RF_ACTIVE)))
		    printf("ata_pciattach: Unable to alloc interrupt\n");
	    }
	    bus_setup_intr(dev, irq, INTR_TYPE_BIO, ataintr, scp, &ih);
	}
#endif
	printf("ata%d at 0x%04x irq %d on ata-pci%d\n",
	       lun, iobase_2, isa_apic_irq(irq2), unit);
    }

    /*setup any disks*/
    ad_attach(0);
    //DELAY(10000000); 
    
}

#ifndef EXOPC
/*we don't need this module junk in exokernel*/
static device_method_t ata_pci_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,	ata_pciprobe),
    DEVMETHOD(device_attach,	ata_pciattach),
    { 0, 0 }
};

static driver_t ata_pci_driver = {
    "ata-pci",
    ata_pci_methods,
    sizeof(int),
};

DRIVER_MODULE(ata, pci, ata_pci_driver, ata_devclass, 0, 0);
#endif /*!EXOPC*/
#endif /*PCI*/

static int32_t
ata_probe(int32_t ioaddr, int32_t altioaddr, int32_t bmaddr,
	  device_t dev, int32_t *unit)
{
    struct ata_softc *scp;
    int32_t lun, mask = 0;
    u_int8_t status0, status1;

    if (atanlun > MAXATA) {
	printf("ata: unit out of range(%d)\n", atanlun);
	return 0;
    }

    /* check if this is located at one of the std addresses */
    if (ioaddr == IO_WD1)
	lun = 0;
    else if (ioaddr == IO_WD2)
	lun = 1;
    else
	lun = atanlun++;

    if ((scp = atadevices[lun])) {
	printf("ata%d: unit already attached\n", lun);
	return 0;
    }
    scp = malloc(sizeof(struct ata_softc), M_ATA, M_NOWAIT);
    if (scp == NULL) {
	printf("ata%d: failed to allocate driver storage\n", lun);
	return 0;
    }
    bzero(scp, sizeof(struct ata_softc));

    scp->ioaddr = ioaddr; 
    scp->altioaddr = altioaddr;
    scp->lun = lun;
    scp->unit = *unit;
    scp->active = ATA_IDLE;

    if (bootverbose)
	printf("ata%d: iobase=0x%04x altiobase=0x%04x\n", 
	       scp->lun, scp->ioaddr, scp->altioaddr);


    /* do we have any signs of ATA/ATAPI HW being present ? */
    outb(scp->ioaddr + ATA_DRIVE, ATA_D_IBM | ATA_MASTER);
    DELAY(1);
    status0 = inb(scp->ioaddr + ATA_STATUS);
    outb(scp->ioaddr + ATA_DRIVE, ATA_D_IBM | ATA_SLAVE);
    DELAY(1);	
    status1 = inb(scp->ioaddr + ATA_STATUS);
    if ((status0 & 0xf8) != 0xf8)
	mask |= 0x01;
    if ((status1 & 0xf8) != 0xf8)
	mask |= 0x02;
    if (bootverbose)
	printf("ata%d: mask=%02x status0=%02x status1=%02x\n", 
	       scp->lun, mask, status0, status1);
    if (!mask) {
	free(scp, M_DEVBUF);
	return 0;
    } 
    ata_reset(scp, &mask);
    if (!mask) {
	free(scp, M_DEVBUF);
	return 0;
    }
    /* 
     * OK, we have at least one device on the chain,
     * check for ATAPI signatures, if none check if its
     * a good old ATA device.
     */ 
    outb(scp->ioaddr + ATA_DRIVE, (ATA_D_IBM | ATA_MASTER));
    DELAY(1);
    if (inb(scp->ioaddr + ATA_CYL_LSB) == ATAPI_MAGIC_LSB &&
	inb(scp->ioaddr + ATA_CYL_MSB) == ATAPI_MAGIC_MSB) {
	scp->devices |= ATA_ATAPI_MASTER;
    }
    outb(scp->ioaddr + ATA_DRIVE, (ATA_D_IBM | ATA_SLAVE));
    DELAY(1);
    if (inb(scp->ioaddr + ATA_CYL_LSB) == ATAPI_MAGIC_LSB &&
	inb(scp->ioaddr + ATA_CYL_MSB) == ATAPI_MAGIC_MSB) {
	scp->devices |= ATA_ATAPI_SLAVE;
    }
    if (status0 != 0x00 && !(scp->devices & ATA_ATAPI_MASTER)) {
	outb(scp->ioaddr + ATA_DRIVE, (ATA_D_IBM | ATA_MASTER));
	DELAY(1);
	outb(scp->ioaddr + ATA_ERROR, 0x58);
	outb(scp->ioaddr + ATA_CYL_LSB, 0xa5);
	if (inb(scp->ioaddr + ATA_ERROR) != 0x58 &&
	    inb(scp->ioaddr + ATA_CYL_LSB) == 0xa5) {
	    scp->devices |= ATA_ATA_MASTER;
	}
    }
    if (status1 != 0x00 && !(scp->devices & ATA_ATAPI_SLAVE)) {
	outb(scp->ioaddr + ATA_DRIVE, (ATA_D_IBM | ATA_SLAVE));
	DELAY(1);
	outb(scp->ioaddr + ATA_ERROR, 0x58);
	outb(scp->ioaddr + ATA_CYL_LSB, 0xa5);
	if (inb(scp->ioaddr + ATA_ERROR) != 0x58 &&
	    inb(scp->ioaddr + ATA_CYL_LSB) == 0xa5) {
	    scp->devices |= ATA_ATA_SLAVE;
	}
    }
    if (bootverbose)
	printf("ata%d: devices = 0x%x\n", scp->lun, scp->devices);
    if (!scp->devices) {
	free(scp, M_DEVBUF);
	return 0;
    }
	 TAILQ_INIT(&scp->ata_queue);
#ifndef EXOPC
    
    TAILQ_INIT(&scp->atapi_queue);
#endif
    *unit = scp->lun;
    scp->dev = dev;
    if (bmaddr)
	scp->bmaddr = bmaddr;
    atadevices[scp->lun] = scp;

#if NAPM > 0
    scp->resume_hook.ah_fun = (void *)ata_reinit;
    scp->resume_hook.ah_arg = scp;
    scp->resume_hook.ah_name = "ATA driver";
    scp->resume_hook.ah_order = APM_MID_ORDER;
    apm_hook_establish(APM_HOOK_RESUME, &scp->resume_hook);
#endif
    return ATA_IOSIZE;
}


int
ataintr(unsigned int irq)
{
    
    struct ata_softc *scp = NULL;
#ifdef ATA_DEBUG    
    printf("IDE interrupt %d thrown\n", irq);
#endif
    if(!(scp = atadevicesbyirq[irq])) {
      printf("ata/ide driver: ERROR - interrupt for unregistered controller"); 
      return 0;
    }

    /* is this interrupt really for this channel */
    if ((scp->flags & ATA_DMA_ACTIVE) &&
	!(ata_dmastatus(scp) & ATA_BMSTAT_INTERRUPT))
	return 0;

    if (((scp->status = inb(scp->ioaddr+ATA_STATUS)) & ATA_S_BUSY)==ATA_S_BUSY) {
#ifdef ATA_DEBUG
		printf("interrupt thrown but device still busy\n");
#endif
	return 0;
	 }

    /* find & call the responsible driver to process this interrupt */
    switch (scp->active) {
#if NATADISK > 0
    case ATA_ACTIVE_ATA:
	if (!scp->running)
	    return 1;
	if (ad_interrupt(scp->running) == ATA_OP_CONTINUES)
	    return 1;
	break;
#endif
#if NATAPICD > 0 || NATAPIFD > 0 || NATAPIST > 0
    case ATA_ACTIVE_ATAPI:
	if (!scp->running)
	    return 1;
	if (atapi_interrupt(scp->running) == ATA_OP_CONTINUES)
	    return 1;
	break;
#endif
    case ATA_WAIT_INTR:
#ifndef EXOPC
      /*ECP this corresponds to tsleep*/
	wakeup((caddr_t)scp);
#endif
	break;

    case ATA_WAIT_READY:
	break;

    case ATA_REINITING:
	return 1;

    default:
    case ATA_IDLE:
#ifdef ATA_DEBUG
	{
    	    static int32_t intr_count = 0;
	    if (intr_count++ < 10)
		printf("ata%d: unwanted interrupt %d status = %02x\n", 
		       scp->lun, intr_count, scp->status);
	}
#endif
	return 1;
    }
    scp->active = ATA_IDLE;
    scp->running = NULL;
    ata_start(scp);

    return 1;
}


void
ata_start(struct ata_softc *scp)
{
    
    struct ad_request *ad_request; 
#if NATAPICD > 0 || NATAPIFD > 0 || NATAPIST > 0
    struct atapi_request *atapi_request;
#endif

#ifdef ATA_DEBUG
    printf("ata_start: entered\n");
#endif
    if (scp->active != ATA_IDLE) {
#ifdef ATA_DEBUG
	printf("scp->active != ATA_IDLE, leaving ata_start\n");	
#endif		
	return;
    }

#if NATADISK > 0
    /* find & call the responsible driver if anything on the ATA queue */

    if ((ad_request = TAILQ_FIRST(&scp->ata_queue))) {

      /* ECP : replace with exokernel request stuff*/
		TAILQ_REMOVE(&scp->ata_queue, ad_request, chain);
		scp->active = ATA_ACTIVE_ATA;
		scp->running = ad_request;
		ad_transfer(ad_request);

	

#ifdef ATA_DEBUG
		printf("ata_start: started ata, leaving\n");
#endif
		return;
    }

#endif

#if NATAPICD > 0 || NATAPIFD > 0 || NATAPIST > 0
    /*
     * find & call the responsible driver if anything on the ATAPI queue.
     * check for device busy by polling the DSC bit, if busy, check
     * for requests to the other device on the channel (if any).
     * if the other device is an ATA disk it already had its chance above.
     * if no request can be served, timeout a call to ata_start.
     */
    if ((atapi_request = TAILQ_FIRST(&scp->atapi_queue))) {
	struct atapi_softc *atp = atapi_request->device;
	static int32_t interval = 1;

	if (atp->flags & ATAPI_F_DSC_USED) {
	    outb(atp->controller->ioaddr + ATA_DRIVE, ATA_D_IBM | atp->unit);
	    DELAY(1);
	    if (!(inb(atp->controller->ioaddr + ATA_STATUS) & ATA_S_DSC)) {
		while ((atapi_request = TAILQ_NEXT(atapi_request, chain))) {
		    if (atapi_request->device->unit != atp->unit) {
			struct atapi_softc *tmp = atapi_request->device;

			outb(tmp->controller->ioaddr + ATA_DRIVE, 
			     ATA_D_IBM | tmp->unit);
			DELAY(1);
			if (!inb(tmp->controller->ioaddr+ATA_STATUS)&ATA_S_DSC)
			    atapi_request = NULL;
			break;
		    }
	        }
	    }
	    if (!atapi_request) {
		timeout((timeout_t *)ata_start, atp->controller, interval++);
		return;
	    }
	    else
		interval = 1;
	}
	TAILQ_REMOVE(&scp->atapi_queue, atapi_request, chain);
	scp->active = ATA_ACTIVE_ATAPI;
	scp->running = atapi_request;
	atapi_transfer(atapi_request);
#ifdef ATA_DEBUG
	printf("ata_start: started atapi, leaving\n");
#endif
	return;
    }
#endif

#ifdef ATA_DEBUG
	 printf("ata_start: leaving, no requests\n");
#endif
}



void
ata_reset(struct ata_softc *scp, int32_t *mask)
{
    int32_t timeout;  
    int8_t status0, status1;

    /* reset channel */
    outb(scp->ioaddr + ATA_DRIVE, ATA_D_IBM | ATA_MASTER);
    DELAY(1);
    inb(scp->ioaddr + ATA_STATUS);
    outb(scp->altioaddr, ATA_A_IDS | ATA_A_RESET);
    DELAY(10000); 
    outb(scp->altioaddr, ATA_A_IDS);
    DELAY(10000);
    inb(scp->ioaddr + ATA_ERROR);
    DELAY(3000);

    /* wait for BUSY to go inactive */
    for (timeout = 0; timeout < 310000; timeout++) {
	outb(scp->ioaddr + ATA_DRIVE, ATA_D_IBM | ATA_MASTER);
	DELAY(1);
	status0 = inb(scp->ioaddr + ATA_STATUS);
	outb(scp->ioaddr + ATA_DRIVE, ATA_D_IBM | ATA_SLAVE);
	DELAY(1);
	status1 = inb(scp->ioaddr + ATA_STATUS);
	if (*mask == 0x01)      /* wait for master only */
	    if (!(status0 & ATA_S_BUSY)) 
		break;
	if (*mask == 0x02)      /* wait for slave only */
	    if (!(status1 & ATA_S_BUSY))
		break;
	if (*mask == 0x03)      /* wait for both master & slave */
	    if (!(status0 & ATA_S_BUSY) && !(status1 & ATA_S_BUSY))
		break;
	DELAY(100);
    }	
    DELAY(1);
    outb(scp->altioaddr, ATA_A_4BIT);
    if (status0 & ATA_S_BUSY)
	*mask &= ~0x01;
    if (status1 & ATA_S_BUSY)
	*mask &= ~0x02;
    if (bootverbose)
	printf("ata%d: mask=%02x status0=%02x status1=%02x\n", 
	       scp->lun, *mask, status0, status1);
}

int32_t
ata_reinit(struct ata_softc *scp)
{
    int32_t mask = 0, omask;

    scp->active = ATA_REINITING;
    scp->running = NULL;
    printf("ata%d: resetting devices .. ", scp->lun);
    if (scp->devices & (ATA_ATA_MASTER | ATA_ATAPI_MASTER))
	mask |= 0x01;
    if (scp->devices & (ATA_ATA_SLAVE | ATA_ATAPI_SLAVE))
	mask |= 0x02;
    omask = mask;
    ata_reset(scp, &mask);
    if (omask != mask)
	printf(" device dissapeared! %d ", omask & ~mask);

#if NATADISK > 0
    if (scp->devices & (ATA_ATA_MASTER) && scp->dev_softc[0])
	ad_reinit((struct ad_softc *)scp->dev_softc[0]);
    if (scp->devices & (ATA_ATA_SLAVE) && scp->dev_softc[1])
	ad_reinit((struct ad_softc *)scp->dev_softc[1]);
#endif
#if NATAPICD > 0 || NATAPIFD > 0 || NATAPIST > 0
    if (scp->devices & (ATA_ATAPI_MASTER) && scp->dev_softc[0])
	atapi_reinit((struct atapi_softc *)scp->dev_softc[0]);
    if (scp->devices & (ATA_ATAPI_SLAVE) && scp->dev_softc[1])
	atapi_reinit((struct atapi_softc *)scp->dev_softc[1]);
#endif
    printf("done\n");
    scp->active = ATA_IDLE;
    ata_start(scp);
    return 0;
}

int32_t
ata_wait(struct ata_softc *scp, int32_t device, u_int8_t mask)
{
    u_int8_t status;
    u_int32_t timeout = 0;

    while (timeout <= 5000000) {	/* timeout 5 secs */
	status = inb(scp->ioaddr + ATA_STATUS);

	/* if drive fails status, reselect the drive just to be sure */
	if (status == 0xff) {
	    printf("ata%d: %s: no status, reselecting device\n",
		   scp->lun, device?"slave":"master");
	    outb(scp->ioaddr + ATA_DRIVE, ATA_D_IBM | device);
	    DELAY(1);
	    status = inb(scp->ioaddr + ATA_STATUS);
	}
	if (status == 0xff)
	    return -1;
	scp->status = status;
	if (!(status & ATA_S_BUSY)) {
	    if (status & ATA_S_ERROR)
		scp->error = inb(scp->ioaddr + ATA_ERROR);
	    if ((status & mask) == mask) 
		return (status & ATA_S_ERROR);
	}
	if (timeout > 1000) {
	    timeout += 1000;
	    DELAY(1000);
	}
	else {
	    timeout += 10;
	    DELAY(10);
	}
    }
    return -1;
}

int32_t
ata_command(struct ata_softc *scp, int32_t device, u_int32_t command,
	   u_int32_t cylinder, u_int32_t head, u_int32_t sector, 
	   u_int32_t count, u_int32_t feature, int32_t flags)
{
#ifdef ATA_DEBUG
    printf("ata%d: ata_command: addr=%04x, device=%02x, cmd=%02x, "
	   "c=%d, h=%d, s=%d, count=%d, flags=%02x\n",
	   scp->lun, scp->ioaddr, device, command, 
	   cylinder, head, sector, count, flags);
#endif

    /* ready to issue command ? */
    if (ata_wait(scp, device, 0) < 0) { 
		printf("ata%d-%s: timeout waiting to give command=%02x s=%02x e=%02x\n",
				 scp->lun, device ? "slave" : "master", command, 
				 scp->status, scp->error);
		return -1;
    }
    outb(scp->ioaddr + ATA_FEATURE, feature);
    outb(scp->ioaddr + ATA_CYL_LSB, cylinder);
    outb(scp->ioaddr + ATA_CYL_MSB, cylinder >> 8);
    outb(scp->ioaddr + ATA_DRIVE, ATA_D_IBM | device | head);
    outb(scp->ioaddr + ATA_SECTOR, sector);
    outb(scp->ioaddr + ATA_COUNT, count);

    switch (flags) {
    case ATA_WAIT_INTR:
#ifndef EXOPC
      /*ECP : for now just do uninterruptible wait, might want call scheduler instead*/
	if (scp->active != ATA_IDLE)
	    printf("WARNING: WAIT_INTR active=%s\n", active2str(scp->active));
	scp->active = ATA_WAIT_INTR;
	outb(scp->ioaddr + ATA_CMD, command);
	
	if (tsleep((caddr_t)scp, PRIBIO, "atacmd", 500)) {
	    printf("ata_command: timeout waiting for interrupt\n");
	    scp->active = ATA_IDLE;
	    return -1;
	}
	break;
#endif
    case ATA_WAIT_READY:
	if (scp->active != ATA_IDLE && scp->active != ATA_REINITING)
	    printf("WARNING: WAIT_READY active=%s\n", active2str(scp->active));
	scp->active = ATA_WAIT_READY;
	outb(scp->ioaddr + ATA_CMD, command);
	if (ata_wait(scp, device, ATA_S_READY) < 0) { 
	    printf("ata%d-%s: timeout waiting for command=%02x s=%02x e=%02x\n",
		   scp->lun, device ? "slave" : "master", command, 
		   scp->status, scp->error);
	    scp->active = ATA_IDLE;
	    return -1;
	}
	scp->active = ATA_IDLE;
	break;

    case ATA_IMMEDIATE:
	outb(scp->ioaddr + ATA_CMD, command);
	break;

    default:
	printf("DANGER: illegal interrupt flag=%s\n", active2str(flags));
    }
#ifdef ATA_DEBUG
    printf("ata_command: leaving\n");
#endif
    return 0;
}

int8_t *
ata_mode2str(int32_t mode)
{
    switch (mode) {
    case ATA_MODE_PIO:
	return "PIO";
    case ATA_MODE_WDMA2:
	return "DMA";
    case ATA_MODE_UDMA2:
	return "UDMA33";
    case ATA_MODE_UDMA3:
	return "UDMA3";
    case ATA_MODE_UDMA4:
	return "UDMA66";
    default:
	return "???";
    }
}

static int8_t *
active2str(int32_t active)
{
    switch (active) {
    case ATA_IDLE:
	return("ATA_IDLE");
    case ATA_WAIT_INTR:
	return("ATA_WAIT_INTR");
    case ATA_ACTIVE_ATA:
	return("ATA_ACTIVE_ATA");
    case ATA_ACTIVE_ATAPI:
	return("ATA_ACTIVE_ATAPI");
    case ATA_REINITING:
	return("ATA_REINITING");
    default:
	return("UNKNOWN");
    }
}

void
bswap(int8_t *buf, int32_t len) 
{
    u_int16_t *p = (u_int16_t*)(buf + len);

    while (--p >= (u_int16_t*)buf)
	*p = ntohs(*p);
} 

void
btrim(int8_t *buf, int32_t len)
{ 
    int8_t *p;

    for (p = buf; p < buf+len; ++p) 
	if (!*p)
	    *p = ' ';
    for (p = buf + len - 1; p >= buf && *p == ' '; --p)
	*p = 0;
}

void
bpack(int8_t *src, int8_t *dst, int32_t len)
{
    int32_t i, j, blank;

    for (i = j = blank = 0 ; i < len-1; i++) {
	if (blank && src[i] == ' ') continue;
	if (blank && src[i] != ' ') {
	    dst[j++] = src[i];
	    blank = 0;
	    continue;
	}
	if (src[i] == ' ') {
	    blank = 1;
	    if (i == 0)
		continue;
	}
	dst[j++] = src[i];
    }
    dst[j] = 0x00;
}
