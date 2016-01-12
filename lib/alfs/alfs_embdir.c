
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


/* ALFS -- Application-Level Filesystem */

/* functions for accessing and manipulating directories with embedded content */

/* NOTE: this implementation assumes several things about the on-disk	*/
/*       (as it sees it) format of directory entries:			*/
/*       (1) a blank character is included at the end of each name.	*/
/*       (2) all name and embedded content sizes (and therefore entry	*/
/*           sizes) are rounded up to 4-byte boundaries.		*/
/*       (3) no directory entry crosses a "sector" boundary, where the	*/
/*           "sector" size is defined by ALFS_EMBDIR_SECTOR_SIZE,	*/
/*           unless forced to by the embedded content length.		*/

/* NOTE: Currently, the embedded content length can NOT cause an	*/
/*       entry's size to exceed ALFS_EMBDIR_SECTOR_SIZE.		*/

#include "alfs/alfs_buffer.h"
#include "alfs/alfs_dinode.h"
#include "alfs/alfs_embdir.h"
#include "alfs/alfs_alloc.h"
#include <assert.h>
#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include <machine/param.h>

#include "alfs/alfs_diskio.h"

#define ALFS_EMBDIR_DIRNUMBLOCKS	8
#define ALFS_EMBDIR_MAXDIRNUM ((ALFS_EMBDIR_DIRNUMBLOCKS * BLOCK_SIZE / sizeof (unsigned int)) - 1)

static unsigned int alfs_embdir_dirNumMap [(ALFS_EMBDIR_MAXDIRNUM + 1)];
static unsigned int alfs_embdir_lastDirNum = 1;  /* 0 is unused */

static int alfs_embdir_getblockforname (block_num_t, dinode_t *, char *, int, int, buffer_t **, embdirent_t **, embdirent_t **, int *);
static embdirent_t * alfs_embdir_compressEntries (char *, int);
static void alfs_embdir_initDirBlock (char *);


/*************************** alfs_embdir_lookupname *********************/
/*									*/
/* scan the directory for "name" and return a pointer to the		*/
/*    corresponding directory entry if found.  Otherwise, return NULL.	*/
/*									*/
/* Parameters upon entry:						*/
/*    fsdev -- the file system identifier used for buffer acquisition	*/
/*    dirInode -- the inode for the directory				*/
/*    name -- the name being looked up					*/
/*    namelen -- length of the name (in characters)			*/
/*    bufferPtr -- pointer to a buffer_t * whose value is undefined	*/
/*									*/
/* Parameters upon exit:  first four same as entry.			*/
/*    bufferPtr -- if name found, (*bufferPtr) contains pointer to	*/
/*                 buffer containing corresponding directory block.	*/
/*                 Must be freed by caller.  Otherwise, undefined.	*/
/*									*/
/* Changes to state: buffer entries for directory blocks may be		*/
/*                   acquired (as indicated above, *bufferPtr held if	*/
/*                   name found)					*/
/*									*/
/************************************************************************/

embdirent_t * alfs_embdir_lookupname (block_num_t fsdev, dinode_t *dirInode, char *name, int namelen, buffer_t **bufferPtr) {
     int currPos = 0;
     int dirBlockLength;
     int dirBlockNumber = 0;
     embdirent_t *dirent;
     buffer_t *dirBlockBuffer;
     char *dirBlock;
     int i;

     assert (dirInode);
/*
printf ("alfs_embdir_lookupname: name %s, namelen %d, dirInodeNum %d (%x), dirLength %d\n", name, namelen, dirInode->dinodeNum, dirInode->dinodeNum, alfs_dinode_getLength(dirInode));
*/

     /* search through the directory looking for the name */

     while (currPos < alfs_dinode_getLength(dirInode)) {
         dirBlockLength = min((alfs_dinode_getLength(dirInode) - currPos), BLOCK_SIZE);
         dirBlockBuffer = alfs_buffer_getBlock (fsdev, dirInode->dinodeNum, dirBlockNumber, BUFFER_READ);
         dirBlockNumber++;
         dirBlock = dirBlockBuffer->buffer;
         for (i=0; i < dirBlockLength; i += dirent->entryLen) {
             dirent = (embdirent_t *) (dirBlock + i);
             if ((namelen == dirent->nameLen) && (name[0] == dirent->name[0]) &&
                 (dirent->type != (char) 0) && (bcmp(dirent->name, name, dirent->nameLen) == 0)) {
                  /* name found */
                  *bufferPtr = dirBlockBuffer;
                  return(dirent);
             }
         }
         alfs_buffer_releaseBlock (dirBlockBuffer, 0);
         currPos += dirBlockLength;
     }

     return (NULL);		/* name not found */
}


/************************ alfs_embdir_getblockforname *******************/
/*									*/
/* scan the directory for "name" and return 1 if found and 0 otherwise	*/
/*									*/
/* Parameters upon entry:						*/
/*    fsdev -- the file system identifier used for buffer acquisition	*/
/*    dirInode -- the inode for the directory				*/
/*    name -- the name being looked up					*/
/*    namelen -- length of the name (in characters)			*/
/*    entryLen -- length of the new entry (in characters)		*/
/*    dirBlockBufferP -- undefined					*/
/*    direntP -- undefined						*/
/*    prevDirentP -- undefined						*/
/*    freeSpaceCount -- undefined					*/
/*									*/
/* Parameters upon exit (only differences noted):			*/
/*    if name found:							*/
/*        dirBlockBufferP -- points to directory block containing entry	*/
/*        direntP -- points to directory entry with matching name	*/
/*        prevdirentP -- points to the immediately previous entry, or	*/
/*                       NULL if direntP points to start of a "sector".	*/
/*        freeSpaceCount -- undefined					*/
/*    if name not found:						*/
/*        dirBlockBufferP -- points to a directory "sector" with enough	*/
/*                           free space for "name"d entry to be		*/
/*                           inserted, or NULL if no such sector	*/
/*                           currently exists				*/
/*        direntP -- points to an entry with enough extra space for	*/
/*                   "name"d entry to be inserted after it, or NULL if	*/
/*                   no such entry exists				*/
/*        prevDirentP -- points to the beginning of a "sector" with	*/
/*                       enough free space (after compression) for	*/
/*                       "name"d entry to be inserted. (Note: undefined	*/
/*                       if *direntP not NULL or *dirBlockBufferP NULL)	*/
/*        freeSpaceCount -- the amount of free space in the "sector"	*/
/*                          identified by prevDirentP (Note: undefined	*/
/*                          whenever prevDirentP is undefined)		*/
/*									*/
/* Changes to state: buffer entries for directory blocks may be		*/
/*                   acquired and released with no changes made. The	*/
/*                   buffer addressed by *dirBlockBufferP is acquired	*/
/*                   WITM and must be released by caller.		*/
/*									*/
/************************************************************************/

static int alfs_embdir_getblockforname (block_num_t fsdev, dinode_t *dirInode, char *name, int namelen, int entryLen, buffer_t **dirBlockBufferP, embdirent_t **direntP, embdirent_t **prevDirentP, int *freeSpaceCount) {
     int currPos = 0;
     embdirent_t *dirent;
     buffer_t *dirBlockBuffer = NULL;
     char *dirBlock = NULL;
     int dirBlockNumber = 0;
     embdirent_t *prevDirent;
     embdirent_t *freeDirent = NULL;
     buffer_t *freeBlockBuffer = NULL;
     int dirBlockLength;
     int extraSpace;
     int extraSpaceCount;
     int i;

     assert (dirInode);

     /* search through the directory looking for the name */

     while (currPos < alfs_dinode_getLength(dirInode)) {

			/* scan one "sector" at a time */
         dirBlockLength = min((alfs_dinode_getLength(dirInode) - currPos), ALFS_EMBDIR_SECTOR_SIZE);

         if ((currPos % BLOCK_SIZE) == 0) {
			/* next "sector" begins a new directory block */
             dirBlockBuffer = alfs_buffer_getBlock (fsdev, dirInode->dinodeNum, dirBlockNumber, (BUFFER_READ | BUFFER_WITM));
             dirBlockNumber++;
             dirBlock = dirBlockBuffer->buffer;
         } else {
			/* next "sector" in same block as previous "sector" */
             dirBlock += ALFS_EMBDIR_SECTOR_SIZE;
         }

			/* state initialization for scanning a "sector" */
         extraSpaceCount = 0;
         prevDirent = NULL;

			/* examine each entry in turn */
         for (i=0; i < dirBlockLength; i += dirent->entryLen) {
             dirent = (embdirent_t *) (dirBlock + i);
             if ((namelen == dirent->nameLen) && (name[0] == dirent->name[0]) &&
                 (dirent->type != (char) 0) && (bcmp(dirent->name, name, namelen) == 0)) {
				/* directory entry matches name -- return */
                 *dirBlockBufferP = dirBlockBuffer;
                 *direntP = dirent;
                 *prevDirentP = prevDirent;
                 if ((freeBlockBuffer) && (freeBlockBuffer != dirBlockBuffer)) {
                    alfs_buffer_releaseBlock (freeBlockBuffer, 0);
                 }
                 return(1);
             }
			/* name does not match, so compute extra space in entry */
             extraSpace = (dirent->type == (char) 0) ? dirent->entryLen : (dirent->entryLen - embdirentsize(dirent));
             extraSpaceCount += extraSpace;
             if ((freeDirent == NULL) && (extraSpace >= entryLen)) {
			/* entry has enough extra space to accomodate addition of */
			/* "name"d entry.  This takes precedence over finding a */
			/* "sector" that can accomodate the new entry after compression */
                  freeDirent = dirent;   /* set marker for entry with extra space */
                  if ((freeBlockBuffer) && (freeBlockBuffer != dirBlockBuffer)) {
			/* stop holding block that needs compression to be used */
                      alfs_buffer_releaseBlock (freeBlockBuffer, 0);
                  }
                  freeBlockBuffer = dirBlockBuffer;
             }
			/* update pointer to previous entry for next iteration */
             prevDirent = dirent;
         }

         if ((freeBlockBuffer == NULL) && (extraSpaceCount >= entryLen)) {
			/* with compression, block can accomodate addition */
			/* of "name"d entry                                */
             freeBlockBuffer = dirBlockBuffer;
             *freeSpaceCount = extraSpaceCount;
             *prevDirentP = (embdirent_t *) dirBlock;
         }

		/* done scanning this "sector", prepare to move on to next */
         currPos += dirBlockLength;
         if ((dirBlockBuffer != freeBlockBuffer) && ((currPos % BLOCK_SIZE) == 0)) {
            alfs_buffer_releaseBlock (dirBlockBuffer, 0);
         }
     }

		/* name not found, set extra space identifiers and return 0 */
     *dirBlockBufferP = freeBlockBuffer;
     *direntP = freeDirent;

     return (0);
}


/*************************** alfs_embdir_addEntry ***********************/
/*									*/
/* Add an entry to a directory.  If successful, return NULL.  If name	*/
/*    already exists, return a pointer to the corresponding directory	*/
/*    entry.  No other failures are tolerated.				*/
/*									*/
/* Parameters upon entry:						*/
/*    fsdev -- the file system identifier used for buffer acquisition	*/
/*    dirInode -- the inode for the directory				*/
/*    name -- the name to be associated with the new entry		*/
/*    nameLen -- length of the name (in characters)			*/
/*    contentLen -- the length of the content to be embedded		*/
/*    flags -- flags used during interactions with the buffer manager.	*/
/*             if a new block must be added to the directory, the flags	*/
/*             field for BUFFER_ALLOCSTORE is associated with buffer	*/
/*             "get".  All other fields are associated with the buffer	*/
/*             release (independent of block allocation needs).		*/
/*									*/
/* Parameters upon exit:  same as entry.				*/
/*									*/
/* Changes to state: buffer entries for directory blocks may be		*/
/*                   acquired (will be released before exit).  One	*/
/*                   directory block will be modified if the entry	*/
/*                   addition is successful.  If name conflicts, the	*/
/*                   buffer for the corresponding directory block is	*/
/*                   returned and not released.				*/
/*									*/
/************************************************************************/

int alfs_embdir_addEntry (block_num_t fsdev, dinode_t *dirInode, char *name, int nameLen, int contentLen, int flags, embdirent_t **direntPtr, buffer_t **bufferPtr) {
     buffer_t *dirBlockBuffer;
     embdirent_t *newDirent;
     embdirent_t *prevDirent;
     int extraSpace;
     int entryLen = (nameLen + contentLen + 3 + SIZEOF_EMBDIRENT_T) & 0xFFFFFFFC;
     int preventryLen;
/*
printf ("alfs_embdir_addEntry: adding name (%s)\n", name);
printf ("alfs_embdir_addEntry: nameLen %d, contentLen %d, entryLen %d, inum %x\n", nameLen, contentLen, entryLen, dirInode->dinodeNum);
*/
     assert (nameLen <= ALFS_EMBDIR_MAX_FILENAME_LENGTH);
     assert (entryLen <= ALFS_EMBDIR_SECTOR_SIZE);

     /* find space for the new directory entry and check for existence */

     if (alfs_embdir_getblockforname (fsdev, dirInode, name, nameLen, entryLen, &dirBlockBuffer, &prevDirent, &newDirent, &extraSpace)) {
			/* directory entry is already present */
/*
printf ("name conflicts\n");
*/
         *direntPtr = prevDirent;
         *bufferPtr = dirBlockBuffer;
         return(1);
     }

     if (dirBlockBuffer) {
			/* space for the entry is available */
         if (prevDirent == NULL) {
			/* however, must compress the block first */
             prevDirent = alfs_embdir_compressEntries ((char *)newDirent, ALFS_EMBDIR_SECTOR_SIZE);
         }
			/* minimize space used by immediately previous entry */
         extraSpace = (prevDirent->type == (char) 0) ? prevDirent->entryLen : (prevDirent->entryLen - embdirentsize(prevDirent));
/*
printf ("found spot: extraSpace %d, preventryLen %d, prevType %x, prevDirent %x, blockstart %x\n", extraSpace, prevDirent->entryLen, prevDirent->type, (u_int) prevDirent, (u_int) dirBlockBuffer->buffer);
*/
         prevDirent->entryLen -= extraSpace;
         preventryLen = prevDirent->entryLen;
         newDirent = (embdirent_t *) ((char *) prevDirent + prevDirent->entryLen);
         assert (extraSpace >= entryLen);
     } else {
			/* must add a block to the directory */
         assert((alfs_dinode_getLength(dirInode) % BLOCK_SIZE) == 0);
         dirBlockBuffer = alfs_buffer_getBlock (fsdev, dirInode->dinodeNum, (alfs_dinode_getLength(dirInode) / BLOCK_SIZE), (BUFFER_GET | BUFFER_WITM | (flags & BUFFER_ALLOCSTORE)));
         alfs_dinode_setLength(dirInode, (alfs_dinode_getLength(dirInode) + BLOCK_SIZE));
			/* initialize the new directory block */
         alfs_embdir_initDirBlock (dirBlockBuffer->buffer);
			/* the new entry should "own" all of the first "sector" */
         newDirent = (embdirent_t *) dirBlockBuffer->buffer;
         extraSpace = newDirent->entryLen;
         preventryLen = 0;
     }

     /* fill in new dirent */

     bcopy(name, newDirent->name, nameLen);
     newDirent->name[nameLen] = (char) 0;
     newDirent->nameLen = nameLen;
     newDirent->contentLen = contentLen;
     newDirent->entryLen = extraSpace;
     newDirent->preventryLen = preventryLen;
     if ((((int)newDirent - (int) dirBlockBuffer->buffer + extraSpace) % ALFS_EMBDIR_SECTOR_SIZE) != 0) {
		/* reusing prevDirent for nextDirent */
        prevDirent = (embdirent_t *) ((char *)newDirent + extraSpace);
        prevDirent->preventryLen = extraSpace;
     }

     *direntPtr = newDirent;
     *bufferPtr = dirBlockBuffer;
/*
printf ("alfs_embdir_addEntry done\n");
*/
     return (0);
}


/********************* alfs_embdir_removeLink_byEntry *******************/
/*									*/
/* Remove an entry from a directory.  No failure conditions are 	*/
/*     tolerated.							*/
/*									*/
/* Parameters upon entry:						*/
/*    dirent -- pointer to the embdirent_t to be removed (it is assumed	*/
/*              that the directory entry is valid and that the		*/
/*              directory block is held by the caller)			*/
/*    dirBlockBuffer -- buffer for directory block containing dirent	*/
/*									*/
/* Parameters upon exit:  same as entry					*/
/*									*/
/* Changes to state: directory block contents are modified to remove	*/
/*                   the directory entry.				*/
/*									*/
/************************************************************************/

void alfs_embdir_removeLink_byEntry (embdirent_t *dirent, buffer_t *dirBlockBuffer)
{
    dirent->type = (char) 0;

    if (((int)dirent - (int)dirBlockBuffer->buffer) % ALFS_EMBDIR_SECTOR_SIZE) {
        embdirent_t *prevDirent = (embdirent_t *) ((char *)dirent - dirent->preventryLen);
        prevDirent->entryLen += dirent->entryLen;
/*
printf ("prevDirent (%x) length increased by %d (now %d)\n", prevDirent, dirent->entryLen, prevDirent->entryLen);
*/
        if (((int)dirent - (int)dirBlockBuffer->buffer + dirent->entryLen) % ALFS_EMBDIR_SECTOR_SIZE) {
           embdirent_t *nextDirent = (embdirent_t *) ((char *)dirent + dirent->entryLen);
           nextDirent->preventryLen = prevDirent->entryLen;
/*
printf ("nextDirent (%x) preventryLen changed to %d\n", nextDirent, prevDirent->entryLen);
*/
        }
    }
}


/*********************** alfs_embdir_removeLink_byName ******************/
/*									*/
/* Remove a named entry from a directory.  If successful, return 1.	*/
/*    If name not found, return 0.  No other failures are tolerated.	*/
/*									*/
/* Parameters upon entry:						*/
/*    fsdev -- the file system identifier used for buffer acquisition	*/
/*    dirInode -- the inode for the directory				*/
/*    name -- the name to be associated with the new entry		*/
/*    nameLen -- length of the name (in characters)			*/
/*    flags -- flags used during for the buffer release of the		*/
/*             directory block that had an entry removed (if any).	*/
/*									*/
/* Parameters upon exit:  same as entry					*/
/*									*/
/* Changes to state: buffer entries for directory blocks may be		*/
/*                   acquired (will be released before exit).  One	*/
/*                   directory block will be modified if the entry	*/
/*                   removal is successful.				*/
/*									*/
/************************************************************************/

int alfs_embdir_removeLink_byName (block_num_t fsdev, dinode_t *dirInode, char *name, int nameLen, int flags) {
    buffer_t *dirBlockBuffer;
    embdirent_t *dirent;
/*
printf ("alfs_embdir_removeLink_byName: name %s, nameLen %d\n", name, nameLen);
*/
    /* find the dirent for name we're removing */

    if ((dirent = alfs_embdir_lookupname (fsdev, dirInode, name, nameLen, &dirBlockBuffer)) == NULL) {
		/* name not found */
        return (0);
    }

    /* invalidate entry */

    alfs_embdir_removeLink_byEntry (dirent, dirBlockBuffer);

    alfs_buffer_releaseBlock (dirBlockBuffer, (flags | BUFFER_DIRTY));

    return (1);
}


int alfs_embdir_isEmpty (block_num_t fsdev, dinode_t *dirInode, int *dotdot)
{
   int currPos = 0;
   int dirBlockLength;
   int dirBlockNumber = 0;
   embdirent_t *dirent;
   buffer_t *dirBlockBuffer;
   char *dirBlock;
   int i;

   assert (dirInode);
   assert (dotdot);

   *dotdot = 0;

   /* search through the directory looking for the name */

   while (currPos < alfs_dinode_getLength(dirInode)) {
      dirBlockLength = min((alfs_dinode_getLength(dirInode) - currPos), BLOCK_SIZE);
      dirBlockBuffer = alfs_buffer_getBlock (fsdev, dirInode->dinodeNum, dirBlockNumber, BUFFER_READ);
      dirBlockNumber++;
      dirBlock = dirBlockBuffer->buffer;
      for (i=0; i < dirBlockLength; i += dirent->entryLen) {
         dirent = (embdirent_t *) (dirBlock + i);
         if ((dirent->type != (char) 0) && (dirent->nameLen == 2) && (dirent->name[0] == '.') && (dirent->name[1] == '.')) {
            *dotdot = *((unsigned int *) embdirentcontent(dirent));
         } else if ((dirent->type != (char) 0) && ((dirent->nameLen > 1) || (dirent->name[0] != '.'))) {
            /* name found */
            alfs_buffer_releaseBlock (dirBlockBuffer, 0);
            return (0);
         }
      }
      alfs_buffer_releaseBlock (dirBlockBuffer, 0);
      currPos += dirBlockLength;
   }

   return (1);		/* directory empty */
}


void alfs_embdir_initDirRaw (dinode_t *dirInode, char type, block_num_t parentInodeNumber, char *dirbuf)
{
    embdirent_t *dirent;
    embdirent_t *dirent2;
    int entryLen;
/*
printf ("alfs_embdir_initDirRaw: inodenum %x, dirblock %d\n", dirInode->dinodeNum, dirInode->directBlocks[0]);
*/
    /* add the "." entry */

    alfs_dinode_setLength(dirInode, BLOCK_SIZE);
    alfs_embdir_initDirBlock (dirbuf);
    dirent = (embdirent_t *) dirbuf;
    dirent->type = type;
    dirent->nameLen = 1;
    dirent->name[0] = '.';
    dirent->name[1] = 0;
    dirent->preventryLen = 0;
    dirent->contentLen = sizeof (unsigned int);
    *((unsigned int *) embdirentcontent(dirent)) = dirInode->dinodeNum;

    /* add the ".." entry, if appropriate.  Note that this uses the extra */
    /* space originally contained in the "." entry.                       */

    if (parentInodeNumber) {
        entryLen = dirent->entryLen;
        dirent->entryLen = embdirentsize (dirent);
        dirent2 = (embdirent_t *) ((char *) dirent + embdirentsize(dirent));
        dirent2->preventryLen = dirent->entryLen;
        dirent2->type = type;
        dirent2->nameLen = 2;
        dirent2->entryLen = entryLen - dirent2->preventryLen;
        dirent2->name[0] = '.';
        dirent2->name[1] = '.';
        dirent2->name[2] = 0;
        dirent2->contentLen = sizeof (unsigned int);
        *((unsigned int *) embdirentcontent(dirent2)) = parentInodeNumber;
    }
}


/************************** alfs_embdir_initDir *************************/
/*									*/
/* initialize a directory file to contain a "." entry linked to itself	*/
/*     and, optionally, a ".." entry.  If successful, returns OK.  If	*/
/*     the directory file is not empty, returns NOTOK.  No other	*/
/*     failure conditions are tolerated.				*/
/*									*/
/* Parameters upon entry:						*/
/*    fsdev -- the file system identifier used for buffer acquisition	*/
/*    dirInode -- the inode for the directory				*/
/*    type -- type field to be associated with the added entries	*/
/*    parentInodeNumber -- the inode number to be assocatiated with the	*/
/*                         ".." entry.  If 0, no ".." entry is added.	*/
/*    flags -- flags used during interactions with the buffer manager.	*/
/*             the flags field for BUFFER_ALLOCSTORE is associated with	*/
/*             the buffer "get" of the new directory block.  All other	*/
/*             fields are associated with the buffer release.		*/
/*									*/
/* Parameters upon exit:  same as entry.				*/
/*									*/
/* Changes to state: a buffer entry for the new directory block will be	*/
/*                   acquired, modified and released.			*/
/*									*/
/************************************************************************/

int alfs_embdir_initDir(block_num_t fsdev, dinode_t *dirInode, char type, block_num_t parentInodeNumber, int flags)
{
    buffer_t *dirBlockBuffer;
/*
printf ("alfs_embdir_initDir: inodenum %x\n", dirInode->dinodeNum);
*/
    if (alfs_dinode_getLength(dirInode) != 0) {
        return(NOTOK);
    }

    /* get the new directory block */

    dirBlockBuffer = alfs_buffer_getBlock (fsdev, dirInode->dinodeNum, 0, (BUFFER_GET | BUFFER_WITM | (flags & BUFFER_ALLOCSTORE)));
    assert (dirBlockBuffer);

    alfs_embdir_initDirRaw (dirInode, type, parentInodeNumber, dirBlockBuffer->buffer);

    alfs_buffer_releaseBlock (dirBlockBuffer, ((flags & ~BUFFER_ALLOCSTORE) | BUFFER_DIRTY));
    return(OK);
}



/************************* alfs_embdir_compressEntries ******************/
/*									*/
/* compress the space in a region of a directory by moving entries	*/
/*    forward and combining any scattered extra space.  returns a	*/
/*    pointer to the last entry in the region, which contains all of	*/
/*    the region's extra space.  No error conditions are tolerated.	*/
/*									*/
/* Parameters upon entry:						*/
/*    dirBlock -- points to the region to be compressed.		*/
/*    dirBlockLength -- length of the region to be compressed.		*/
/*									*/
/* Parameters upon exit:  same as entry.				*/
/*									*/
/* Changes to state: the contents of the region may be modified		*/
/*                   physically.  The logical directory state should	*/
/*                   remain unchanged.					*/
/*									*/
/************************************************************************/

static embdirent_t *alfs_embdir_compressEntries (char *dirBlock, int dirBlockLength)
{
   embdirent_t *dirent = NULL;
   embdirent_t *nextDirent;
   int extraSpace;
   int i;

   /* examine/manipulate each directory entry in turn */

   for (i=0; i<dirBlockLength; i+=dirent->entryLen) {
      dirent = (embdirent_t *) (dirBlock + i);
      extraSpace = dirent->entryLen - embdirentsize(dirent);
      assert(extraSpace >= 0);

      /* if entry has extra space and it is not the last entry, then abut the */
      /* next entry to it and "give" the extra space to same.                 */

      if ((extraSpace) && (dirBlockLength > (i + dirent->entryLen))) {
         nextDirent = (embdirent_t *) ((char *) dirent + dirent->entryLen);
         dirent->entryLen -= extraSpace;
         nextDirent->entryLen += extraSpace;
         nextDirent->preventryLen = dirent->entryLen;
         bcopy((char *) nextDirent, ((char *) dirent + dirent->entryLen), embdirentsize(nextDirent));
      }
   }

   return (dirent);
}


/*************************** alfs_embdir_initDirBlock *******************/
/*									*/
/* initialize the contents of a new directory block.  In particular,	*/
/*    this is used to set up the "sectors" that make up the block.	*/
/*    No value is returned and no errors conditions are tolerated.	*/
/*									*/
/* Parameters upon entry:						*/
/*    dirBlock -- points to the new directory block.			*/
/*									*/
/* Parameters upon exit:  same as entry.				*/
/*									*/
/* Changes to state: the contents of the new block will be modified.	*/
/*									*/
/************************************************************************/

static void alfs_embdir_initDirBlock (char *dirBlock)
{
   embdirent_t *dirent = (embdirent_t *) dirBlock;
   int i;

   for (i=0; i<(BLOCK_SIZE/ALFS_EMBDIR_SECTOR_SIZE); i++) {
      dirent->type = (char) 0;
      dirent->entryLen = ALFS_EMBDIR_SECTOR_SIZE;
      dirent->preventryLen = 0;
      dirent = (embdirent_t *) ((char *) dirent + ALFS_EMBDIR_SECTOR_SIZE);
   }
}


int alfs_embdir_initmetablks ()
{
   return (ALFS_EMBDIR_DIRNUMBLOCKS);
}


unsigned int alfs_embdir_initDisk (cap2_t fscap, block_num_t fsdev, int blkno, int rootdirno, int rootinum)
{
   unsigned int initMap [(BLOCK_SIZE / sizeof (unsigned int))];
   int i;

   bzero ((char *) initMap, BLOCK_SIZE);
   assert (rootdirno < (BLOCK_SIZE / sizeof (unsigned int)));
   assert ((rootinum & 0xFFFF0000) == 0);
   initMap[rootdirno] = rootinum;    /* for the root directory */

   for (i=blkno; i<(ALFS_EMBDIR_DIRNUMBLOCKS+blkno); i++) {
      struct buf buft;
      assert (alfs_alloc_allocBlock (fsdev, i) == 0);
      ALFS_DISK_WRITE (&buft, ((char *) initMap), fsdev, i);
      initMap[rootdirno] = 0;
   }
   return (blkno + ALFS_EMBDIR_DIRNUMBLOCKS);
}


unsigned int alfs_embdir_mountDisk (cap2_t fscap, block_num_t fsdev, int blkno)
{
   struct buf buft;
   int i;
   for (i=0; i<ALFS_EMBDIR_DIRNUMBLOCKS; i++) {
      ALFS_DISK_READ (&buft, ((char *) alfs_embdir_dirNumMap + (i * BLOCK_SIZE)), fsdev, (i+blkno));
   }
   return (blkno + ALFS_EMBDIR_DIRNUMBLOCKS);
}


unsigned int alfs_embdir_unmountDisk (cap2_t fscap, block_num_t fsdev, int blkno)
{
   struct buf buft;
   int i;
   for (i=0; i<ALFS_EMBDIR_DIRNUMBLOCKS; i++) {
      ALFS_DISK_WRITE (&buft, ((char *) alfs_embdir_dirNumMap + (i * BLOCK_SIZE)), fsdev, (i+blkno));
   }
   return (blkno + ALFS_EMBDIR_DIRNUMBLOCKS);
}


dinode_t *alfs_embdir_getInode (unsigned int fsdev, unsigned int effdinodeNum, unsigned int dinodeNum, buffer_t **dirBlockBufferPtr)
{
   buffer_t *dirBlockBuffer;
   unsigned int sectno = (((effdinodeNum & 0x0000FFFF) >> 3) - 1) % (BLOCK_SIZE/ALFS_EMBDIR_SECTOR_SIZE);
   embdirent_t *dirent;
   int runlength = 0;
   dinode_t *dinode;
   
   dirBlockBuffer = alfs_buffer_getBlock (fsdev, alfs_embdir_dirDInodeNum(effdinodeNum), alfs_embdir_entryBlkno(effdinodeNum), BUFFER_READ);
   dirent = (embdirent_t *) (dirBlockBuffer->buffer + (sectno * ALFS_EMBDIR_SECTOR_SIZE));
   while (runlength < ALFS_EMBDIR_SECTOR_SIZE) {
      if (dirent->type == 1) {
         dinode = (dinode_t *) embdirentcontent (dirent);
         if (dinode->dinodeNum == dinodeNum) {
            *dirBlockBufferPtr = dirBlockBuffer;
            return (dinode);
         }
      }
      runlength += dirent->entryLen;
      dirent = (embdirent_t *) ((char *) dirent + dirent->entryLen);
   }
   alfs_buffer_releaseBlock (dirBlockBuffer, 0);
   return (NULL);
}


unsigned int alfs_embdir_transDirNum (unsigned int dinodeNum)
{
/*
printf ("alfs_embdir_transDirNum %d (%x) ==> DirNum[%d] === %d (%x)\n", dinodeNum, dinodeNum, (((dinodeNum) >> 16) & 0x0000FFFF), alfs_embdir_dirNumMap[(((dinodeNum) >> 16) & 0x0000FFFF)], alfs_embdir_dirNumMap[(((dinodeNum) >> 16) & 0x0000FFFF)]);
*/
   assert ((((dinodeNum) >> 16) & 0x0000FFFF) <= ALFS_EMBDIR_MAXDIRNUM);
   return (alfs_embdir_dirNumMap[(((dinodeNum) >> 16) & 0x0000FFFF)]);
}


unsigned int alfs_embdir_allocDirNum (unsigned int dinodeNum)
{
   unsigned int i;
   for (i=alfs_embdir_lastDirNum; i<ALFS_EMBDIR_MAXDIRNUM; i++) {
      if (alfs_embdir_dirNumMap[i] == 0) {
         break;
      }
   }
   if (i == ALFS_EMBDIR_MAXDIRNUM) {
      for (i=1; i<alfs_embdir_lastDirNum; i++) {
         if (alfs_embdir_dirNumMap[i] == 0) {
            break;
         }
      }
      if (i == alfs_embdir_lastDirNum) {
         return (-1);
      }
   }
   alfs_embdir_dirNumMap[i] = dinodeNum;
   alfs_embdir_lastDirNum = i;
/*
printf ("alfs_embdir_allocDirNum %d to %d (%x)\n", i, dinodeNum, dinodeNum);
*/
   return (i << 16);
}


void alfs_embdir_deallocDirNum (unsigned int dirnum)
{
   assert (dirnum < ALFS_EMBDIR_MAXDIRNUM);
/*
printf ("alfs_embdir_deallocDirNum %d from %d (%x)\n", dirnum, alfs_embdir_dirNumMap[dirnum], alfs_embdir_dirNumMap[dirnum]);
*/
   alfs_embdir_dirNumMap[dirnum] = 0;
}

