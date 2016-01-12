#include "port.h"
#include "emu.h"
#include "priv.h"
#include "debug.h"
#include "cpu.h"
#include "config.h"
#include "types.h"
#include "tss.h"
#include "opa.h"

static struct port_struct {
    int start;
    int size;
    int permission;
    int ormask, andmask;
    int fp;
} *ports = NULL;
static int num_ports = 0;
static u_char video_port_io = 0;

/* find a matching entry in the ports array */
static int find_port(unsigned short port, int permission)
{
    static int last_found = 0;
    unsigned int i;

    for (i = last_found; i < num_ports; i++)
	if (port >= ports[i].start &&
	    port < (ports[i].start + ports[i].size) &&
	    ((ports[i].permission & permission) == permission)) {
	    last_found = i;
	    return (i);
	}
    for (i = 0; i < last_found; i++)
	if (port >= ports[i].start &&
	    port < (ports[i].start + ports[i].size) &&
	    ((ports[i].permission & permission) == permission)) {
	    last_found = i;
	    return (i);
	}
/*
   for now, video_port_io flag allows special access for video regs
 */
    if (((LWORD(cs) & 0xf000) == config.vbios_seg)
	&& config.allowvideoportaccess
    /* && scr_state.current */ ) {
	v_printf("VID: checking port %x\n", port);

	if (port > 0x4000)
	    return (-1);
	port &= 0xfff;
	if ((port > 0x300 && port < 0x3f8)
	    || port > 0x400
	    || port == 0x42 || port == 0x1ce || port == 0x1cf) {
	    video_port_io = 1;
	    return 1;
	}
    }
    return (-1);
}

#if 0
/* control the io permissions for ports */
int allow_io(unsigned int start, int size, int permission, int ormask, int andmask,
	     unsigned int portspeed, char *device)
{
    int opendevice = -1;
    char line[100];
    char portname[50];
    unsigned int beg, end;
    FILE *fp;

    /* find out whether the port address request is available 
     * this way, try to deny uncoordinated acces
     *
     * If it is not listed in /proc/ioports, register them 
     * ( we need some syscall to do so bo 960609)
     * if it is registered, we need the name of a device to open
     * if we can open it, we disallow to that port
     */
    if ((fp = fopen("/proc/ioports", "r")) == NULL) {
	i_printf("Ports can't open /proc/ioports\n");
	return (0);
    }
    while (fgets(line, 100, fp)) {
	sscanf(line, "%x-%x : %s", &beg, &end, portname);
	if ((start >= beg) && (start + size - 1 <= end)) {
	    /* ports are occupied, try to open the according device */
	    i_printf("PORT already registered as %s 0x%04x-0x%04x\n",
		     portname, beg, end);
	    enter_priv_on();
	    opendevice = open(device, O_RDWR);
	    leave_priv_setting();
	    if (opendevice == -1)
		switch (errno) {
		case EBUSY:
		    i_printf("PORT Device %s already in use\n", device);
		    return (0);
		case EACCES:
		    i_printf("PORT Device %s , access not allowed\n", device);
		    return (0);
		case ENOENT:
		    i_printf("PORT No such Device %s\n", device);
		    return (0);
		}
	    i_printf("PORT Device %s opened successfully\n", device);
	}
    }
    if (opendevice == -1) {
	/* register it
	 */
	i_printf("PORT Syscall to register ports not yet implemented\n");
    }
#if 0
    if ((base + size) < beg || base >= end)
	continue;
    else
	return NULL;		/* overlap */
#endif



    /* first the simplest case...however, this simplest case does
     * not apply to ports above 0x3ff--the maximum ioperm()able
     * port under Linux--so we must handle some of that ourselves.
     *
     * if we want to log port access below 0x400, don't enter these 
     * ports here with set_ioperm but add them to the list of allowed
     * ports. That way we get an exception to get out of vm86(0)
     */
    if (permission == IO_RDWR && (ormask == 0 && andmask == 0xFFFF)
	&& portspeed) {
	if ((start + size - 1) <= 0x3ff) {
	    i_printf("giving fast hardware access to ports 0x%x -> 0x%x\n",
		     start, start + size - 1);
	    set_ioperm(start, size, 1);
	    /* Don't return, but enter it in the list to keep track of the file 
	     * descriptor
	     * return (1);
	     */
	}
	/* allow direct I/O to all the ports that *are* below 0x3ff
	   * and add the rest to our list
	 */
	else if (start <= 0x3ff) {
	    warn("PORT: s-s: %d %d\n", start, size);
	    i_printf("giving fast hardware access only to ports 0x%x -> 0x3ff\n",
		     start);
	    set_ioperm(start, 0x400 - start, 1);
	    size = start + size - 0x400;
	    start = 0x400;
	}
    }
    /* we'll need to add an entry */
    if (ports) {
	ASSERT(0);  /* FIXME XXX */
	/* ports = (struct port_struct *) realloc(ports, sizeof(struct port_struct) * (num_ports + 1)); */
    } else {
	ASSERT(0);  /* FIXME XXX */
	/* ports = (struct port_struct *) malloc(sizeof(struct port_struct) * (num_ports + 1)); */
    }

    num_ports++;
    if (!ports) {
	error("allocation error for ports!\n");
	num_ports = 0;
	return (0);
    }
    ports[num_ports - 1].start = start;
    ports[num_ports - 1].size = size;
    ports[num_ports - 1].permission = permission;
    ports[num_ports - 1].ormask = ormask;
    ports[num_ports - 1].andmask = andmask;
    ports[num_ports - 1].fp = opendevice;

    i_printf("added port %x size=%d permission=%d ormask=%x andmask=%x\n",
	     start, size, permission, ormask, andmask);

    return (1);
}
#endif

/* release registered ports or blocked devices */
void release_ports()
{
/* FIXME */
#if 0
    unsigned int i;

    for (i = 0; i < num_ports; i++)
	if (ports[i].fp == -1) {
	    i_printf("PORT Syscall to release ports not yet implemented\n");
	    i_printf("PORT releasing registered ports 0x%04x-0x%04x \n",
		     ports[i].start, ports[i].start + ports[i].size - 1);
	} else if (close(ports[i].fp) == -1) {
	    i_printf("PORT Closeing device-filedescriptor %d failed with %s\n",
		     ports[i].fp, strerror(errno));
	} else {
	    i_printf("PORT Closeing device succeeded\n");
	}
#endif
}


int port_readable(unsigned short port)
{
    return (find_port(port, IO_READ) != -1);
}

inline int port_writeable(unsigned short port)
{
    return (find_port(port, IO_WRITE) != -1);
}


/*
  inb,outb, etc only check if _host_ will let us access ports.  it
  assumes guest is already checked and happy (via guest_ports_accessible())
*/

int guest_ports_accessible(unsigned short port, unsigned short numports)
{
    if (! opa.pe)
	return 1;

    if (opa.cpl > opa.iopl && vmstate.tr_loaded) {
	if (((struct Ts*)vmstate.tr_base)->ts_iomb + numports/8 + (numports&7) ? 1 : 0 > vmstate.tr_limit) {
	    /* Past the end of bitmap, so you lose. */
	    return 0;
	} else {
	    /* Test if any of bits numbered (port .. port+numports] are set --> return 0 */

	    /* FIXME */

	    return 0;
	}
    }

    return 1;
}

int ports_writeable(unsigned short port, unsigned short bytes)
{
    unsigned short i;
    for (i=port; i<port+bytes; i++)
	if (! port_writeable(i))
	    return 0;
    return 1;
}


/* Support for controllable port i/o tracing            G. Guenther 96.07.20

   The "T" debug flag and LOG_IO macro below may be used to collect
   port i/o protocol traces.  These are the latest version of the patches
   that I used to collect the data required for deducing the parallel
   port communication protocols used by the ZIP drive, the Shuttle EPAT
   chip (in the EZ drives) and the backpack CDrom ...

   Uwe's patches to allow_io above, control whether or not "T" debugging
   will work:  If a port is listed in the ports section of the DOSemu
   config file, and *NOT* given the "fast" attribute, then all accesses
   to that port will trap through the following routines.  If the "T"
   flag is enabled (with -D+T or dosdbg +T), a very short record will
   be logged to the debug file for every access to a listed port.

   If you enable "h" *and* "i" debugging you will get the same information 
   logged in a much more verbose manner - along with a lot of other stuff.
   This verbosity turns out to be a serious problem, as it can slow the \
   emulator's response so much that external hardware gets confused.  

   (Can anybody explain *why* byte accesses are logged with the 'h' flag
   and word access are logged with the 'i' flag ? )

   If you are trying to use a slow system to trace a fast protocol, you
   may need to shorten the output further ...  (In one case, I had to 
   add suppression for repeated identical writes, as the drive being
   monitored read and wrote each byte 4 times ...)

   The logged records look like this:

   ppp f vvvv

   ppp is the hex address of the port, f is a flag, one of:

   >   byte read    inb
   <   byte write   outb
   }   word read    inw
   {   word write   outw

   vvvv is the value read or written.

 */

#define LOG_IO(p,r,f,m)	  i_printf("%x %c %x\n",p&0xfff,f,r&m);

unsigned char read_port(unsigned short port)
{
    unsigned char r;
    int i = find_port(port, IO_READ);

    /* FIXME:  stuff does/should check find_port first, so don't waste
       time checking again!  ASSERT!! */

    if (i == -1)
	return (0xff);

    if (!video_port_io)
	enter_priv_on();
    if (port <= 0x3ff)
	set_ioperm(port, 1, 1);
    else
	priv_iopl(3);
    if (!video_port_io)
	leave_priv_setting();

    r = port_in(port);

    if (!video_port_io)
	enter_priv_on();
    if (port <= 0x3ff)
	set_ioperm(port, 1, 0);
    else
	priv_iopl(0);
    if (!video_port_io)
	leave_priv_setting();

    if (!video_port_io) {
	r &= ports[i].andmask;
	r |= ports[i].ormask;
    } else
	video_port_io = 0;

    LOG_IO(port, r, '>', 0xff);

    i_printf("read port 0x%x gave %02x at %04x:%04x\n",
	     port, r, LWORD(cs), LWORD(eip));
    return (r);
}

unsigned int read_port_w(unsigned short port)
{
    unsigned int r;
    int i = find_port(port, IO_READ);
    int j = find_port(port + 1, IO_READ);

#ifdef GUSPNP
    if (port == 0x324)
	i = j = 0;
#endif

    /* FIXME:  shoudl already be checked.  ASSERT if i, j == -1 */
    if (i == -1) {
	i_printf("can't read low-byte of 16 bit port 0x%04x:trying to read high\n",
		 port);
	return ((read_port(port + 1) << 8) | 0xff);
    }
    if (j == -1) {
	i_printf("can't read hi-byte of 16 bit port 0x%04x:trying to read low\n",
		 port);
	return (read_port(port) | 0xff00);
    }
    if (!video_port_io)
	enter_priv_on();
    if (port <= 0x3fe) {
	set_ioperm(port, 2, 1);
    } else
	priv_iopl(3);
    if (!video_port_io)
	leave_priv_setting();

    r = port_in_w(port);
    if (!video_port_io)
	enter_priv_on();
    if (port <= 0x3fe)
	set_ioperm(port, 2, 0);
    else
	priv_iopl(0);
    if (!video_port_io)
	leave_priv_setting();

    if (
#ifdef GUSPNP
	   i && j &&
#endif
	   !video_port_io) {
	r &= (ports[i].andmask | 0xff00);
	r &= ((ports[j].andmask << 8) | 0xff);
	r |= (ports[i].ormask & 0xff);
	r |= ((ports[j].ormask << 8) & 0xff00);
    } else
	video_port_io = 0;

    LOG_IO(port, r, '}', 0xffff);

    i_printf("read 16 bit port 0x%x gave %04x at %04x:%04x\n",
	     port, r, LWORD(cs), LWORD(eip));
    return (r);
}

int write_port(unsigned int value, unsigned short port)
{
    int i = find_port(port, IO_WRITE);

    if (i == -1)
	return (0);

    if (!video_port_io) {
	value &= ports[i].andmask;
	value |= ports[i].ormask;
    }
    i_printf("write port 0x%x value %02x at %04x:%04x\n",
	     port, (value & 0xff), LWORD(cs), LWORD(eip));

    LOG_IO(port, value, '<', 0xff);

    if (!video_port_io)
	enter_priv_on();
    if (port <= 0x3ff)
	set_ioperm(port, 1, 1);
    else
	priv_iopl(3);
    if (!video_port_io)
	leave_priv_setting();

    port_out(value, port);

    if (!video_port_io)
	enter_priv_on();
    if (port <= 0x3ff)
	set_ioperm(port, 1, 0);
    else
	priv_iopl(0);
    if (!video_port_io)
	leave_priv_setting();
    video_port_io = 0;

    return (1);
}

int write_port_w(unsigned int value, unsigned short port)
{
    int i = find_port(port, IO_WRITE);
    int j = find_port(port + 1, IO_WRITE);

#ifdef GUSPNP
    if (port == 0x324)
	i = j = 0;
#endif
    if ((i == -1) || (j == -1)) {
	i_printf("can't write to 16 bit port 0x%04x\n", port);
	return 0;
    }
    if (
#ifdef GUSPNP
	   i && j &&
#endif
	   !video_port_io) {
	value &= (ports[i].andmask | 0xff00);
	value &= ((ports[j].andmask << 8) | 0xff);
	value |= (ports[i].ormask & 0xff);
	value |= ((ports[j].ormask << 8) & 0xff00);
    }
    i_printf("write 16 bit port 0x%x value %04x at %04x:%04x\n",
	     port, (value & 0xffff), LWORD(cs), LWORD(eip));

    LOG_IO(port, value, '{', 0xffff);

    if (!video_port_io)
	enter_priv_on();
    if (port <= 0x3fe)
	set_ioperm(port, 2, 1);
    else
	priv_iopl(3);
    if (!video_port_io)
	leave_priv_setting();

    port_out_w(value, port);

    if (!video_port_io)
	enter_priv_on();
    if (port <= 0x3fe)
	set_ioperm(port, 2, 0);
    else
	priv_iopl(0);
    if (!video_port_io)
	leave_priv_setting();
    video_port_io = 0;

    return (1);
}

void safe_port_out_byte(const unsigned short port, const unsigned char byte)
{
    if (i_am_root) {
	int result;

	result = set_ioperm(port, 1, 1);
	if (result) {
	    dprintf("failed to enable port %x\n", port);
	    leaveemu(ERR_IO);
	}
	port_out(byte, port);
	result = set_ioperm(port, 1, 0);
	if (result) {
	    dprintf("failed to disable port %x\n", port);
	    leaveemu(ERR_IO);
	}

    } else
	i_printf("want to ");
    i_printf("out(%x, 0x%x)\n", port, byte);
}


char safe_port_in_byte(const unsigned short port)
{
    unsigned char value = 0;

    if (i_am_root) {
	int result;

	result = set_ioperm(port, 1, 1);
	if (result)
	    error("failed to enable port %x\n", port);
	value = port_in(port);
	result = set_ioperm(port, 1, 0);
	if (result)
	    error("failed to disable port %x\n", port);
    } else
	i_printf("want to ");
    i_printf("in(%x)", port);
    if (i_am_root)
	i_printf(" = 0x%x", value);
    i_printf("\n");
    return value;
}
