#include "config.h"
#include "emu.h"
#include "termio.h"
#include "video.h"
#include "vc.h"

extern int wait_vc_active(void);

/*
int 
sys_crtctl (u_int sn, struct Crtctl *c, int a)
*/

inline void
console_update_cursor(int xpos, int ypos, int blinkflag, int forceflag)
{
  /* This routine updates the state of the cursor, including its cursor
   * position and visibility.  The linux console recognizes the ANSI code 
   * "\033[y;xH" where y is the cursor row, and x is the cursor position.  
   * For example "\033[15;8H" printed to stdout will move the cursor to 
   * row 15, column 8.   The "\033[?25l" string hides the cursor and the 
   * "\033[?25h" string shows the cursor.
   */

  /* Static integers to preserve state of variables from last call. */
  static int oldx = -1;
  static int oldy = -1;
  static int oldblink = 0;
  
  /* If forceflag is set, then this ensures that the cursor is actually
   * updated regardless of its previous state. */
  if (forceflag) {
    oldx = -1;
    oldy = -1;
    oldblink = !blinkflag;
  }

  /* The cursor is off-screen, so disable its blinking */
  if ((unsigned) xpos > co || (unsigned) ypos > li)
    blinkflag = 0;
  
  if (blinkflag) {
    /* Enable blinking if it has not already */
    if (!oldblink)
	printf("\033[?25h");
    /* Update cursor position if it has moved since last time in this func */
    if ((xpos != oldx) || (ypos != oldy)) 
	printf("\033[%d;%dH",ypos+1,xpos+1);
  }
  else {
    /* Disable blinking if it hasnt already */ 
    if (oldblink)
	printf("\033[?25l");
  }
  
  /* Save current state of cursor for next call to this function. */
  oldx = xpos; oldy = ypos; oldblink = blinkflag;
}

void do_console_update_cursor() {
   console_update_cursor(cursor_col,cursor_row,cursor_blink,0);
}

void set_console_video(void)
{
#if 0
  if (config.console_video) {
    k_printf("KBD: Taking mouse control\n");
    if ( config.cardtype == CARD_MDA )
      ioctl(console_fd, KDSETMODE, KD_TEXT);  /* The kernel doesn't support
					     HGA graphics */
    else
      ioctl(console_fd, KDSETMODE, KD_GRAPHICS);
  }
#endif

  /* Clear the Linux console screen. The console recognizes these codes: 
   * \033[?25h = show cursor.
   * \033[0m = reset color.  
   * \033[H = Move cursor to upper-left corner of screen.  
   * \033[2J = Clear screen.  
   */
  printf("\033[?25h\033[0m\033[H\033[2J");

  SCR_STATE.mapped = 0;
  allow_switch();

  /* warning! this must come first! the VT_ACTIVATES which some below
     * cause set_dos_video() and set_linux_video() to use the modecr
     * settings.  We have to first find them here.
     */
  if (config.vga) {
    int permtest;

    permtest = set_ioperm(0x3d4, 2, 1);	/* get 0x3d4 and 0x3d5 */
    permtest |= set_ioperm(0x3da, 1, 1);
    permtest |= set_ioperm(0x3c0, 2, 1);	/* get 0x3c0 and 0x3c1 */
    if (config.chipset == S3) {
      permtest |= set_ioperm(0x102, 1, 1);
      permtest |= set_ioperm(0x2ea, 4, 1);
    }
  }

  /*
     Switch to dosemu VT if config.forcevtswitch is set.
     The idea of this action is that in case of config.console_video
     dosemu need to work at active VT (at least at start) for accessing
     video memory. So to be able to run dosemu for instance
     through cron we need config option like forcevtswitch.
     The action seems sensible only if config.console_video.
                                                   saw@shade.msu.ru
  */
  
#if 0
  if (config.console_video && config.force_vt_switch && !vc_active()) {
    if (ioctl(console_fd, VT_ACTIVATE, SCR_STATE.console_no)<0)
      v_printf("VID: error VT switching %s\n", strerror(errno));
  }
#endif

  /* XXX - get this working correctly! */
#define OLD_SET_CONSOLE 1
#ifdef OLD_SET_CONSOLE
  get_video_ram(WAIT);
  SCR_STATE.mapped = 1;
#endif
  if (vc_active()) {
      /* int other_no = (SCR_STATE.console_no == 1 ? 2 : 1); */
      v_printf("VID: we're active, waiting...\n");
#ifndef OLD_SET_CONSOLE
      get_video_ram(WAIT);
      SCR_STATE.mapped = 1;
#endif
#if 0
      if (!config.vga) {
	  ioctl(console_fd, VT_ACTIVATE, other_no);
/*
 *    Fake running signal_handler() engine to process release/acquire
 *    vt's.
 */
	  while(vc_active())handle_signals();
	  ioctl(console_fd, VT_ACTIVATE, SCR_STATE.console_no);
	  while(!vc_active())handle_signals();
      }
#endif
  }
  else
    v_printf("VID: not active, going on\n");

  allow_switch();
  clear_screen(0, 7);
}

void clear_console_video(void)
{
  if (SCR_STATE.current) {
      /* set_linux_video(); */
      put_video_ram();		/* unmap the screen */
  }

  /* XXX - must be current console! */

  if (config.vga) {
#if 0
    ioctl(console_fd, KIOCSOUND, 0);	/* turn off any sound */
#endif
  } else {
      printf("\033[?25h");        /* Turn on the cursor */
  }

  if (config.console_video) {
    k_printf("KBD: Release mouse control\n");
#if 0
    ioctl(console_fd, KDSETMODE, KD_TEXT);
#endif
  }
}

void set_consoleX_video(void)
{
/*  clear_screen(0, 7); */
}

void clear_consoleX_video(void)
{

}

int terminal_initialize(void)
{
    return 0;
}

void terminal_close(void)
{
}
 

#define console_init terminal_initialize
#define console_close terminal_close
#define console_setmode NULL

struct video_system Video_console = {
   1,                /* is_mapped */
   console_init,
   console_close,
   console_setmode,
   NULL,             /* update_screen */
   do_console_update_cursor
};
