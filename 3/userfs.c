#include "userfs.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DEBUG 0 // поменяй на 0 чтобы выключить

#if DEBUG
#define DBG(fmt, ...)                                                          \
  printf("%s:%d - " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define DBG(fmt, ...)
#endif
enum {
  BLOCK_SIZE = 512, // 2^9

  MAX_FILE_SIZE = 1024 * 1024 * 100, //
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
  /** Block memory. */
  char *memory;
  /** How many bytes are occupied. */
  int occupied;
  /** Next block in the file. */
  struct block *next;
  /** Previous block in the file. */
  struct block *prev;

  /* PUT HERE OTHER MEMBERS */
};

struct file {
  /** Double-linked list of file blocks. */
  struct block *block_list;
  /**
   * Last block in the list above for fast access to the end
   * of file.
   */
  struct block *last_block;
  /** How many file descriptors are opened on the file. */
  int refs;
  /** File name. */
  char *name;
  /** Files are stored in a double-linked list. */
  struct file *next;
  struct file *prev;

  /* PUT HERE OTHER MEMBERS */
  bool delete;
};

/** List of all files. */
static struct file *file_list = NULL;

void DBG_PRINT_ALL_FILENAMES() {
  DBG("start print all names");
  for (struct file *curr = file_list; curr != NULL; curr = curr->next) {
    DBG("\t file:%s", curr->name);
  }
  DBG("end print all names");
}
struct filedesc {
  struct file *file;

  int *offset;
  /* PUT HERE OTHER MEMBERS */
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;

// you must be sure there is file_descriptors is null or u lose information
// about old fd's
void alloc_file_descr_array(int with_capacity) {

  file_descriptors =
      (struct filedesc **)calloc(with_capacity, sizeof(struct filedesc *));
  file_descriptor_capacity = with_capacity;
}
// new_capacity should be bigger than fd_count
// you must be sure what fd array already allocated
void resize_file_descr_array(int new_capacity) {
  DBG("resize filedescr_arr");
  struct filedesc **old_fds = file_descriptors;
  alloc_file_descr_array(new_capacity);
  for (int i = 0; i < file_descriptor_capacity; i++) {
    file_descriptors[i] = old_fds[i];
  }
  file_descriptor_capacity = new_capacity;
  free((void *)old_fds);
}
// before use this function you must be sure what is file_descriptors array is
// not null
// realloc file_desc array if count == capacity with 2x capacity from old
int find_free_fd() {
  for (int i = 0; i < file_descriptor_capacity; i++) {
    if (file_descriptors[i] == NULL) {
      return i;
    } else {
      // DBG("fd[%d] file with filename%s", i, file_descriptors[i]->file->name);
    }
  }
  int old_capacity = file_descriptor_capacity;
  // if not found free fd should realloc array and get new fd;
  resize_file_descr_array(old_capacity * 2);
  // return old_capacity because fd_arr[old_capacity] must be NULL;
  // ez optimization?
  return old_capacity;
}
enum ufs_error_code ufs_errno() { return ufs_error_code; }

// return fd or <0 if no fd for this file
int get_file_fd_if_exists(const char *filename) {
  DBG("try to find fd for file:%s", filename);
  if (file_descriptors == NULL) {
    DBG("file_descriptors == NULL");
    return -1;
  }
  DBG("file_desc_cap:%d", file_descriptor_capacity);
  for (int i = 0; i < file_descriptor_capacity; i++) {
    if (file_descriptors[i] != NULL) {
      struct file *file = file_descriptors[i]->file;
      DBG("found fd for file: %s", file->name);
      if (strcmp(file->name, filename) == 0) {
        return i;
      }
    }
  }
  return -1;
}
// also create new block
struct file *alloc_new_file(const char *filename) {
  struct block *new_block = calloc(1, sizeof(struct block));
  new_block->memory = malloc(BLOCK_SIZE);

  char *new_filename = malloc(strlen(filename) + 1);
  strcpy(new_filename, filename);

  struct file *new_file = malloc(sizeof(struct file));
  new_file->block_list = new_block;
  new_file->last_block = new_block;
  new_file->refs = 0;
  new_file->name = new_filename;
  new_file->next = NULL;
  new_file->prev = NULL;
  new_file->delete = false;
  return new_file;
}
// return's file with this name or NULL
struct file *find_file_by_filename(const char *filename) {
  struct file *file = NULL;
  for (struct file *curr = file_list; curr != NULL; curr = curr->next) {
    if (strcmp(curr->name, filename) == 0) {
      file = curr;
      DBG("find file with needed name(just check for cycle error)");
      // when test's will pass add here return (useless iterations)
    }
  }
  return file;
}
void free_block(struct block *block) {
  free(block->memory);
  free(block);
}
// don't free head memory (but set occupied = 0) and free all block but not head
void reset_blocklist(struct block *head) {
  DBG("resetting blocklist");
  head->occupied = 0;

  struct block *curr = head->next;
  struct block *to_free = NULL;
  while (curr != NULL) {
    to_free = curr;
    curr = curr->next;
    free_block(to_free);
  }
  head->next = NULL;
  head->prev = NULL;
}
void delete_file(struct file *file) {
  if (file_list == file) {
    file_list = file->next;
  }
  reset_blocklist(file->block_list);
  free_block(file->block_list);
  free(file->name);
  free(file);
}
void DBG_read_file_content(struct file *to_read) {
  DBG("printing content of file\n\t");
  for (struct block *curr = to_read->block_list;
       curr != NULL && curr->occupied != 0; curr = curr->next) {
    for (int i = 0; i < curr->occupied; i++) {
      // DBG("%c", curr->memory[i]);
    }
  }
  DBG("content of file printed\n");
}
void create_and_add_to_filelist_new_file(const char *filename) {

  DBG("should create new file");
  // check now exists file with such filename or no
  struct file *new_file = find_file_by_filename(filename);
  if (new_file == NULL) {
    DBG("\t file with such name not exists,creating a new one");

    new_file = alloc_new_file(filename);
    if (file_list == NULL) {
      DBG("file list is null, file_list=new_file");
      file_list = new_file;
    } else {
      struct file *curr = file_list;
      for (; curr->next != NULL; curr = curr->next) {
      }
      curr->next = new_file;
      new_file->prev = curr;

      DBG("added to file_list new file");
    }
  } else {
    // zeroize file data
    reset_blocklist(new_file->block_list);
    // already freed new_file->last_block;
    new_file->last_block = new_file->block_list;
  }
}
int ufs_open(const char *filename, int flags) {
  // DBG_PRINT_ALL_FILENAMES();
  if (file_descriptors == NULL) {
    DBG("fd_arr is null;allocating");
    alloc_file_descr_array(BLOCK_SIZE);
    DBG("there should not be existing fd structs");
    find_free_fd();
  }
  DBG("start ufs_open ");
  if ((flags & UFS_CREATE) != 0) {
    create_and_add_to_filelist_new_file(filename);
  }
  struct file *curr_file = NULL;
  for (struct file *curr = file_list; curr != NULL; curr = curr->next) {
    // DBG("found filename:%s", curr->name);
    if (strcmp(filename, curr->name) == 0) {
      curr_file = curr;
      DBG("curr file = %s", filename);
      if (curr == file_list) {
        DBG("curr file is a head");
      }
      break;
    }
  }
  if (curr_file == NULL) {
    ufs_error_code = UFS_ERR_NO_FILE;
    return -1;
  }
  DBG("founded file");

  struct filedesc *fd = malloc(sizeof(struct filedesc));
  fd->file = curr_file;
  fd->offset = malloc(sizeof(int));
  *fd->offset = 0;
  curr_file->refs++;

  int new_fd = find_free_fd();
  file_descriptors[new_fd] = fd;
  file_descriptor_count++;
  DBG("created fd for file:%s fd:%d", filename, new_fd);
  // DBG_PRINT_ALL_FILENAMES();
  return new_fd;
}
struct block *alloc_new_block() {
  struct block *new_block = malloc(sizeof(struct block));
  new_block->memory = malloc(BLOCK_SIZE);
  new_block->next = NULL;
  new_block->prev = NULL;
  new_block->occupied = 0;
  return new_block;
}
struct block *get_of_allocate_new_block(struct block *curr_block) {

  if (curr_block->next == NULL) {
    curr_block->next = alloc_new_block();
    struct block *new_block = curr_block->next;
    new_block->prev = curr_block;
    curr_block = curr_block->next;
  } else {
    curr_block = curr_block->next;
  }
  return curr_block;
}
// just iter blocks, if you need offset IN block do it manually
struct block *get_needed_block(struct block *head_block, int offset) {
  struct block *curr = head_block;
  // we should go to place where ptr stopped;
  while (offset >= BLOCK_SIZE) {
    curr = curr->next;

    offset -= BLOCK_SIZE;
  }
  return curr;
}
ssize_t write_to_file(struct file *to_write, int *ptr_offset, const char *buf,
                      size_t size) {
  DBG("start writing info in file");
  if (*ptr_offset + size > MAX_FILE_SIZE) {
    ufs_error_code = UFS_ERR_NO_MEM;
    return -1;
  }
  size_t remain = size;
  // add ptr offset use
  struct block *curr_block =
      get_needed_block(to_write->block_list, *ptr_offset);
  // получили блок на котором остановились
  int block_offset = *ptr_offset % BLOCK_SIZE;
  // заняли место с которого надо писать
  int total_written = 0;
  while (remain != 0) {
    size_t available_in_block = BLOCK_SIZE - block_offset;
    int howmuch_write =
        (remain < available_in_block) ? remain : available_in_block;
    memcpy(curr_block->memory + block_offset, buf + total_written,
           howmuch_write);
    // записали в блок до места оффсет+хавмачврайт
    remain -= howmuch_write;
    total_written += howmuch_write;
    // curr_block->occupied += howmuch_write;
    if (curr_block->occupied < block_offset + howmuch_write) {
      curr_block->occupied = block_offset + howmuch_write;
    }
    DBG("block occupied at %d bytes", curr_block->occupied);
    if (curr_block->occupied == BLOCK_SIZE) {
      curr_block = get_of_allocate_new_block(curr_block);
      block_offset = 0;
      DBG("getting new block");
    } else {
      block_offset += howmuch_write;
    }
  }

  DBG("try to dereference ptr_offset");
  *ptr_offset += total_written;
  DBG_read_file_content(to_write);
  DBG("finished writing in file");
  return total_written;
}
ssize_t ufs_write(int fd, const char *buf, size_t size) {
  if (fd < 0 || fd >= file_descriptor_capacity) {
    ufs_error_code = UFS_ERR_NO_FILE;
    return -1;
  }
  if (file_descriptors[fd] == NULL) {
    ufs_error_code = UFS_ERR_NO_FILE;
    return -1;
  }
  struct file *to_write = file_descriptors[fd]->file;
  return write_to_file(to_write, file_descriptors[fd]->offset, buf, size);
  (void)fd;
  (void)buf;
  (void)size;
  ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
  return -1;
}
ssize_t read_file(struct file *to_read, int *ptr_offset, char *buf,
                  size_t size) {
  DBG("start read file");
  // DBG_read_file_content(to_read);
  size_t remain = size;
  struct block *curr_block = get_needed_block(to_read->block_list, *ptr_offset);
  DBG("\t in this block occupied %d bytes", curr_block->occupied);
  int block_offset = *ptr_offset % BLOCK_SIZE;
  // we should go to place where ptr stopped;
  int total_readed = 0;
  while (remain != 0 && curr_block != NULL) {
    if (curr_block == NULL) {
      return 0;
    }
    size_t available_read = curr_block->occupied - block_offset;
    // assert(available_read >= 0);
    if (available_read == 0) {
      curr_block = curr_block->next;
      block_offset = 0;
      continue;
    }

    size_t to_read = (remain < available_read) ? remain : available_read;
    DBG("\t trying to read %zu bytes", to_read);
    memcpy(buf + total_readed, curr_block->memory + block_offset, to_read);
    block_offset += to_read;
    total_readed += to_read;
    remain -= to_read;
  }
  DBG("finished read file code %d", total_readed);
  *ptr_offset += total_readed;
  return total_readed;
}
ssize_t ufs_read(int fd, char *buf, size_t size) {

  if (fd < 0 || fd >= file_descriptor_capacity) {
    ufs_error_code = UFS_ERR_NO_FILE;
    return -1;
  }
  if (file_descriptors[fd] == NULL) {
    ufs_error_code = UFS_ERR_NO_FILE;
    return -1;
  }
  struct file *to_read = file_descriptors[fd]->file;
  return read_file(to_read, file_descriptors[fd]->offset, buf, size);

  /* IMPLEMENT THIS FUNCTION */
  (void)fd;
  (void)buf;
  (void)size;
  ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
  return -1;
}

int ufs_close(int fd) {
  DBG("closing file\n");
  if (fd < 0 || fd >= file_descriptor_capacity) {
    ufs_error_code = UFS_ERR_NO_FILE;
    return -1;
  }
  if (file_descriptors[fd] == NULL) {
    DBG("trying to close unexisted fd %d", fd);
    ufs_error_code = UFS_ERR_NO_FILE;
    return -1;
  }
  struct filedesc *fd_struct = file_descriptors[fd];
  struct file *file = fd_struct->file;
  DBG("file have %d refs", file->refs);
  free(fd_struct->offset);
  // no more fd should destroy fd and fd_arr[fd] = NULL;
  free(fd_struct);
  file_descriptors[fd] = NULL;
  file_descriptor_count--;
  if (--(file->refs) == 0) {
    DBG("\t free fd_struct with fd:%d", fd);

    if (file->delete) {
      delete_file(file);
    }
  }
  DBG("end closing file");
  return 0;
  /* IMPLEMENT THIS FUNCTION */
  (void)fd;
  ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
  return -1;
}

int ufs_delete(const char *filename) {
  struct file *to_delete = find_file_by_filename(filename);
  if (to_delete == NULL) {
    ufs_error_code = UFS_ERR_NO_FILE;
    return -1;
  }
  to_delete->delete = true;
  if (to_delete->refs == 0) {
    DBG("refs_count == 0 so we can now delete file");
    // deleting file
    struct file *prev_file = to_delete->prev;
    struct file *next_file = to_delete->next;
    if (prev_file != NULL) {
      prev_file->next = next_file;
    }
    if (next_file != NULL) {
      next_file->prev = prev_file;
    }
    delete_file(to_delete);
    return 0;
  }

  // handle this file delete in ufs_close();
  // first delete from file_list;

  if (to_delete == file_list) {
    file_list = file_list->next;
    return 0;
  }
  struct file *prev_file = to_delete->prev;
  struct file *next_file = to_delete->next;
  if (prev_file != NULL) {
    prev_file->next = next_file;
  }
  if (next_file != NULL) {
    next_file->prev = prev_file;
  }

  assert(file_list != to_delete);
  return 0;
  /* IMPLEMENT THIS FUNCTION */
  (void)filename;
  ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
  return -1;
}

#if NEED_RESIZE

int ufs_resize(int fd, size_t new_size) {
  /* IMPLEMENT THIS FUNCTION */
  (void)fd;
  (void)new_size;
  ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
  return -1;
}

#endif

void ufs_destroy(void) {}
