#ifndef _H_LIBSFTP2_H
#define _H_LIBSFTP2_H

#include <stdlib.h>
#include <sys/statvfs.h>
#include <dirent.h>

struct sftp;
struct sftp_dir;
struct sftp_fd;

#define ADDR_MAX 1024
#define PORT_MAX 10

struct volume
{
  char name[NAME_MAX];
  char root[PATH_MAX];
  char addr[ADDR_MAX];
  char port[PORT_MAX];
  char public_key[PATH_MAX];
  char private_key[PATH_MAX];
  char username[NAME_MAX];
  char passphrase[NAME_MAX];
};

struct sftp *
sftp_init (struct volume *vol, const char *mount_point);

void
sftp_destroy (struct sftp *s);

int
sftp_stat (struct sftp *s, const char *path, struct stat *buf);

int
sftp_fstat (struct sftp_fd *fd, struct stat *buf);

int
sftp_lstat (struct sftp *s, const char *path, struct stat *buf);

ssize_t
sftp_readlink (struct sftp *s, const char *path, char *buf, size_t bufsize);

ssize_t
sftp_realpath (struct sftp *s, const char *path, char *buf, size_t bufsize);

struct sftp_fd *
sftp_open (struct sftp *s, const char *path, int flags, mode_t mode);

int
sftp_close (struct sftp_fd * fd);

int
sftp_read (struct sftp_fd *fd, void *buf, size_t nbyte, off_t offset);

int
sftp_statvfs (struct sftp *s, const char *path, struct statvfs *buf);

int
sftp_fstatvfs (struct sftp_fd *fd, struct statvfs *buf);

struct sftp_dir *
sftp_opendir (struct sftp *s, const char *path);

struct dirent *
sftp_readdir (struct sftp_dir *dir);

int
sftp_closedir (struct sftp_dir *dir);

#endif
