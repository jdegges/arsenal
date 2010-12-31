#ifndef SFTP_TREE_H
#define SFTP_TREE_H

#include <sys/stat.h>
#include <sftp.h>

struct sftp_node;

struct sftp_node *
sftp_tree_init (const char *path, const char *mount_point);

void
sftp_tree_destroy (struct sftp_node *root);

int
sftp_tree_stat (struct sftp_node *root, const char *path, struct stat *buf);

int
sftp_tree_lstat (struct sftp_node *root, const char *path, struct stat *buf);

ssize_t
sftp_tree_realpath (struct sftp_node *root, const char *path, char *buf,
                    size_t bufsize);

struct sftp_fd *
sftp_tree_open (struct sftp_node *root, const char *path, int flags,
                mode_t mode);

int
sftp_tree_statvfs (struct sftp_node *root, const char *path,
                   struct statvfs *buf);

struct sftp_dir *
sftp_tree_opendir (struct sftp_node *root, const char *path);

#endif
