#define FUSE_USE_VERSION 27

#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>

#include <fuse.h>
#include <sftp.h>
#include <sftp_tree.h>

#include <debug.h>

FILE *DEBUGFP = NULL;
static pthread_mutex_t mutex;
static struct sftp_node *sftp_context = NULL;
static char *mount_point;

struct options
{
  char *config_file_path;
} options;

#define ARSENAL_OPT_KEY(t, p, v) { t, offsetof (struct options, p), v }

enum
{
  KEY_VERSION,
  KEY_HELP
};

static struct fuse_opt arsenal_opts[] =
{
  ARSENAL_OPT_KEY ("cfg=%s", config_file_path, 0),
  FUSE_OPT_KEY ("-V", KEY_VERSION),
  FUSE_OPT_KEY ("--version", KEY_VERSION),
  FUSE_OPT_KEY ("-h", KEY_HELP),
  FUSE_OPT_KEY ("--help", KEY_HELP),
  FUSE_OPT_END
};

static int
arsenal_getattr (const char *path, struct stat *buf)
{
  memset (buf, 0, sizeof *buf);

  if (sftp_tree_lstat (sftp_context, path, buf) < 0)
    {
      print_error ("sftp_lstat");
      errno = ENOENT;
      return -1;
    }
  return 0;
}

static int
arsenal_readlink (const char *path, char *buf, size_t bufsize)
{
  int err;

  if ((err = sftp_tree_realpath (sftp_context, path, buf, bufsize)) < 0)
    {
      print_error ("sftp_realpath");
      return -1;
    }

  return 0;
}

static int
arsenal_open (const char *path, struct fuse_file_info *fi)
{
  if (0 == (fi->fh = (uint64_t) sftp_tree_open (sftp_context, path, fi->flags,
                                           O_RDONLY)))
    {
      print_error ("sftp_open");
      return -EACCES;
    }
  return 0;
}

static int
arsenal_read (const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
  int amount_read;
  (void) path;
  (void) offset;

  pthread_mutex_lock (&mutex);
  amount_read = sftp_read ((struct sftp_fd *) fi->fh, buf, size, offset);
  if (amount_read <= 0)
    {
      print_error ("sftp_read");
      if (errno == EOF)
        {
          print_error ("sftp_read: EOF");
          pthread_mutex_unlock (&mutex);
          return -EOF;
        }
      pthread_mutex_unlock (&mutex);
      return -ENOENT;
    }
  pthread_mutex_unlock (&mutex);
  return amount_read;
}

static int
arsenal_statfs (const char *path, struct statvfs *buf)
{
  if (sftp_tree_statvfs (sftp_context, path, buf) < 0)
    {
      print_error ("sftp_statvfs");
      return -1;
    }
  return 0;
}

static int
arsenal_release (const char *path, struct fuse_file_info *fi)
{
  (void) path;
  return sftp_close ((struct sftp_fd *) fi->fh);
}

static int
arsenal_opendir (const char *path, struct fuse_file_info *fi)
{
  if (0 == (fi->fh = (uint64_t) sftp_tree_opendir (sftp_context, path)))
    {
      print_error ("sftp_opendir");
      return -ENOENT;
    }
  return 0;
}

static int
arsenal_readdir (const char *path, void *buf, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info *fi)
{
  struct dirent *entry;
  int err = -1;

  (void) path;
  (void) offset;
  (void) fi;

  pthread_mutex_lock (&mutex);
  while (NULL != (entry = sftp_readdir ((struct sftp_dir *) fi->fh)))
    {
      filler (buf, entry->d_name, NULL, 0);
      free (entry);
      err = 0;
    }
  pthread_mutex_unlock (&mutex);
  return err;
}

static int
arsenal_releasedir (const char *path, struct fuse_file_info *fi)
{
  (void) path;

  if (sftp_closedir ((struct sftp_dir *) fi->fh) < 0)
    {
      print_error ("sftp_closedir");
      return -1;
    }
  return 0;
}

static void *
arsenal_init (struct fuse_conn_info *conn)
{
  (void) conn;

  if (NULL == (DEBUGFP = fopen (DEBUGLOG, "a+")))
    return NULL;

  pthread_mutex_init (&mutex, NULL);

  if (NULL == options.config_file_path)
    {
      print_error ("Must specify configuration file");
      return NULL;
    }

  sftp_context = sftp_tree_init (options.config_file_path, mount_point);
  if (NULL == sftp_context)
    {
      print_error ("sftp_tree_init");
      return NULL;
    }
  return NULL;
}

static void
arsenal_destroy (void *vptr)
{
  (void) vptr;
  sftp_tree_destroy (sftp_context);
  fclose (DEBUGFP);
  pthread_mutex_destroy (&mutex);
}

static int
arsenal_fgetattr (const char *path, struct stat *buf, struct fuse_file_info *fi)
{
  (void) path;

  if (sftp_fstat ((struct sftp_fd *) fi->fh, buf) < 0)
    {
      print_error ("sftp_fstat");
      return -1;
    }
  return 0;
}

static struct fuse_operations arsenal_oper = {
  .getattr = arsenal_getattr,
  .readlink = arsenal_readlink,
  .getdir = NULL,
  .mknod = NULL,
  .mkdir = NULL,
  .unlink = NULL,
  .rmdir = NULL,
  .symlink = NULL,
  .rename = NULL,
  .link = NULL,
  .chmod = NULL,
  .chown = NULL,
  .truncate = NULL,
  .utime = NULL,
  .open = arsenal_open,
  .read = arsenal_read,
  .write = NULL,
  .statfs = arsenal_statfs,
  .flush = NULL,
  .release = arsenal_release,
  .fsync = NULL,
  .setxattr = NULL,
  .getxattr = NULL,
  .listxattr = NULL,
  .removexattr = NULL,
  .opendir = arsenal_opendir,
  .readdir = arsenal_readdir,
  .releasedir = arsenal_releasedir,
  .fsyncdir = NULL,
  .init = arsenal_init,
  .destroy = arsenal_destroy,
  .access = NULL,
  .create = NULL,
  .ftruncate = NULL,
  .fgetattr = arsenal_fgetattr,
  .lock = NULL,
  .utimens = NULL,
  .bmap = NULL
};

void
get_mount_point (int argc, char **argv)
{
  struct fuse_args argst = FUSE_ARGS_INIT (argc, argv);
  fuse_parse_cmdline (&argst, &mount_point, NULL, NULL);
}

int
main (int argc, char **argv)
{
  int ret;
  struct fuse_args args = FUSE_ARGS_INIT (argc, argv);

  get_mount_point (argc, argv);
  memset (&options, 0, sizeof (struct options));
  if (-1 == fuse_opt_parse (&args, &options, arsenal_opts, NULL))
    return -1;
  ret = fuse_main (args.argc, args.argv, &arsenal_oper, NULL);
  fuse_opt_free_args (&args);
  return ret;
}
