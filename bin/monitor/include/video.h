#ifndef VIDEO_H
#define VIDEO_H

#ifndef INIT
#define INIT(x...)
#endif

typedef unsigned char byte;

typedef struct { byte end, start; } cshape;

extern void gettermcap(int);
extern void scr_state_init(void);

#define max_page 7  /* number of highest vid page - 1 */

/* if you set this to 1, the video memory dirty bit will be checked 
   before updating the screen.
   (this affects emu.c and video/int10.c)
*/
#define VIDEO_CHECK_DIRTY 0

/* if you set this to 1, then you will be able to use your MDA + monitor
   as second display. This currently is possible together with
   Video_term, Video_console and Video_graphics.
   You also must set the keyword "dualmon" in the video section
   of your /etc/dosemu.conf.
   With dualmonitor support you can run CAD-programs, debuggers, or
   simply change your PC-console with "mode mono"
   NOTE: 
     Currently this can't be used together with VIDEO_CHECK_DIRTY,
     because the kernel (vm86.c) would remap all video pages
     from 0xa0000 to 0xbffff.
*/
#define USE_DUALMON 0

#if USE_DUALMON && VIDEO_CHECK_DIRTY
  #error "Currently USE_DUALMON can't be used together with VIDEO_CHECK_DIRTY"
#endif

#define CURSOR_START(c) ((int)((cshape*)&c)->start)
#define CURSOR_END(c)   ((int)((cshape*)&c)->end)
#define NO_CURSOR 0x0100

#define MAX_COLUMNS 132
#define MAX_LINES 60

#if 0
#define SCREEN_ADR(s)	((u_short *)(virt_text_base + (s*TEXT_SIZE)))
#endif

/********************************************/

/* macros for accessing video memory. w is an ushort* 
   to a character cell, attr is a byte.
*/

#if 0
#define CHAR(w) (((char*)(w))[0])
#define ATTR(w) (((byte*)(w))[1])
#else
#define CHAR(w) ((char)READ_BYTE(w))
#define ATTR(w) ((byte)READ_BYTE(((Bit8u *)(w))+1))
#endif
#define ATTR_FG(attr) (attr & 0x0F)
#define ATTR_BG(attr) (attr >> 4)

/**********************************************************************/
/* scroll queue */

#define USE_SCROLL_QUEUE 0

#if USE_SCROLL_QUEUE

struct scroll_entry {
   short x0,y0,x1,y1;
   short n;
   byte attr;
};

#define SQ_MAXLENGTH 5

struct scroll_entry scroll_queue[SQ_MAXLENGTH+1];
int sq_head,sq_tail;

struct scroll_entry *get_scroll_queue();
void clear_scroll_queue();

extern int video_update_lock;

#define VIDEO_UPDATE_LOCK() video_update_lock++;
#define VIDEO_UPDATE_UNLOCK() video_update_lock--;

#else
#define video_update_lock 0
#define VIDEO_UPDATE_LOCK()
#define VIDEO_UPDATE_UNLOCK()
#define clear_scroll_queue()
#endif

/***********************************************************************/

/* Here's an idea to clean up the video code: Build a 'virtual' video
   support, a bit like the VFS in the linux kernel.
   The following structure defines the currently active video routines.
   There should be one such structure for each 'video front end',
   i.e. terminal, ncurses, console, X.
   The pointer 'Video' will be initialized at startup to point to
   the video_system struct of the active frontend and Video->init
   will then be called. The other contents don't have to be valid
   before Video->init() but the recommended method is to define
   them statically.

   Most of the function pointers defined here can be set to NULL for
   no special handling.
   functions returning int should return zero for normal completion,
   non-zero on error (unless specified otherwise).

   The contents of this struct should only be changed by init, cleanup
   and (maybe) setmode.
*/


struct video_system {
   unsigned char is_mapped;       /* if true, video ram is directly mapped and
                                  update_screen is not needed. */

   int (*init)(void);              /* does all frontend-specific setup,
                                  like mapping video memory, opening XWindow,
                                  etc. */
   void (*close)(void);
   
   int (*setmode)(int type, int xsize,int ysize);   /* type=0 currently (text mode) */

   int (*update_screen)(void);     /* (partially) update screen and cursor from the 
                                  video memory. called from sigalrm handler */

   void (*update_cursor)(void);    /* update cursor position&shape. Called by sigalrm
                                  handler *only* if update_screen does not exist
                                  or is not done because the video mem is clean */
};

extern struct video_system *Video;

extern struct video_system Video_graphics,  Video_X, Video_console, Video_hgc, Video_term;

extern ushort *screen_adr;   /* pointer to video memory of current page */
/* currently displayed page (was scrbuf) */
extern ushort *prev_screen;  /* pointer to currently displayed screen   */
                             /* used&updated by Video->update_screen    */

extern int video_mode INIT(0);
extern int video_page INIT(0); 
extern int char_blink INIT(0);
extern int co INIT(80);
extern int li INIT(25);

extern int cursor_col INIT(0);
extern int cursor_row INIT(0);
extern int cursor_blink;
extern ushort cursor_shape INIT(0xe0f);
/* bit mask for testing vm86s.screen_bitmap */
extern unsigned int screen_mask;

extern int font_height INIT(16);

extern int vga_font_height INIT(16);  /* current EMULATED setting for vga font height */

extern int std_font_height INIT(16);  /* font height set by int10,0 mode 3 */
extern int text_scanlines INIT(400);   /* # of scan lines in textmodes */
                             /* these have effect only on video mode sets! */

extern unsigned char video_initialized INIT(0);
extern int vga_initialize(void);
extern void mda_initialize(void);
/* extern void install_int_10_handler(void); */
extern void clear_screen(int s,int att);
extern int set_video_mode(int);

extern void Scroll(u_short *sadr,int x0,int y0,int x1,int y1,int n,int attr);
#define scrollup(x0,y0,x1,y1,l,att) Scroll(x0,y0,x1,y1,l,att)
#define scrolldn(x0,y0,x1,y1,l,att) Scroll(x0,y0,x1,y1,-(l),att)

/* Values are set by video_config_init depending on video-card defined in config */
/* Values are set from emu.c depending on video-config */


extern int virt_text_base INIT(0);
extern int phys_text_base INIT(0);
extern int video_combo INIT(0);
extern int video_subsys;
extern int v_8514_base INIT(0);

/* The following defines are for terminal (curses) mode */

/* Character set defines */
#define CHARSET_LATIN	1
#define CHARSET_IBM	2
#define CHARSET_FULLIBM	3

/* Color set defines */
/* #define COLOR_NORMAL	1 */
/* #define COLOR_XTERM	2 */

/* Terminal update defines. For using direct ANSI sequences, or NCURSES */
/* #define METHOD_FAST	1 */
/* #define METHOD_NCURSES	2 */

/* Various defines for all common video adapters */

#define CARD_VGA	1
#define CARD_EGA	2
#define CARD_CGA	3
#define CARD_MDA	4

/* Defines for Monochrome Display Adapter */

#define MDA_INIT_SCREEN_MODE   7 /* 80x25 MDA monochrome */
#define MDA_CONF_SCREEN_MODE   (3<<4)	/* for int 11h info     */
#define MDA_VIDEO_COMBO        1
#define MDA_VIDEO_SUBSYS       1 /* 1=mono               */
/* #define BASE_CRTC               0x3b4  currently not used */

/* Defines for Color Graphics Adapter */

#define CGA_INIT_SCREEN_MODE   3 /* 80x25 VGA color */
#define CGA_CONF_SCREEN_MODE   (2<<4)	/* (2<<4)=80x25 color CGA/EGA/VGA */
#define CGA_VIDEO_COMBO        4 /* 4=EGA (ok), 8=VGA (not ok?) */
#define CGA_VIDEO_SUBSYS       0 /* 0=color */
/* #define BASE_CRTC               0x3d4  currently not used */

/* Defines for Enhanched Graphics Adapter, same as CGA */

#define EGA_INIT_SCREEN_MODE   3 /* 80x25 VGA color */
#define EGA_CONF_SCREEN_MODE   (2<<4)	/* (2<<4)=80x25 color CGA/EGA/VGA */
#define EGA_VIDEO_COMBO        4 /* 4=EGA (ok), 8=VGA (not ok?) */
#define EGA_VIDEO_SUBSYS       0 /* 0=color */
/* #define BASE_CRTC               0x3d4  currently not used */

/* Defines for Video Graphic Array, same as CGA */

#define VGA_INIT_SCREEN_MODE   3 /* 80x25 VGA color */
#define VGA_CONF_SCREEN_MODE   (2<<4)	/* (2<<4)=80x25 color CGA/EGA/VGA */
#define VGA_VIDEO_COMBO        8 /* 4=EGA (ok), 8=VGA (not ok?) */
#define VGA_VIDEO_SUBSYS       0 /* 0=color */
/* #define BASE_CRTC               0x3d4  currently not used */


#define PLAINVGA	0
#define TRIDENT		1
#define ET4000		2
#define DIAMOND		3
#define S3		4
#define AVANCE		5
#define ATI		6
#define CIRRUS		7
#define MATROX		8
#define WDVGA		9
#define MAX_CARDTYPE	WDVGA

extern void get_screen_size (void);
extern void set_video_bios_size(void);
extern void X_handle_events(void);
extern void X_blink_cursor(void);
extern void init_vga_card(void);
extern void scr_state_init(void);

/* moved here from s3.c --AV  */
#define BASE_8514_1	0x2e8
#define BASE_8514_2	0x148

#endif
