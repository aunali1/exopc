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
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/ata/ata-disk.h,v 1.13 1999/10/09 19:57:11 sos Exp $
 */

/*Ported to exokernel by Eric Peterson, 12/99*/

/* ATA device parameter information */
struct ata_params {
    int16_t	config;				/* general configuration bits */
    u_int16_t	cylinders;			/* number of cylinders */
    int16_t	reserved2;
    u_int16_t	heads;				/* # heads */
    int16_t	unfbytespertrk;			/* # unformatted bytes/track */
    int16_t	unfbytes;			/* # unformatted bytes/sector */
    u_int16_t	sectors;			/* # sectors/track */
    int16_t	vendorunique0[3];
    int8_t	serial[20];			/* serial number */
    int16_t	buffertype;			/* buffer type */
#define ATA_BT_SINGLEPORTSECTOR		1	/* 1 port, 1 sector buffer */
#define ATA_BT_DUALPORTMULTI		2	/* 2 port, mult sector buffer */
#define ATA_BT_DUALPORTMULTICACHE	3	/* above plus track cache */

    int16_t	buffersize;			/* buf size, 512-byte units */
    int16_t	necc;				/* ecc bytes appended */
    int8_t	revision[8];			/* firmware revision */
    int8_t	model[40];			/* model name */
    int8_t	nsecperint;			/* sectors per interrupt */
    int8_t	vendorunique1;
    int16_t	usedmovsd;			/* double word read/write? */

    u_int8_t	vendorcap;			/* vendor capabilities */
    u_int8_t	dmaflag		:1;		/* DMA supported - always 1 */
    u_int8_t	lbaflag		:1;		/* LBA supported - always 1 */
    u_int8_t	iordydis	:1;		/* IORDY may be disabled */
    u_int8_t	iordyflag	:1;		/* IORDY supported */
    u_int8_t			:1;
    u_int8_t	standby		:1;		/* standby timer supported */
    u_int8_t			:1;
    u_int8_t			:1;
    int16_t	capvalidate;			/* validation for above */

    int8_t	vendorunique3;
    int8_t	opiomode;			/* PIO modes 0-2 */
    int8_t	vendorunique4;
    int8_t	odmamode;			/* old DMA modes, not ATA-3 */

    int16_t	atavalid;			/* fields valid */
#define		ATA_FLAG_54_58	      1		/* words 54-58 valid */
#define		ATA_FLAG_64_70	      2		/* words 64-70 valid */

    int16_t	currcyls;
    int16_t	currheads;
    int16_t	currsectors;
    int16_t	currsize0;
    int16_t	currsize1;
    int8_t	currmultsect;
    int8_t	multsectvalid;
    int32_t	lbasize;

    int16_t	sdmamodes;			/* singleword DMA modes */ 
    int16_t	wdmamodes;			/* multiword DMA modes */ 
    int16_t	apiomodes;			/* advanced PIO modes */ 

    u_int16_t	mwdmamin;			/* min. M/W DMA time/word ns */
    u_int16_t	mwdmarec;			/* rec. M/W DMA time ns */
    u_int16_t	pioblind;			/* min. PIO cycle w/o flow */
    u_int16_t	pioiordy;			/* min. PIO cycle IORDY flow */

    int16_t	reserved69;
    int16_t	reserved70;
    u_int16_t	rlsovlap;			/* rel time (us) for overlap */
    u_int16_t	rlsservice;			/* rel time (us) for service */
    int16_t	reserved73;
    int16_t	reserved74;
    int16_t	queuelen;
    int16_t	reserved76;
    int16_t	reserved77;
    int16_t	reserved78;
    int16_t	reserved79;
    int16_t	versmajor;
    int16_t	versminor;
    int16_t	featsupp1;
    int16_t	featsupp2;
    int16_t	featsupp3;
    int16_t	featenab1;
    int16_t	featenab2;
    int16_t	featenab3;
    int16_t	udmamodes;			/* UltraDMA modes */
    int16_t	erasetime;
    int16_t	enherasetime;
    int16_t	apmlevel;
    int16_t	masterpasswdrev;
    u_int16_t	masterhwres:8;
    u_int16_t	slavehwres:5;
    u_int16_t	cblid:1;
    u_int16_t	reserved93_1415:2;
    int16_t	reserved94[32];
    int16_t	rmvstat;
    int16_t	securstat;
    int16_t	reserved129[30];
    int16_t	cfapwrmode;
    int16_t	reserved161[84];
    int16_t	integrity;
};

/* structure describing an ATA disk */
struct ad_softc {  
    struct ata_softc		*controller;	/* ptr to parent ctrl */
    struct ata_params		*ata_parm;	/* ata device params */
    int32_t			unit;		/* ATA_MASTER or ATA_SLAVE */
    int32_t			lun;		/* logical unit number */
    u_int16_t			cylinders;	/* disk geometry (probed) */
    u_int8_t			heads;
    u_int8_t			sectors;
    u_int32_t			total_secs;	/* total # of sectors (LBA) */
    u_int32_t			transfersize;	/* size of each transfer */
    u_int32_t			num_tags;	/* number of tags supported */
    u_int32_t			flags;		/* drive flags */
#define		AD_F_LABELLING		0x0001		
#define		AD_F_LBA_ENABLED	0x0002
#define		AD_F_32B_ENABLED	0x0004
#define		AD_F_DMA_ENABLED	0x0008
#define		AD_F_TAG_ENABLED	0x0010

#ifndef EXOPC 
    struct buf_queue_head	queue;		/* head of request queue */
#else
    struct buf                  *next;          /*next request to add to ata queu*/ 
#endif

#ifndef EXOPC
    struct devstat		stats;		/* devstat entry */
    struct disk			disk;		/* disklabel/slice stuff */
#endif
};

struct ad_request {
    struct ad_softc		*device;	/* ptr to parent device */
    u_int32_t			blockaddr;	/* block number */
    u_int32_t			bytecount;	/* bytes to transfer */
    u_int32_t			donecount;	/* bytes transferred */
    u_int32_t			currentsize;	/* size of current transfer */
#ifndef EXOPC
    struct callout_handle       timeout_handle; /* handle for untimeout */ 
#endif
    int32_t			retries;	/* retry count */
    int32_t			flags;
#define		AR_F_READ		0x0001
#define		AR_F_ERROR		0x0002
#define		AR_F_DMA_USED		0x0004

    int8_t			*data;		/* pointer to data buf */
    struct buf			*bp;		/* associated buf ptr */
    struct buf                  *last;          /* last serviced in s/g */
    int                         offset;         /* offset for new i/o in s/g*/
    u_int8_t			tag;		/* tag ID of this request */
    TAILQ_ENTRY(ad_request)	chain;		/* list management */
};

void ad_transfer(struct ad_request *);
int32_t ad_interrupt(struct ad_request *);
void ad_reinit(struct ad_softc *);
