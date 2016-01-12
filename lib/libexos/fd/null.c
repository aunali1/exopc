
/*
 * Copyright (C) 1997 Massachusetts Institute of Technology 
 *
 * This software is being provided by the copyright holders under the
 * following license. By obtaining, using and/or copying this software,
 * you agree that you have read, understood, and will comply with the
 * following terms and conditions:
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose and without fee or royalty is
 * hereby granted, provided that the full text of this NOTICE appears on
 * ALL copies of the software and documentation or portions thereof,
 * including modifications, that you make.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS," AND COPYRIGHT HOLDERS MAKE NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED. BY WAY OF EXAMPLE,
 * BUT NOT LIMITATION, COPYRIGHT HOLDERS MAKE NO REPRESENTATIONS OR
 * WARRANTIES OF MERCHANTABILITY OR FITNESS FOR ANY PARTICULAR PURPOSE OR
 * THAT THE USE OF THE SOFTWARE OR DOCUMENTATION WILL NOT INFRINGE ANY
 * THIRD PARTY PATENTS, COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS. COPYRIGHT
 * HOLDERS WILL BEAR NO LIABILITY FOR ANY USE OF THIS SOFTWARE OR
 * DOCUMENTATION.
 *
 * The name and trademarks of copyright holders may NOT be used in
 * advertising or publicity pertaining to the software without specific,
 * written prior permission. Title to copyright in this software and any
 * associated documentation will at all times remain with copyright
 * holders. See the file AUTHORS which should have accompanied this software
 * for a list of all copyright holders.
 *
 * This file may be derived from previously copyrighted software. This
 * copyright applies only to those changes made by the copyright
 * holders listed in the AUTHORS file. The rest of this file is covered by
 * the copyright notices, if any, listed below.
 */

#ifdef AEGIS
#include <aegis/aegis.h>
#endif /* AEGIS */
#ifdef EXOPC
#include <xok/wk.h>
#define BUFSZ
#endif /* EXOPC */

#include "fd/proc.h"
#include "errno.h"
#include <exos/debug.h>

#include <unistd.h>

int
null_init(void);

static int
null_open(struct file *dirp, struct file *filp, char *name, 
	 int flags, mode_t mode);

static int
null_read(struct file *filp, char *buffer, int nbyte, int blocking);

static int
null_write(struct file *filp, char *buffer, int nbyte, int blocking);

static int 
null_select_pred (struct file *filp, int, struct wk_term *);
  
static int 
null_select(struct file *filp, int );

static int null_close(struct file * filp);

static int 
null_stat(struct file * filp, struct stat *buf);

struct file_ops const null_file_ops = {
    null_open,		/* open */
    NULL,			/* lseek */
    null_read,		/* read */
    null_write,		/* write */
    null_select,		/* select */
    null_select_pred,		/* select_pred */
    NULL,			/* ioclt */
    NULL,			/* close0 */
    null_close,		/* close */
    NULL,			/* lookup */
    NULL,			/* link */
    NULL,		        /* symlink */
    NULL,			/* unlink */
    NULL,			/* mkdir */
    NULL,			/* rmdir */
    NULL,			/* mknod */
    NULL,			/* rename */
    NULL,		        /* readlink */
    NULL,			/* follow_link */
    NULL,			/* truncate */
    NULL,			/* dup */
    NULL,		        /* release */
    NULL,		        /* acquire */
    NULL,			/* bind */
    NULL,			/* connect */
    NULL,			/* filepair */
    NULL,			/* accept */
    NULL,			/* getname */
    NULL,			/* listen */
    NULL,			/* sendto */
    NULL, 		        /* recvfrom */
    NULL,			/* shutdown */
    NULL,			/* setsockopt */
    NULL,			/* getsockopt */
    NULL,			/* fcntl */
    NULL,			/* mount */
    NULL,			/* unmount */
    NULL,			/* chmod */
    NULL,			/* chown */
    null_stat,			/* stat */
    NULL,			/* readdir */
    NULL,			/* utimes */
    NULL,			/* bmap */
    NULL,			/* fsync */
    NULL			/* exithandler */
};

  

int
null_init(void) {
    DPRINTF(CLUHELP_LEVEL,("null_init"));
    register_file_ops((struct file_ops *)&null_file_ops, NULL_TYPE);
    return 0;
}

static int
null_open(struct file *dirp, struct file *filp, char *name, 
	 int flags, mode_t mode) {

  DPRINTF(CLU_LEVEL,("null_open: dirp: %08x, name: %s, flags: %d, mode: %d.\n",
		     (int)dirp,name,flags,mode));

  demand(filp, bogus filp);

  filp->f_dev = DEV_DEVICE;
  filp->f_ino = NULL_TYPE;
  filp->f_mode = S_IFREG && 0644;
  filp->f_pos = 0;
  filp->f_flags = flags;
  filp_refcount_init(filp);
  filp_refcount_inc(filp);
  filp->f_owner = __current->uid;
  filp->op_type = NULL_TYPE;
  return 0;
}

static int
null_read(struct file *filp, char *buffer, int nbyte, int blocking) {
  return 0;
}

static int
null_write(struct file *filp, char *buffer, int nbyte, int blocking) {
  return nbyte;
}

static int 
null_close(struct file * filp) {
  return 0;
}

static int
null_select_pred (struct file *filp, int rw, struct wk_term *t) {
  assert (0);
  return (0);
}
		
static int 
null_select(struct file *filp,int rw) {
  return 1;
}


static int 
null_stat(struct file *filp, struct stat *buf) {

    DPRINTF(CLU_LEVEL,("pty_stat:\n"));
    demand(filp, bogus filp);
    if (buf == (struct stat *) 0) {errno = EFAULT; return -1;}
    
    buf->st_dev     = DEV_DEVICE;
    buf->st_ino     = NULL_TYPE;
    buf->st_mode    = 0x2190;
    buf->st_nlink   = 1;
    buf->st_uid     = getuid();
    buf->st_gid     = getgid();
    buf->st_rdev    = NULL_TYPE;
    buf->st_size    = 0;
    buf->st_atime   = 0;
    buf->st_mtime   = 0;
    buf->st_ctime   = 0;
    buf->st_blksize = 0;
    buf->st_blocks  = 0;
    return(0);
}
