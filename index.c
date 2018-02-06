#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/buffer.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DISK_SEPERATE_SIZE 16


/* On-disk inode.
   Must be exactly DISK_SECTOR_SIZE bytes long. */
//struct inode_disk
//  {
//    uint32_t *sector_array[DISK_SEPERATE_SIZE]; /* For indirect block */
//    off_t length;                       /* File size in bytes. */
//    unsigned magic;                     /* Magic number. */
//    uint32_t unused[126 - DISK_SEPERATE_SIZE];               /* Not used. */
//  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* In-memory inode. */
//struct inode 
//  {
//    struct list_elem elem;              /* Element in inode list. */
//    disk_sector_t sector;               /* Sector number of disk location. */
//    int open_cnt;                       /* Number of openers. */
//    bool removed;                       /* True if deleted, false otherwise. */
//    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
//    struct inode_disk data;             /* Inode content. */
//  };  

/* Returns the disk sector that contains byte offset POS within
   INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  int array_first_offset = pos / DISK_SECTOR_SIZE;

  //First sector list: sector_array[254 % 16][?]
  //Second sector list: sector_array[254 % 16][254 / 16] -> one-to-one always
  uint32_t *first_sector_list = inode->data.sector_array[array_first_offset % DISK_SEPERATE_SIZE];
  struct list *final_sector_list = first_sector_list[array_first_offset / DISK_SEPERATE_SIZE];

  struct list_elem *e;
  struct sector_num *sector_num;
  /* Iterate through list to find sector_num */
  for (e = list_begin (final_sector_list); e != list_end (final_sector_list);
       e = list_next (e)) 
    {
      sector_num = list_entry (e, struct sector_num, elem);
      if (sector_num->pos_to_block_num == array_first_offset) 
        {
          return sector_num->sector;
        }
    }
  return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);
  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  int i,j;
  struct list *test_list = NULL;
  //get memory for sector memory
  for(i = 0; i < DISK_SEPERATE_SIZE; i++){
    //disk_inode->sector_array[i] = calloc(DISK_SEPERATE_SIZE, sizeof *struct list );
    disk_inode->sector_array[i] = calloc(DISK_SEPERATE_SIZE, 4);
  }
  //list initialization
  for(i = 0; i < DISK_SEPERATE_SIZE; i++){
    for(j = 0; j < DISK_SEPERATE_SIZE; j++){
      test_list = malloc(sizeof *test_list);
      list_init(test_list);
      disk_inode->sector_array[i][j] = test_list;
    }
  }

  //printf("sector_array content: %08x\n",disk_inode->sector_array[0][1]);
  //Now, list_init(disk_inode->sector_array[i][j])
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = is_dir;
      //printf("sector: %d, length: %d, is_dir: %d\n",sector,disk_inode->length, disk_inode->is_dir);
      disk_inode->parent = ROOT_DIR_SECTOR;
      //printf("ROOT_DIR_SECTOR:%d\n",ROOT_DIR_SECTOR);
      if (sector_allocate (sectors, disk_inode->sector_array, 0))
        {
          disk_write (filesys_disk, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[DISK_SECTOR_SIZE];
              //size_t i;
              struct list *final_sector_list;
              struct list_elem *e;
              struct sector_num *sector_num;

              for (i = 0; i < sectors; i++) 
              {
                final_sector_list = disk_inode->sector_array[i%16][i/16];
                for (e = list_begin (final_sector_list); e != list_end (final_sector_list);
                  e = list_next (e)) 
                {
                  sector_num = list_entry (e, struct sector_num, elem);
                  disk_write (filesys_disk, sector_num->sector, zeros);
                }
              } 
            }
        }
      success = true; 
    } 
  free (disk_inode);
    
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) 
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_back (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  disk_read (filesys_disk, inode->sector, &inode->data);
  inode->is_dir = inode->data.is_dir;
  inode->parent = inode->data.parent;
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          /*
          free_map_release (inode->sector, 1);

          //free all allocated blocks
          struct list *sector_list;
          uint32_t **sector_array = inode->data.sector_array;
          int i;

          for(i = 0;i < DISK_SEPERATE_SIZE * DISK_SEPERATE_SIZE;i++)
          {
            sector_list = sector_array[i%16][i/16];
            if(list_size(sector_list) == 0)
              break;
            struct list_elem *e;
            struct sector_num *sector_num;
            //iterate through list to free all blocks
            for (e = list_begin (sector_list); e != list_end (sector_list); e = list_next (e)) 
              {
                sector_num = list_entry (e, struct sector_num, elem);
                free_map_release (sector_num->sector, 1);
              }
          }*/
          //Todo: free all mallocs
          
        }
      else{
        //check?
        struct list *sector_list;
        uint32_t **sector_array = inode->data.sector_array;
        int i;
        for(i = 0;i < DISK_SEPERATE_SIZE * DISK_SEPERATE_SIZE;i++)
          {
            sector_list = sector_array[i%16][i/16];
            if(list_size(sector_list) == 0)
              break;
            struct list_elem *e;
            struct sector_num *sector_num;
            //iterate through list to free all blocks
            for (e = list_begin (sector_list); e != list_end (sector_list); e = list_next (e)) 
              {
                sector_num = list_entry (e, struct sector_num, elem);
                struct cache_entry *ce = find_cache_entry(sector_num->sector);
                if(ce != NULL)
                { 
                  disk_write(filesys_disk, ce->sector, ce->content);
                }
                //else{
                  //printf("sector: %d, NULL\n",sector_num->sector);
                //}
              }
          }
          disk_write (filesys_disk, inode->sector, &inode->data);
      }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  //printf("read.inode: %08x, size: %08x\n",inode,size);
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  //printf("size:%08x\n",size);
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      //printf("read sector: %d\n",sector_idx);
      int sector_ofs = offset % DISK_SECTOR_SIZE;
      //printf("read on sector %08x\n",sector_idx);
      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      //if(inode == 0xc012080c)
      //  inode->data.length = 2134;
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      //printf("sector_idx: %08x\n",sector_idx);
      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      //printf("sector_idx: %08x, offset: %08x, size: %08x\n",sector_idx,offset,size);
      //write cache's content into buffer
      bool success = cache_read(sector_idx, buffer, sector_ofs, chunk_size, offset, bytes_read);
      if (!success)
        break;
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
    //printf("inode_read_at finished\n");

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  //printf("write! inode: %08x, size: %08x\n",inode,size);
  //printf("size: %d, offset: %d\n",size,offset);
  //printf("inode_write_at started\n");
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;
  //if (inode->is_dir == true)
  //  return 0;
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;
      //printf("write on sector %d, length: %d\n",sector_idx,inode_length (inode));
      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      
      //extend inode
      if(inode_length (inode) < size + offset){
        bool extended = inode_extend (inode, size + offset);
        if (!extended)
          PANIC("inode extended failed\n");
        sector_idx = byte_to_sector (inode, offset);
        //printf("write on sector %08x\n",sector_idx);
      }
      //printf("inode_length: %d\n",inode_length(inode));
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < sector_left ? size : sector_left;
      //printf("write sector: %d\n",sector_idx);
      bool success = cache_write(sector_idx, buffer, sector_ofs, chunk_size, 0, bytes_written);
      if (!success)
        break;
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);
  return bytes_written;
}

bool
sector_allocate (size_t sectors, uint32_t **sector_array, size_t idx)
{
  struct sector_num *sector_num;
  struct list *sector_list;
  size_t i;
  for(i = idx; i < idx + sectors; i++)
  {
    //give me one sector from free_map_allocate
    disk_sector_t sector = free_map_allocate_without_sector (1);
    //printf("sector: %d\n",sector);
    if(sector == -1)
      return false;
    //Put struct sector_num on sector_list
    sector_num = malloc(sizeof *sector_num);
    sector_num->pos_to_block_num = i;
    sector_num->sector = sector;
    //printf("sector_array[0][0]:%08x\n",sector_array[0][0]);
    sector_list = sector_array[i%16][i/16];
    list_push_front(sector_list, &sector_num->elem);
  }
  return true;
}
/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  //ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  if (inode->deny_write_cnt > 0)
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  //printf("inode_length\n");
  return inode->data.length;
}

bool
inode_extend (struct inode *inode, off_t extend_size)
{
  //printf("inode_extend func, extend_size: %d\n",extend_size);
  //allocate one block
  off_t offset = 0;
  while(offset < extend_size){
    //printf("byte_to_sector00: %d, offset: %d\n",byte_to_sector (inode, offset),offset);
    if(byte_to_sector (inode, offset) != -1)
    {
      offset += DISK_SECTOR_SIZE;
      continue;
    }
    disk_sector_t desired_sector = offset / DISK_SECTOR_SIZE;
    uint32_t **sector_array = inode->data.sector_array;
    bool allocated = sector_allocate(1, sector_array, desired_sector);
    if(!allocated)
      return false;
    //set allocated sector to zero
    static char zeros[DISK_SECTOR_SIZE];

    struct list *final_sector_list;
    struct sector_num *sector_num;
    
    final_sector_list = inode->data.sector_array[desired_sector%16][desired_sector/16];
    struct list_elem *e;
    for (e = list_begin (final_sector_list); e != list_end (final_sector_list);
      e = list_next (e)) 
    {
      sector_num = list_entry (e, struct sector_num, elem);
      //printf("sector_num -> pos_to_block_num: %d\n",sector_num->sector);
      if(sector_num -> pos_to_block_num == desired_sector)
        disk_write (filesys_disk, sector_num->sector, zeros);
    } 
    offset += DISK_SECTOR_SIZE;
    //printf("byte_to_sector11: %d, offset: %d\n",byte_to_sector (inode, offset),offset);
  }
  inode->data.length = extend_size;
  return true;
  }
