#ifndef _H_LIBSFTP2_H
#define _H_LIBSFTP2_H

#include <sys/stat.h>
#include <dirent.h>

struct sftp;
struct sftp_dir;
struct sftp_fd;

struct sftp *
sftp_init (const char *path, const char *mount_point);

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

int
sftp_mkdir (struct sftp *s, const char *path, mode_t mode);

int
sftp_unlink (struct sftp *s, const char *path);

int
sftp_rmdir (struct sftp *s, const char *path);

int
sftp_symlink (struct sftp *s, const char *path1, const char *path2);

int
sftp_rename (struct sftp *s, const char *old, const char *new);

struct sftp_fd *
sftp_open (struct sftp *s, const char *path, int flags, mode_t mode);

int
sftp_close (struct sftp_fd * fd);

int
sftp_read (struct sftp_fd *fd, void *buf, size_t nbyte, off_t offset);

int
sftp_write (struct sftp_fd *fd, const void *buf, size_t nbyte);

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

struct sftp_fd *
sftp_creat (struct sftp *s, const char *path, mode_t mode);

#endif
