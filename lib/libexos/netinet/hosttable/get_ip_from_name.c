
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

#include <assert.h>
#include <string.h>
#include <exos/debug.h>

#include <exos/netinet/hosttable.h>

/* get_ip_from_name returns a pointer to a four character array containing
 * the IP address for that name.  if the pointer is null it means that no
 * address was found. */
char *get_ip_from_name(const char *name)
{
  char *name_tmp;
  int len;
  map_t *m = hosttable_map;

  /* finds the length of the first name until the first '.' or NULL */
  if ((name_tmp = strchr(name,'.'))) {
    len = name_tmp - name;
  } else {
    len = strlen(name);
  }
  
  while(m->name[0] != 0) {
    DPRINTF(SYSHELP_LEVEL,("looking at name: %s\n",m->name));
    if(!strncasecmp(name, m->name,len)) {
      DPRINTF(SYSHELP_LEVEL,("returning: %s\n",m->name));
      return(m->ip_addr);
    }
    m++;
  }
  DPRINTF(SYSHELP_LEVEL,("returning null pointer\n"));
  return (char *)0;
}
