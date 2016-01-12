/*
 * tc.prompt.c: Prompt printing stuff
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
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
 */
#include "sh.h"


#include "ed.h"

/*
 * kfk 21oct1983 -- add @ (time) and / ($cwd) in prompt.
 * PWP 4/27/87 -- rearange for tcsh.
 * mrdch@com.tau.edu.il 6/26/89 - added ~, T and .# - rearanged to switch()
 *                 instead of if/elseif
 * Luke Mewburn, <lm@rmit.edu.au>:  6-Sep-91 - changed date format,
 *	   16-Feb-94 - rewrote directory prompt code, added $ellipsis
 */

static char   *month_list[12];
static char   *day_list[7];

void
dateinit()
{
#ifdef notyet
  int i;

  setlocale(LC_TIME, "");

  for (i = 0; i < 12; i++)
      xfree((ptr_t) month_list[i]);
  month_list[0] = strsave(_time_info->abbrev_month[0]);
  month_list[1] = strsave(_time_info->abbrev_month[1]);
  month_list[2] = strsave(_time_info->abbrev_month[2]);
  month_list[3] = strsave(_time_info->abbrev_month[3]);
  month_list[4] = strsave(_time_info->abbrev_month[4]);
  month_list[5] = strsave(_time_info->abbrev_month[5]);
  month_list[6] = strsave(_time_info->abbrev_month[6]);
  month_list[7] = strsave(_time_info->abbrev_month[7]);
  month_list[8] = strsave(_time_info->abbrev_month[8]);
  month_list[9] = strsave(_time_info->abbrev_month[9]);
  month_list[10] = strsave(_time_info->abbrev_month[10]);
  month_list[11] = strsave(_time_info->abbrev_month[11]);

  for (i = 0; i < 7; i++)
      xfree((ptr_t) day_list[i]);
  day_list[0] = strsave(_time_info->abbrev_wkday[0]);
  day_list[1] = strsave(_time_info->abbrev_wkday[1]);
  day_list[2] = strsave(_time_info->abbrev_wkday[2]);
  day_list[3] = strsave(_time_info->abbrev_wkday[3]);
  day_list[4] = strsave(_time_info->abbrev_wkday[4]);
  day_list[5] = strsave(_time_info->abbrev_wkday[5]);
  day_list[6] = strsave(_time_info->abbrev_wkday[6]);
#else
  month_list[0] = "Jan";
  month_list[1] = "Feb";
  month_list[2] = "Mar";
  month_list[3] = "Apr";
  month_list[4] = "May";
  month_list[5] = "Jun";
  month_list[6] = "Jul";
  month_list[7] = "Aug";
  month_list[8] = "Sep";
  month_list[9] = "Oct";
  month_list[10] = "Nov";
  month_list[11] = "Dec";

  day_list[0] = "Sun";
  day_list[1] = "Mon";
  day_list[2] = "Tue";
  day_list[3] = "Wed";
  day_list[4] = "Thu";
  day_list[5] = "Fri";
  day_list[6] = "Sat";
#endif
}

void
printprompt(promptno, str)
    int     promptno;
    char   *str;
{
    static  Char *ocp = NULL;
    static  char *ostr = NULL;
    time_t  lclock = time(NULL);
    Char   *cp;

    switch (promptno) {
    default:
    case 0:
	cp = varval(STRprompt);
	break;
    case 1:
	cp = varval(STRprompt2);
	break;
    case 2:
	cp = varval(STRprompt3);
	break;
    case 3:
	if (ocp != NULL) {
	    cp = ocp;
	    str = ostr;
	}
	else 
	    cp = varval(STRprompt);
	break;
    }

    if (promptno < 2) {
	ocp = cp;
	ostr = str;
    }

    PromptBuf[0] = '\0';
    tprintf(FMT_PROMPT, PromptBuf, cp, 2 * INBUFSIZE - 2, str, lclock, NULL);

    if (!editing) {
	for (cp = PromptBuf; *cp ; )
	    (void) putraw(*cp++);
	SetAttributes(0);
	flush();
    }
}

void
tprintf(what, buf, fmt, siz, str, tim, info)
    int what;
    Char *buf, *fmt;
    size_t siz;
    char *str;
    time_t tim;
    ptr_t info;
{
    Char   *z, *q;
    Char    attributes = 0;
    static int print_prompt_did_ding = 0;
    const char   *cz;
    Char    buff[BUFSIZE];
    char    cbuff[BUFSIZE];

    Char *p  = buf;
    Char *ep = &p[siz];
    Char *cp = fmt, Scp;
    struct tm *t = localtime(&tim);

			/* prompt stuff */
    static Char *olddir = NULL, *olduser = NULL;
    extern int tlength;	/* cache cleared */
    register int updirs;
    int pdirs;

    for (; *cp; cp++) {
	if (p >= ep)
	    break;
	if (*cp == '%') {
	    cp++;
	    switch (*cp) {
	    case 'R':
		if (what == FMT_HISTORY)
		    fmthist('R', info, str = cbuff);
		if (str != NULL)
		    for (; *str; *p++ = attributes | *str++)
			if (p >= ep) break;
		break;
	    case '#':
		*p++ = attributes | ((uid == 0) ? PRCHROOT : PRCH);
		break;
	    case '!':
	    case 'h':
		switch (what) {
		case FMT_HISTORY:
		    fmthist('h', info, cbuff);
		    break;
		case FMT_SCHED:
		    (void) xsprintf(cbuff, "%d", *(int *)info);
		    break;
		default:
		    (void) xsprintf(cbuff, "%d", eventno + 1);
		    break;
		}
		for (cz = cbuff; *cz; *p++ = attributes | *cz++)
		    if (p >= ep) break;
		break;
	    case 'T':		/* 24 hour format	 */
	    case '@':
	    case 't':		/* 12 hour am/pm format */
	    case 'p':		/* With seconds	*/
	    case 'P':
		{
		    char    ampm = 'a';
		    int     hr = t->tm_hour;

		    if (p >= ep - 10) break;

		    /* addition by Hans J. Albertsson */
		    /* and another adapted from Justin Bur */
		    if (adrof(STRampm) || (*cp != 'T' && *cp != 'P')) {
			if (hr >= 12) {
			    if (hr > 12)
				hr -= 12;
			    ampm = 'p';
			}
			else if (hr == 0)
			    hr = 12;
		    }		/* else do a 24 hour clock */

		    /* "DING!" stuff by Hans also */
		    if (t->tm_min || print_prompt_did_ding || 
			what != FMT_PROMPT || adrof(STRnoding)) {
			if (t->tm_min)
			    print_prompt_did_ding = 0;
			Itoa(hr, buff);
			*p++ = attributes | buff[0];
			if (buff[1]) 
			    *p++ = attributes | buff[1];
			*p++ = attributes | ':';
			Itoa(t->tm_min, buff);
			if (buff[1]) {
			    *p++ = attributes | buff[0];
			    *p++ = attributes | buff[1];
			}
			else {
			    *p++ = attributes | '0';
			    *p++ = attributes | buff[0];
			}
			if (*cp == 'p' || *cp == 'P') {
			    *p++ = attributes | ':';
			    Itoa(t->tm_sec, buff);
			    if (buff[1]) {
				*p++ = attributes | buff[0];
				*p++ = attributes | buff[1];
			    }
			    else {
				*p++ = attributes | '0';
				*p++ = attributes | buff[0];
			    }
			}
			if (adrof(STRampm) || (*cp != 'T' && *cp != 'P')) {
			    *p++ = attributes | ampm;
			    *p++ = attributes | 'm';
			}
		    }
		    else {	/* we need to ding */
			int     i = 0;

			(void) Strcpy(buff, STRDING);
			while (buff[i]) {
			    *p++ = attributes | buff[i++];
			}
			print_prompt_did_ding = 1;
		    }
		}
		break;

	    case 'M':
#ifndef HAVENOUTMP
		if (what == FMT_WHO)
		    cz = who_info(info, 'M', cbuff);
		else 
#endif /* HAVENOUTMP */
		    cz = getenv("HOST");
		/*
		 * Bug pointed out by Laurent Dami <dami@cui.unige.ch>: don't
		 * derefrence that NULL (if HOST is not set)...
		 */
		if (cz != NULL)
		    for (; *cz ; *p++ = attributes | *cz++)
			if (p >= ep) break;
		break;

	    case 'm':
#ifndef HAVENOUTMP
		if (what == FMT_WHO)
		    cz = who_info(info, 'm', cbuff);
		else 
#endif /* HAVENOUTMP */
		    cz = getenv("HOST");

		if (cz != NULL)
		    for ( ; *cz && (what == FMT_WHO || *cz != '.')
			  ; *p++ = attributes | *cz++ )
			if (p >= ep) break;
		break;

			/* lm: new directory prompt code */
	    case '~':
	    case '/':
	    case '.':
	    case 'c':
	    case 'C':
		Scp = *cp;
		if (Scp == 'c')		/* store format type (c == .) */
		    Scp = '.';
		if ((z = varval(STRcwd)) == STRNULL)
		    break;		/* no cwd, so don't do anything */

			/* show ~ whenever possible - a la dirs */
		if (Scp == '~' || Scp == '.' ) {
		    if (tlength == 0 || olddir != z) {
			olddir = z;		/* have we changed dir? */
			olduser = getusername(&olddir);
		    }
		    if (olduser)
			z = olddir;
		}
		updirs = pdirs = 0;

			/* option to determine fixed # of dirs from path */
		if (Scp == '.' || Scp == 'C') {
		    int skip;

		    q = z;
		    while (*z)				/* calc # of /'s */
			if (*z++ == '/')
			    updirs++;
		    if ((Scp == 'C' && *q != '/'))
			updirs++;

		    if (cp[1] == '0') {			/* print <x> or ...  */
			pdirs = 1;
			cp++;
		    }
		    if (cp[1] >= '1' && cp[1] <= '9') {	/* calc # to skip  */
			skip = cp[1] - '0';
			cp++;
		    }
		    else
			skip = 1;

		    updirs -= skip;
		    while (skip-- > 0) {
			while ((z > q) && (*z != '/'))
			    z--;			/* back up */
			if (skip && z > q)
			    z--;
		    }
		    if (*z == '/' && z != q)
			z++;
		} /* . || C */

							/* print ~[user] */
		if ((olduser) && ((Scp == '~') ||
		     (Scp == '.' && (pdirs || (!pdirs && updirs <= 0))) )) {
		    *p++ = attributes | '~';
		    if (p >= ep) break;
		    for (q = olduser; *q; *p++ = attributes | *q++)
			if (p >= ep) break;
		}

			/* RWM - tell you how many dirs we've ignored */
			/*       and add '/' at front of this         */
		if (updirs > 0 && pdirs) {
		    if (p >= ep - 5) break;
		    if (adrof(STRellipsis)) {
			*p++ = attributes | '.';
			*p++ = attributes | '.';
			*p++ = attributes | '.';
		    } else {
			*p++ = attributes | '/';
			*p++ = attributes | '<';
			if (updirs > 9) {
			    *p++ = attributes | '9';
			    *p++ = attributes | '+';
			} else
			    *p++ = attributes | ('0' + updirs);
			*p++ = attributes | tcsh ? '>' : '%';
		    }
		}
		
		for (; *z ; *p++ = attributes | *z++)
		    if (p >= ep) break;
		break;
			/* lm: end of new directory prompt code */

	    case 'n':
#ifndef HAVENOUTMP
		if (what == FMT_WHO) {
		    if ((cz = who_info(info, 'n', cbuff)) != NULL)
			for (; *cz ; *p++ = attributes | *cz++)
			    if (p >= ep) break;
		}
		else  
#endif /* HAVENOUTMP */
		{
		    if ((z = varval(STRuser)) != STRNULL)
			for (; *z; *p++ = attributes | *z++)
			    if (p >= ep) break;
		}
		break;
	    case 'l':
#ifndef HAVENOUTMP
		if (what == FMT_WHO) {
		    if ((cz = who_info(info, 'l', cbuff)) != NULL)
			for (; *cz ; *p++ = attributes | *cz++)
			    if (p >= ep) break;
		}
		else  
#endif /* HAVENOUTMP */
		{
		    if ((z = varval(STRtty)) != STRNULL)
			for (; *z; *p++ = attributes | *z++)
			    if (p >= ep) break;
		}
		break;
	    case 'd':
		for (cz = day_list[t->tm_wday]; *cz; *p++ = attributes | *cz++)
		    if (p >= ep) break;
		break;
	    case 'D':
		Itoa(t->tm_mday, buff);
		if (p >= ep - 3) break;
		if (buff[1]) {
		    *p++ = attributes | buff[0];
		    *p++ = attributes | buff[1];
		}
		else {
		    *p++ = attributes | '0';
		    *p++ = attributes | buff[0];
		}
		break;
	    case 'w':
		if (p >= ep - 5) break;
		for (cz = month_list[t->tm_mon]; *cz;)
		    *p++ = attributes | *cz++;
		break;
	    case 'W':
		if (p >= ep - 3) break;
		Itoa(t->tm_mon + 1, buff);
		if (buff[1]) {
		    *p++ = attributes | buff[0];
		    *p++ = attributes | buff[1];
		}
		else {
		    *p++ = attributes | '0';
		    *p++ = attributes | buff[0];
		}
		break;
	    case 'y':
		if (p >= ep - 3) break;
		Itoa(t->tm_year, buff);
		if (buff[1]) {
		    *p++ = attributes | buff[0];
		    *p++ = attributes | buff[1];
		}
		else {
		    *p++ = attributes | '0';
		    *p++ = attributes | buff[0];
		}
		break;
	    case 'Y':
		if (p >= ep - 5) break;
		Itoa(t->tm_year + 1900, buff);
		*p++ = attributes | buff[0];
		*p++ = attributes | buff[1];
		*p++ = attributes | buff[2];
		*p++ = attributes | buff[3];
		break;
	    case 'S':		/* start standout */
		attributes |= STANDOUT;
		break;
	    case 'B':		/* start bold */
		attributes |= BOLD;
		break;
	    case 'U':		/* start underline */
		attributes |= UNDER;
		break;
	    case 's':		/* end standout */
		attributes &= ~STANDOUT;
		break;
	    case 'b':		/* end bold */
		attributes &= ~BOLD;
		break;
	    case 'u':		/* end underline */
		attributes &= ~UNDER;
		break;
	    case 'L':
		ClearToBottom();
		break;
	    case '?':
		if ((z = varval(STRstatus)) != STRNULL)
		    for (; *z; *p++ = attributes | *z++)
			if (p >= ep) break;
		break;
	    case '%':
		*p++ = attributes | '%';
		break;
	    case '{':		/* literal characters start */
#if LITERAL == 0
		/*
		 * No literal capability, so skip all chars in the literal
		 * string
		 */
		while (*cp != '\0' && (*cp != '%' || cp[1] != '}'))
		    cp++;
#endif				/* LITERAL == 0 */
		attributes |= LITERAL;
		break;
	    case '}':		/* literal characters end */
		attributes &= ~LITERAL;
		break;
	    default:
#ifndef HAVENOUTMP
		if (*cp == 'a' && what == FMT_WHO) {
		    cz = who_info(info, 'a', cbuff);
		    for (; *cz; *p++ = attributes | *cz++)
			if (p >= ep) break;
		}
		else 
#endif /* HAVENOUTMP */
		{
		    if (p >= ep - 3) break;
		    *p++ = attributes | '%';
		    *p++ = attributes | *cp;
		}
		break;
	    }
	}
	else if (*cp == '\\' || *cp == '^') 
	    *p++ = attributes | parseescape(&cp);
	else if (*cp == HIST) {	/* EGS: handle '!'s in prompts */
	    if (what == FMT_HISTORY) 
		fmthist('h', info, cbuff);
	    else
		(void) xsprintf(cbuff, "%d", eventno + 1);
	    for (cz = cbuff; *cz; *p++ = attributes | *cz++)
		if (p >= ep) break;
	}
	else 
	    *p++ = attributes | *cp;	/* normal character */
    }
    *p = '\0';
}
