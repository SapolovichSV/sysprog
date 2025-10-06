#include "userfs.h"
#include <stddef.h>
#include <stdlib.h>

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

  file_descriptors = calloc(with_capacity, sizeof(struct filedesc *));
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

int ufs_open(const char *filename, int flags) {
  if ((flags & UFS_CREATE) != 0) {
    struct block new_block = {
        malloc(BLOCK_SIZE),
        0,
        NULL,
        NULL,
    };
    struct file new_file = {
        &new_block, &new_block, 0, filename, NULL, NULL,
    };
    if (file_list == NULL) {
      file_list = &new_file;
    } else {
      // need to iter on file_list and add file to list
      for (struct file *curr = file_list->next; curr != NULL;
           curr = curr->next) {
        curr->next = &new_file;
        new_file.prev = curr;
      }
    }
  }
  if (file_list == NULL) {
    ufs_error_code = UFS_ERR_NO_FILE;
    return -1;
  }
  struct file *curr_file = file_list;
  while (curr_file->name != filename) {
    if (curr_file->next != NULL) {
      curr_file = curr_file->next;
    } else {
      if (file_list == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
      }
    }
  }
  struct filedesc *fd = malloc(sizeof(struct filedesc));
  fd->file = curr_file;
  curr_file->refs++;

  int id = find_free_fd();

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
