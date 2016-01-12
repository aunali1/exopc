/* for Linux DOS emulator
 *   Robert Sanders, gt8134b@prism.gatech.edu
 *
 */
#ifndef DISKS_H
#define DISKS_H

#include "types.h"

#define PART_INFO_START		0x1be	/* offset in MBR for partition table */
#define PART_INFO_LEN   	0x10	/* size of each partition record */
#define PART_SIG		0x55aa	/* magic signature */

#define PART_NOBOOT	0
#define PART_BOOT	0x80

/* disk file types */
typedef enum {
  NODISK = -1,
  IMAGE = 0, HDISK, FLOPPY, PARTITION, MAXIDX_DTYPES,
  NUM_DTYPES
} disk_t;

#define DISK_RDWR	0
#define DISK_RDONLY	1

/* definitions for 'dexeflags' in 'struct vdisk' and 'struct image_header' */
#define  DISK_IS_DEXE		1
#define  DISK_DEXE_RDWR		2


struct partition {
    int number;
    int beg_head, beg_sec, beg_cyl;
    int end_head, end_sec, end_cyl;
    long pre_secs;		/* sectors preceding partition */
    long num_secs;		/* sectors in partition */
    char *mbr;			/* fake Master Boot Record */
    int mbr_size;		/* usu. 1 sector */
};

struct vdisk {
    char *dev_name;		/* disk file */
    int wantrdonly;		/* user wants the disk to be read only */
    int sectors, heads, tracks;	/* geometry */
    long start;			/* geometry */
    int cmos;			/* default CMOS floppy type */
    disk_t type;		/* type of file: image, partition, disk */
    off_t header;		/* compensation for opt. pre-disk header */
    int fdesc;			/* below are not user settable */
    u_int mode;			/* The way we opened the disk (only filled in if the disk is open) */
    int removeable;		/* not user settable */
    int timeout;		/* seconds between floppy timeouts */
    struct partition part_info;	/* neato partition info */
    Bit8u error;                /* last error */
#ifndef NDEBUG
    u_int read_count;
    u_int write_count;
#endif
};

#if 0
/* NOTE: the "header" element in the structure above can (and will) be
 * negative. This facilitates treating partitions as disks (i.e. using
 * /dev/hda1 with a simulated partition table) by adjusting out the
 * simulated partition table offset...
 */

struct vdisk_fptr {
  void (*autosense) (struct vdisk *);
  void (*setup) (struct vdisk *);
};

#endif


/* this header appears only in hdimage files
 */
struct image_header {
  char sig[7];			/* always set to "DOSEMU", null-terminated
				   or to "\x0eDEXE" */
  long heads;
  long sectors;
  long cylinders;
  long header_end;	/* distance from beginning of disk to end of header
			 * i.e. this is the starting byte of the real disk
			 */
  char dummy[1];	/* someone did define the header unaligned,
  			 * we correct that atleast for the future
  			 */
  long dexeflags;
} __attribute__((packed)) ;

#define IMAGE_MAGIC		"DOSEMU"
#define IMAGE_MAGIC_SIZE	strlen(IMAGE_MAGIC)
#define DEXE_MAGIC		0x5845440e /* 0x0e,'D','E','X' */
#define HEADER_SIZE		128

/* CMOS types for the floppies */
#define MAX_FDISKS 4
#define MAX_HDISKS 16
#define SECTOR_SIZE		512

/*
 * Array of disk structures for floppies...
 */ 
extern struct vdisk fdisktab[MAX_FDISKS];

/*
 * Array of disk structures for hard disks...
 *
 * Can be whole hard disks, dos extended partitions (containing one or
 * more partitions) or their images (files)
 */
extern struct vdisk hdisktab[MAX_HDISKS];


#if 1
#ifdef __linux__
#define DISK_OFFSET(dp,h,s,t) \
  (((long long)(t * dp->heads + h) * dp->sectors + s) * SECTOR_SIZE)
#else
#define DISK_OFFSET(dp,h,s,t) \
  (((t * dp->heads + h) * dp->sectors + s) * SECTOR_SIZE)
#endif
#else
#define DISK_OFFSET(dp,h,s,t) \
  (((h * dp->tracks + t) * dp->sectors + s) * SECTOR_SIZE)
#endif

int read_sectors(struct vdisk *, char *, u_int, u_int, u_int, u_int);
int write_sectors(struct vdisk *, char *, u_int, u_int, u_int, u_int);

void d_nullf(struct vdisk *);

void disk_open(struct vdisk *dp);
void disk_close(void);
void boot(struct vdisk *);

#define partition_auto	hdisk_auto
#define floppy_auto	d_nullf

#define image_setup	d_nullf
#define hdisk_setup	d_nullf
void partition_setup(struct vdisk *);

#define floppy_setup	d_nullf

/* int13 error returns */
#define DERR_NOERR	0
#define DERR_BADCMD 	1
#define DERR_BADSEC 	2
#define DERR_WP 	3
#define DERR_NOTFOUND 	4
#define DERR_CHANGE 	6
#define DERR_ECCERR 	0x10
#define DERR_CONTROLLER	0x20
#define DERR_SEEK	0x40
#define DERR_NOTREADY	0x80
#define DERR_WRITEFLT	0xcc
#define DERR_SENSE      0xff

void print_disks(void);
void disk_create(const char *name, disk_t type, int floppy, int ro);
void map_disk_params(void);

#endif
