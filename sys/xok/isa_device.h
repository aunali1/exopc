#ifndef _I386_ISA_ISA_DEVICE_H_
#define	_I386_ISA_ISA_DEVICE_H_

/*-
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)isa_device.h	7.1 (Berkeley) 5/9/91
 */

struct isa_device {
	int	id_id;		/* device id */
	struct	isa_driver *id_driver;
	int	id_iobase;	/* base i/o address */
	u_int	id_irq;		/* interrupt request */
	int	id_drq;		/* DMA request */
	caddr_t id_maddr;	/* physical i/o memory address on bus (if any)*/
	int	id_msize;	/* size of i/o memory */
	void    *(id_intr);	/* interrupt interface routine */
	int	id_unit;	/* unit number */
#ifdef EXOPC
	int	id_xoknetno;	/* xoknet network interface number */
#endif
	int	id_flags;	/* flags */
	int	id_scsiid;	/* scsi id if needed */
	int	id_alive;	/* device is present */
#define	RI_FAST		1		/* fast interrupt handler */
	u_int	id_ri_flags;	/* flags for register_intr() */
	int	id_reconfig;	/* hot eject device support (such as PCMCIA) */
	int	id_enabled;	/* is device enabled */
	int	id_conflicts;	/* we're allowed to conflict with things */
	struct isa_device *id_next; /* used in isa_devlist in userconfig() */
};

#endif /* !_I386_ISA_ISA_DEVICE_H_ */
