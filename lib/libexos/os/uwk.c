
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


/********************** High-level design of uwk support **********************/
/*

The uwk.c support includes two main components:

1. Simple functions for handling everything involved with waiting for
   trivial events.  For example, wk_waitfor_value(...) waits for a
   specified memory location to contain a specified value.  Wherever
   reasonable, functions like these should be used in place of
   private WK predicate construction.

2. A (bypassable, of course) level of indirection between other libOS
   code and the kernel's WK predicate support.  All libOS code should
   call these wrappers (e.g., wk_waitfor_pred) rather than downloading
   the predicates themselves and manipulating process state.  All extra
   predicate components will be added to the specified predicate.
   When control returns, the predicate will have been satisfied.

All desire to wait for WK predicates to evaluate to true should use one
of these two components rather than doing the work itself.

Clearly, #2 is the big one.  Several things are glossed over in the above
high-level description.  In particular, what the interfaces to this
code look like and what the "extra predicate components" are.

uwk.c currently provides two main interfaces for arbitrary predicates:
  1. wk_waitfor_pred (wk_term *t, int sz), which takes a predicate and
     waits for it to be true.  This will be implemented as a #define
     wrapper for the following interface.
  2. wk_waitfor_pred_directed (wk_term *t, int sz, int envid), which takes
     a predicate and waits for it to be true, but specifically yields to
     a specific environment rather than just freeing the CPU for anyone.

Registering extra predicate components
--------------------------------------

Rather than hard-coding the extra predicate components that should be
included in all predicates (e.g., waking up when signals received),
I propose to have a clean simple mechanism for registering such
components.  The interface will look like:

   wk_register_pred (construct, callback, param, maxlen)

where:
   "construct" is a function that builds (in place) the relevant extra
      predicate component.  This will be called each time a predicate is
      to be downloaded.
   "callback" is a function that will be called if the process was awakened
      due to this predicate component.
   "param" is a 64-bit value that will be passed to both "construct" and
      "callback".
   "maxlen" is the maximum length of any predicate constructed by "construct"

Control flow (source files are os/uwk.h and os/uwk.c)
-----------------------------------------------------

1. Needing to wait for an event, a libOS component calls "wk_waitfor_pred"
   with its personal predicate.

2. A large enough (for the specified predicate plus the maximum sizes of
   all registered extras) wk_term array is malloc'd, and the specified
   predicate is copied to the front of it.

3. For each registered extra, a "WK_OR" is added, a "WK_TAG" is added and
   the "construct" routine is called to build the registered predicate.
   The tag value is taken from a range dedicated to os/uwk.c.  (Note: if
   a libOS re-uses these tag values, unpleasant things will happen.  It
   might be appropriate to replace "WK_TAG" values with indices into the
   down-loaded wk_term array, which would be unambiguous.)

4. The resulting predicate is downloaded and the process sleeps until
   one of the predicate components matches.  If the resultant tag value
   identifies one of the registered extras (rather than the original libOS
   predicate), than the appropriate callback routine is executed, the
   malloc'd region is freed, and the process returns to step #2.  Otherwise,
   the malloc'd region is freed and control returns to the original caller
   (see step #1).  (Note: this will work because predicate components are
   evaluated in order, stopping with the first positive component.)

*/
/****************************************************************************/


#include <xok/sys_ucall.h>
#include <xok/wk.h>
#include <exos/uwk.h>
#include <xok/sysinfo.h>
#include <exos/process.h>
#include <exos/cap.h>
#include <exos/mallocs.h>
#include <xok/env.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <exos/osdecl.h>


/* space for constructing the super-predicate (i.e., one that contains */
/* both the specified conditions and the pre-registered extras).       */

#define DEFAULT_MALLOCED_WKLEN	16
static int superlen = DEFAULT_MALLOCED_WKLEN;
static struct wk_term static_wkarray[DEFAULT_MALLOCED_WKLEN];
static struct wk_term *wksuper = static_wkarray;

/* information about the registered extra conditions */

typedef struct {
   int (*construct)(struct wk_term *t, u_quad_t param);
   void (*callback)(u_quad_t param);
   u_quad_t param;
   int maxlen;
} wk_extra_t;

#define MAX_EXTRAS	16
static wk_extra_t wk_extras[MAX_EXTRAS];	/* assuming zero'd at exec */
static int wk_extra_maxlen = UWK_MKSIG_PRED_SIZE + 2 + UWK_MKREV_PRED_SIZE + 2; /* for OR and TAG */


/* the main function.  this constructs a larger predicate (including both  */
/* the specified wk_terms and the registered extras), and then yields the  */
/* time slice to the specified envid.  If one of the registered extras     */
/* causes the predicate to evaluate to TRUE, then the associated call-back */
/* will be done and the core of this function repeats.                     */

void wk_waitfor_pred_directed (struct wk_term *t, int sz, int envid)
{
   int ret;
   int origsz = sz;
   int needsz = sz + wk_extra_maxlen;
   int i;

   assert (sz != 0);
   //kprintf ("%d: wk_waitfor_perd_directed: sz %d (needsz %d, superlen %d), envid %d\n", geteid(), sz, needsz, superlen, envid);

   if (needsz > superlen) {
     //kprintf ("eid %d, sz %d, needsz %d, superlen %d\n", geteid(), sz, needsz, superlen);
     if (wksuper != static_wkarray) { __free(wksuper); }
     wksuper = __malloc (needsz * sizeof(struct wk_term));
     assert (wksuper != NULL);
     superlen = needsz;
   }

   memcpy (wksuper, t, (sz * sizeof(struct wk_term)));

wait_again:

   for (i=0; i<MAX_EXTRAS; i++) {
      if (wk_extras[i].construct) {
         int tmpsz = sz;
         tmpsz = wk_mkop (tmpsz, wksuper, WK_OR);
         tmpsz = wk_mktag (tmpsz, wksuper, (UWK_TAG_MIN+i));
         ret = wk_extras[i].construct (&wksuper[tmpsz], wk_extras[i].param);
         if (ret > wk_extras[i].maxlen) {
            kprintf ("wk_waitfor_pred: extra predicate exceeds pre-specified"
		     " maxlen (%d > %d)\n", ret, wk_extras[i].maxlen);
            kprintf ("more info: construct %p, callback %p, param %qu\n",
		     wk_extras[i].construct, wk_extras[i].callback,
		     wk_extras[i].param);
            assert (0);
         } else if (ret > 0) {
            sz = tmpsz + ret;
         }
      }
   }

   /* if you add on more terms here, make sure you updated wk_extra_maxlen
      above too */

   /* add signals to the super wake-up predicate */
   sz = wk_mkop (sz, wksuper, WK_OR);
   sz = wk_mktag (sz, wksuper, UWK_TAG_MAX);
   sz += wk_mksig_pred (&wksuper[sz]);

#if 1
   /* add page revocation requests */
   sz = wk_mkop (sz, wksuper, WK_OR);
   sz = wk_mktag (sz, wksuper, UWK_TAG_MAX);
   sz += wk_mkrev_pred (&wksuper[sz]);
#endif

   /* now download the predicate */
   if ((ret = sys_wkpred (wksuper, sz)) < 0) {
      kprintf ("wk_waitfor_pred_directed: unable to download wkpred (ret %d, sz %d)\n", ret, sz);
      assert ("wk_waitfor_pred_directed setup failed" == 0);
   }

   UAREA.u_status = U_SLEEP;

   /* GROK -- should we repeat the directed yield or do -1 after the */
   /*         first time??                                           */
   yield (envid);

   /* we woke up, so let's figure out why */
   if ((UAREA.u_pred_tag >= UWK_TAG_MIN) && (UAREA.u_pred_tag <= UWK_TAG_MAX)) {
     i = UAREA.u_pred_tag - UWK_TAG_MIN;
     if (UAREA.u_pred_tag == UWK_TAG_MAX) {
        /* must be awakened because of signal, so no callback to call */

	/* could have been deregistered in prolog -- just let it go, if so */
     } else if (wk_extras[i].construct && wk_extras[i].callback) {
        wk_extras[i].callback (wk_extras[i].param);
     }
     sz = origsz;
     goto wait_again;
   }

#if 1
   /* this is just to get rid of the predicate, so it won't accidentally */
   /* get used in a subsequent non-predicated yield.                     */
   if ((ret = sys_wkpred (NULL, 0)) < 0) {
      kprintf ("wk_waitfor_pred_directed: unable to eliminate wkpred"
	       " (ret %d)\n", ret);
      assert ("wk_waitfor_pred_directed cleanup failed" == 0);
   }
#endif
}


int wk_register_extra (int (*construct)(struct wk_term *,u_quad_t),
		       void (*callback)(u_quad_t), u_quad_t param, int maxlen)
{
   int i;

   assert (maxlen > 0);
   assert (construct != NULL);

   for (i=0; i<MAX_EXTRAS; i++) {
     if (wk_extras[i].construct == NULL) {
       wk_extras[i].construct = construct;
       wk_extras[i].callback = callback;
       wk_extras[i].param = param;
       wk_extras[i].maxlen = maxlen;
       wk_extra_maxlen += maxlen + 2;		/* pred plus WK_OR */
       return (i);
     }
   }
   kprintf ("wk_register_extra: failed due to lack of space in wk_extras\n");
   return (-1);
}


int wk_deregister_extra (int (*construct)(struct wk_term *,u_quad_t),
			 u_quad_t param)
{
   int i;

   for (i=0; i<MAX_EXTRAS; i++) {
     if ((wk_extras[i].construct == construct) &&
	 (wk_extras[i].param == param)) {
       wk_extras[i].construct = NULL;
       wk_extra_maxlen -= wk_extras[i].maxlen + 2;
       return (0);
     }
   }
   kprintf ("wk_deregister_extra: extra not found (construct %p, param %qu)\n",
	    construct, param);
   return (-1);
}


/* misc user-level functions for dealing with the wk predicates */

void wk_waitfor_value (int *addr, int value, u_int cap)
{
   struct wk_term t[UWK_MKCMP_EQ_PRED_SIZE];
   int sz = 0;

   sz = wk_mkcmp_eq_pred (&t[0], addr, value, cap);
   assert (sz == UWK_MKCMP_EQ_PRED_SIZE);

   while (*addr != value) {
      wk_waitfor_pred (t, sz);
   }
}


void wk_waitfor_value_neq (int *addr, int value, u_int cap)
{
   struct wk_term t[UWK_MKCMP_NEQ_PRED_SIZE];
   int sz = 0;

   sz = wk_mkcmp_neq_pred (&t[0], addr, value, cap);
   assert (sz == UWK_MKCMP_NEQ_PRED_SIZE);

   while (*addr == value) {
      wk_waitfor_pred (t, sz);
   }
}


void wk_waitfor_value_lt (int *addr, int value, u_int cap)
{
   struct wk_term t[3];
   int sz = 0;

   sz = wk_mkvar (sz, t, wk_phys (addr), cap);
   sz = wk_mkimm (sz, t, value);
   sz = wk_mkop (sz, t, WK_LT);

   while (*addr >= value) {
      wk_waitfor_pred (t, sz);
   }
}


void wk_waitfor_value_gt (int *addr, int value, u_int cap)
{
   struct wk_term t[3];
   int sz = 0;

   sz = wk_mkvar (sz, t, wk_phys (addr), cap);
   sz = wk_mkimm (sz, t, value);
   sz = wk_mkop (sz, t, WK_GT);

   while (*addr <= value) {
      wk_waitfor_pred (t, sz);
   }
}


/* construct a predicate term that is true when *addr == value */
int wk_mkcmp_eq_pred (struct wk_term *t, int *addr, int value, u_int cap)
{
   int sz = 0;
   sz = wk_mkvar (sz, t, wk_phys (addr), cap);
   sz = wk_mkimm (sz, t, value);
   sz = wk_mkop (sz, t, WK_EQ);
   return sz;
}

/* construct a predicate term that is true when *addr != value */
int wk_mkcmp_neq_pred (struct wk_term *t, int *addr, int value, u_int cap)
{
   int sz = 0;
   sz = wk_mkvar (sz, t, wk_phys (addr), cap);
   sz = wk_mkimm (sz, t, value);
   sz = wk_mkop (sz, t, WK_NEQ);
   return sz;
}

/* contstruct a predicate term that is true when *addr > value */
int wk_mkcmp_gt_pred (struct wk_term *t, int *addr, int value, u_int cap) {
  int sz = 0;

  sz = wk_mkvar (sz, t, wk_phys (addr), cap);
  sz = wk_mkimm (sz, t, value);
  sz = wk_mkop (sz, t, WK_GT);
  return sz;
}

/* construct a predicate term that always evaluates to false */
int wk_mkfalse_pred (struct wk_term *t) 
{
  int sz = 0;
  sz = wk_mkimm (sz, t, 0);
  sz = wk_mkimm (sz, t, 0);
  sz = wk_mkop (sz, t, WK_NEQ);
  return sz;
}


/* construct a predicate term that always evaluates to true */
int wk_mktrue_pred (struct wk_term *t) 
{
  int sz = 0;
  sz = wk_mkimm (sz, t, 0);
  sz = wk_mkimm (sz, t, 0);
  sz = wk_mkop (sz, t, WK_EQ);
  return sz;
}


/* has to split up ticks into longs for the wk_pred code */
int wk_mksleep_pred (struct wk_term *t, u_quad_t ticks)
{
   int sz = 0;

   /* if low bits greater */
   sz = wk_mkvar (sz, t, wk_phys (&__sysinfo.si_system_ticks), CAP_ROOT);
   sz = wk_mkimm (sz, t, (u32)ticks);
   sz = wk_mkop (sz, t, WK_GT);
   /* and high bits the same */
   sz = wk_mkvar (sz, t, wk_phys (&((u32*)&__sysinfo.si_system_ticks)[1]), CAP_ROOT);
   sz = wk_mkimm (sz, t, ((u32*)&ticks)[1]);
   sz = wk_mkop (sz, t, WK_EQ);
   /* or high bits greater */
   sz = wk_mkop (sz, t, WK_OR);
   sz = wk_mkvar (sz, t, wk_phys (&((u32*)&__sysinfo.si_system_ticks)[1]), CAP_ROOT);
   sz = wk_mkimm (sz, t, ((u32*)&ticks)[1]);
   sz = wk_mkop (sz, t, WK_GT);

   assert (sz <= UWK_MKSLEEP_PRED_SIZE);

   return (sz);
}


/* has to split up value into longs for the wk_pred code */
int wk_mkenvdeath_pred (struct wk_term *t)
{
   int sz = 0;
   u_quad_t count = __sysinfo.si_killed_envs;

   /* if low bits greater */
   sz = wk_mkvar (sz, t, wk_phys (&__sysinfo.si_killed_envs), CAP_ROOT);
   sz = wk_mkimm (sz, t, (u32)count);
   sz = wk_mkop (sz, t, WK_GT);
   /* and high bits the same */
   sz = wk_mkvar (sz, t, wk_phys (&((u32*)&__sysinfo.si_killed_envs)[1]),
		  CAP_ROOT);
   sz = wk_mkimm (sz, t, ((u32*)&count)[1]);
   sz = wk_mkop (sz, t, WK_EQ);
   /* or high bits greater */
   sz = wk_mkop (sz, t, WK_OR);
   sz = wk_mkvar (sz, t, wk_phys (&((u32*)&__sysinfo.si_killed_envs)[1]),
		  CAP_ROOT);
   sz = wk_mkimm (sz, t, ((u32*)&count)[1]);
   sz = wk_mkop (sz, t, WK_GT);

   assert (sz <= UWK_MKENVDEATH_PRED_SIZE);

   return (sz);
}


int wk_mkusleep_pred (struct wk_term *t, u_int usecs) {
  int sz = 0;
   unsigned long long ticks = ((usecs + __sysinfo.si_rate - 1)
			       / __sysinfo.si_rate) + __sysinfo.si_system_ticks;
   sz = wk_mksleep_pred (t, ticks);
   return (sz);
}


void wk_waitfor_usecs (u_int usecs)
{
   struct wk_term t[UWK_MKSLEEP_PRED_SIZE];
   int sz = 0;

   sz = wk_mkusleep_pred (t, usecs);
   wk_waitfor_pred (t, sz);
}

/* add a term that is true when the kernel signals us to return pages */
int wk_mkrev_pred (struct wk_term *t) {
  return (wk_mkcmp_gt_pred (t, &UAREA.u_revoked_pages, 0, CAP_ROOT));
}

void wk_waitfor_freepages ()
{
   struct wk_term t[10];
   int sz = 0;

   /* GROK -- this is very inexact.  I don't know what the right answer is!   */
   /* So, this just waits for any of (1) the number of free pages to increase,*/
   /* (2) the number of dirty pages to decrease, or (3) the number of pinned  */
   /* pages to decrease.  Any of these can have the effect of making more     */
   /* allocatable pages...                                                    */

   if (__sysinfo.si_nfreepages <= (__sysinfo.si_ndpages + __sysinfo.si_pinnedpages)) {
      u_int ndpages = __sysinfo.si_ndpages;
      u_int freepages = __sysinfo.si_nfreepages;
      u_int pinnedpages = __sysinfo.si_pinnedpages;
      sz = wk_mkvar (sz, t, wk_phys (&__sysinfo.si_nfreepages), CAP_ROOT);
      sz = wk_mkimm (sz, t, freepages);
      sz = wk_mkop (sz, t, WK_GT);
      sz = wk_mkop (sz, t, WK_OR);
      sz = wk_mkvar (sz, t, wk_phys (&__sysinfo.si_ndpages), CAP_ROOT);
      sz = wk_mkimm (sz, t, ndpages);
      sz = wk_mkop (sz, t, WK_LT);
      sz = wk_mkop (sz, t, WK_OR);
      sz = wk_mkvar (sz, t, wk_phys (&__sysinfo.si_pinnedpages), CAP_ROOT);
      sz = wk_mkimm (sz, t, pinnedpages);
      sz = wk_mkop (sz, t, WK_LT);
      wk_waitfor_pred (t, sz);
   }
}


void wk_dump(struct wk_term* t, int sz) {
  if (!sz)
    return;

  switch (t[0].wk_type) {
  case WK_VAR:
    printf("WK_VAR %#x %d\n", (u_int)t[0].wk_var, t[0].wk_cap);
    break;
  case WK_IMM:
    printf("WK_IMM %d\n", t[0].wk_imm);
    break;
  case WK_TAG:
    printf("WK_TAG %d\n", t[0].wk_tag);
    break;
  case WK_OP:
    switch (t[0].wk_op) {
    case WK_GT:  printf("WK_GT\n");  break;
    case WK_GTE: printf("WK_GTE\n"); break;
    case WK_LT:  printf("WK_LT\n");  break;
    case WK_LTE: printf("WK_LTE\n"); break;
    case WK_EQ:  printf("WK_EQ\n");  break;
    case WK_NEQ: printf("WK_NEQ\n"); break;
    case WK_OR:  printf("WK_OR\n");  break;
    default:
      printf("WK_OP <invalid>");
      break;
    }
    break;
  default:
    printf("<invalid (%d)>\n", t[0].wk_type);
  }

  wk_dump(&t[1], sz-1);
}

