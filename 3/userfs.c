#include "userfs.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define DEBUG 1 // поменяй на 0 чтобы выключить

#if DEBUG
#define DBG(fmt, ...)                                                          \
  printf("%s:%d - " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define DBG(fmt, ...) // ничего не делаем
#endif
enum {
  BLOCK_SIZE = 512,
  MAX_FILE_SIZE = 1024 * 1024 * 100,
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
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc {
  struct file *file;

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
  struct filedesc **old_fds = file_descriptors;
  alloc_file_descr_array(new_capacity);
  for (int i = 0; i < new_capacity; i++) {
    file_descriptors[i] = old_fds[i];
  }
  file_descriptor_capacity = new_capacity;
  free((void *)old_fds);
}
// before use this function you must be sure what is file_descriptors array is
// not null
// realloc file_desc array if count == capacity with 2x capacity from old
int find_free_fd() {
  for (int i = 0; i < file_descriptor_capacity &&
                  file_descriptor_count != file_descriptor_capacity;
       i++) {
    if (file_descriptors[i] == NULL) {
      return i;
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
  if (file_descriptors == NULL) {
    return -1;
  }
  for (int i = 0; i < file_descriptor_capacity; i++) {
    if (file_descriptors[i] != NULL) {

      struct file *file = file_descriptors[i]->file;
      if (strcmp(file->name, filename) == 0) {
        return i;
      }
    }
  }
  return -1;
}
// also create new block
struct file *create_new_file(const char *filename) {
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
  return new_file;
}
// return's file with this name or NULL
struct file *find_file_by_filename(const char* filename) {
  struct file *file = NULL;
  for (struct file *curr = file_list; curr != NULL; curr = curr->next) {
    if (strcmp(curr->name, filename) == 0) {
      file = curr;
    }
  }
  return file;
  
}
int ufs_open(const char *filename, int flags) {
  DBG("start ufs_open ");
  if ((flags & UFS_CREATE) != 0) {
    // а что происходит если мы два раза подряд создаем файл
    // с одним и тем же именем?
    // я создаю новый файл с таким же именем
    // это косяк
    // возможное решение просто обнулять записанное в изначальном?
    DBG("should create new file");
    
    struct file *new_file = create_new_file(filename);

    if (file_list == NULL) {
      DBG("file list is null, file_list=new_file");
      file_list = new_file;
    } else {
      // check what if we already have file with such name

      // podozritelno must check UNSAFE
      // // need to iter on file_list and add file to list
      // for (struct file *curr = file_list->next; curr != NULL;
      //      curr = curr->next) {
      //   curr->next = new_file;
      //   new_file->prev = curr;
      // }

      struct file *curr = file_list;
      for (; curr->next != NULL; curr = curr->next) {
      }
      curr->next = new_file;
      new_file->prev = curr;

      // END UNSAFE
      DBG("added to file_list new file");
    }
  }
  struct file *curr_file = NULL;
  for (struct file *curr = file_list; curr != NULL; curr = curr->next) {
    DBG("found filename:%s", curr->name);
    if (strcmp(filename, curr->name) == 0) {
      curr_file = curr;
      break;
    }
  }
  if (curr_file == NULL) {
    ufs_error_code = UFS_ERR_NO_FILE;
    return -1;
  }
  DBG("founded file");
  if (get_file_fd_if_exists(filename) >= 0) {
    curr_file->refs++;
    return get_file_fd_if_exists(filename);
  }

  struct filedesc *fd = malloc(sizeof(struct filedesc));
  fd->file = curr_file;
  curr_file->refs++;

  int new_fd = find_free_fd();
  file_descriptors[new_fd] = fd;
  file_descriptor_count++;
  return new_fd;

  /* IMPLEMENT THIS FUNCTION */
  (void)filename;
  (void)flags;
  (void)file_list;
  (void)file_descriptors;
  (void)file_descriptor_count;
  (void)file_descriptor_capacity;
  ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
  return -1;
}

ssize_t ufs_write(int fd, const char *buf, size_t size) {
  /* IMPLEMENT THIS FUNCTION */
  (void)fd;
  (void)buf;
  (void)size;
  ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
  return -1;
}

ssize_t ufs_read(int fd, char *buf, size_t size) {
  /* IMPLEMENT THIS FUNCTION */
  (void)fd;
  (void)buf;
  (void)size;
  ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
  return -1;
}

int ufs_close(int fd) {
  if (file_descriptors[fd] == NULL) {
    DBG("trying to close unexisted fd %d", fd);
    ufs_error_code = UFS_ERR_NO_FILE;
    return -1;
  }
  struct filedesc *fd_struct = file_descriptors[fd];
  struct file *file = fd_struct->file;
  if (--(file->refs) == 0) {
    // no more fd should destroy fd and fd_arr[fd] = NULL;
    free(fd_struct);
    file_descriptors[fd] = NULL;
    file_descriptor_count--;
  }
  return 0;
  /* IMPLEMENT THIS FUNCTION */
  (void)fd;
  ufs_error_code = UFS_ERR_NOT_IMPLEMENTED;
  return -1;
}

int ufs_delete(const char *filename) {
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
