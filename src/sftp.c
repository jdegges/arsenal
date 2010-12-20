#define _POSIX_SOURCE
#define _BSD_SOURCE

#include <stdlib.h>
#include <stdio.h>

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include <libssh2.h>
#include <libssh2_sftp.h>
#include <list.h>
#include <debug.h>

#include <sftp.h>

struct sftp
{
  int sockfd;
  LIBSSH2_SESSION *session;
  LIBSSH2_SFTP *sftp;
  pthread_mutex_t mutex;
  char jail[PATH_MAX];
  size_t jail_len;
  struct list *list;
  char *mount_point;
  size_t mount_size;
};

struct sftp_dir
{
  struct sftp *sftp_ctx;
  LIBSSH2_SFTP_HANDLE *handle;
};

struct sftp_fd
{
  struct sftp *sftp_ctx;
  LIBSSH2_SFTP *sftp;
  LIBSSH2_SFTP_HANDLE *handle;
  off_t offset;
};

#define pthread_error(expr){ \
  int err = (expr); \
  if (0 != err) \
    print_error ("%s", strerror (err)); \
}

#define sftp_lock(s) pthread_error (pthread_mutex_lock(&s->mutex))
#define sftp_unlock(s) pthread_error (pthread_mutex_unlock(&s->mutex))

static char *
resolve_path (struct sftp *s, const char *path, char *resolved_path)
{
  char jpath[PATH_MAX];
  char *buf = NULL;
  size_t i;
  int err;

  if (NULL == s || NULL == path)
    return NULL;

  if (snprintf (jpath, PATH_MAX, "%s/%s", s->jail, path) < 0)
    return NULL;

  if (NULL == resolved_path
      && NULL == (buf = resolved_path = calloc (PATH_MAX,
                                                sizeof *resolved_path)))
    {
      print_error ("Out of memory");
      return NULL;
    }

  sftp_lock (s);
  if ((err = libssh2_sftp_realpath (s->sftp,
                                    jpath,
                                    resolved_path,
                                    PATH_MAX)) <= 0)
    {
      print_error ("libssh2_sftp_realpath: %d", err);
      print_error ("trying to resolve: %s", jpath);
      free (buf);
      resolved_path = NULL;
      errno = ENOENT;
      goto exit;
    }

  /* make sure the resolved path is within the jail */
  for (i = 0; i < s->jail_len && s->jail[i] == resolved_path[i] ; i++);

  /* if its not in the jail then return NULL */
  if (i < s->jail_len)
    {
      free (buf);
      resolved_path = NULL;
      errno = EACCES;
      goto exit;
    }

  if (resolved_path)
    memcpy (resolved_path, jpath, PATH_MAX);

exit:
  sftp_unlock (s);
  return resolved_path;
}

struct sftp *
sftp_init (struct volume *vol, const char *mount_point)
{
  struct sftp *s = NULL;
  LIBSSH2_SESSION *session = NULL;
  LIBSSH2_SFTP *sftp = NULL;
  int sockfd = -1;
  int err;

  if (NULL == vol)
    return NULL;

  if ((err = libssh2_init (0)) < 0)
    {
      print_error ("libssh2_init: %d", err);
      return NULL;
    }

  /* connect to host/port */
  {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s;

    memset (&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* Stream socket */
    hints.ai_flags = 0;
    hints.ai_protocol = 0;          /* Any protocol */

    if (0 != (s = getaddrinfo (vol->addr, vol->port, &hints, &result)))
      {
        print_error ("%s", gai_strerror (s));
        goto error;
      }

    for (rp = result; NULL != rp; rp = rp->ai_next)
      {
        if (-1 == (sockfd = socket (rp->ai_family, rp->ai_socktype,
                                    rp->ai_protocol)))
          continue;

        if (-1 != connect (sockfd, rp->ai_addr, rp->ai_addrlen))
          break;

        if (0 != close (sockfd))
          print_error ("%s", strerror (errno));
      }

    if (NULL == rp)
      {
        print_error ("Could not connect");
        freeaddrinfo (result);
        goto error;
      }

    freeaddrinfo (result);
  }

  if (NULL == (session = libssh2_session_init ()))
    {
      print_error ("libssh2_session_init");
      goto error;
    }

  if ((err = libssh2_session_startup (session, sockfd)) < 0)
    {
      print_error ("libssh2_session_startup: %d", err);
      goto error;
    }

  if ((err = libssh2_userauth_publickey_fromfile (session, vol->username,
                                                  vol->public_key,
                                                  vol->private_key,
                                                  vol->passphrase)))
    {
      print_error ("libssh2_userauth_publickey_fromfile: %d", err);
      goto error;
    }

  if (NULL == (sftp = libssh2_sftp_init (session)))
    {
      print_error ("libssh2_sftp_init");
      goto error;
    }

  libssh2_session_set_blocking (session, 1);

  if (NULL == (s = malloc (sizeof *s)))
    {
      print_error ("Out of memory");
      goto error;
    }

  pthread_error (pthread_mutex_init (&s->mutex, NULL));

  s->sockfd = sockfd;
  s->session = session;
  s->sftp = sftp;
  s->mount_point = (char *) mount_point;
  s->mount_size = strlen (mount_point);

  if ('\0' == *vol->root)
    strcpy (s->jail, "/");
  else
    strcpy (s->jail, vol->root);

  s->jail_len = strlen (s->jail);

  return s;

error:
  if (NULL != sftp && (err = libssh2_sftp_shutdown (sftp)) < 0)
    print_error ("libssh2_sftp_shutdown: %d", err);
  if (NULL != session && (err = libssh2_session_free (session)) < 0)
    print_error ("libssh2_session_free: %d", err);
  if (sockfd < 0 && 0 != close (sockfd))
    print_error ("%s", strerror (errno));
  libssh2_exit ();
  print_error ("sftp_init");
  return NULL;
}

void
sftp_destroy (struct sftp *s)
{
  int err;

  if (NULL != s)
    {
      if (NULL != s->sftp && (err = libssh2_sftp_shutdown (s->sftp)) < 0)
        print_error ("libssh2_sftp_shutdown: %d", err);
      if (NULL != s->session && (err = libssh2_session_free (s->session)) < 0)
        print_error ("libssh2_session_free: %d", err);
      if (s->sockfd < 0 && 0 != close (s->sockfd))
        print_error ("%s", strerror (errno));
      pthread_error (pthread_mutex_destroy (&s->mutex));
      libssh2_exit ();
      if (NULL != s->list)
        {
          uint64_t i;
          for (i = 0; i < list_count (s->list); i++)
            free (list_get (s->list, i));
          list_free (s->list);
        }
      free (s);
    }
}

int
sftp_stat (struct sftp *s, const char *path, struct stat *buf)
{
  LIBSSH2_SFTP_ATTRIBUTES attrs;
  char *rpath;
  int err;

  if (NULL == s || NULL == s->sftp || NULL == path || NULL == buf)
    {
      print_error ("Invalid arguments");
      return -1;
    }

  if (NULL == (rpath = resolve_path (s, path, NULL)))
    {
      print_error ("resolve_path");
      return -1;
    }

  sftp_lock (s);
  if ((err = libssh2_sftp_stat (s->sftp, rpath, &attrs)) < 0)
    {
      print_error ("libssh2_sftp_stat: %d", err);
      err = -1;
      goto exit;
    }

  memset (buf, 0, sizeof *buf);
  if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE)
    buf->st_size = attrs.filesize;
  if (attrs.flags & LIBSSH2_SFTP_ATTR_UIDGID)
    {
      buf->st_uid = attrs.uid;
      buf->st_gid = attrs.gid;
    }
  if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS)
    buf->st_mode = (LIBSSH2_SFTP_S_ISLNK (attrs.permissions) ? S_IFLNK : 0)
                 | (LIBSSH2_SFTP_S_ISREG (attrs.permissions) ? S_IFREG : 0)
                 | (LIBSSH2_SFTP_S_ISDIR (attrs.permissions) ? S_IFDIR : 0)
                 | (LIBSSH2_SFTP_S_ISCHR (attrs.permissions) ? S_IFCHR : 0)
                 | (LIBSSH2_SFTP_S_ISBLK (attrs.permissions) ? S_IFBLK : 0)
                 | (LIBSSH2_SFTP_S_ISFIFO (attrs.permissions) ? S_IFIFO : 0)
                 | (LIBSSH2_SFTP_S_ISSOCK (attrs.permissions) ? S_IFSOCK : 0)
                 | (LIBSSH2_SFTP_S_IRUSR & attrs.permissions ? S_IRUSR : 0)
                 | (LIBSSH2_SFTP_S_IWUSR & attrs.permissions ? S_IWUSR : 0)
                 | (LIBSSH2_SFTP_S_IXUSR & attrs.permissions ? S_IXUSR : 0)
                 | (LIBSSH2_SFTP_S_IRGRP & attrs.permissions ? S_IRGRP : 0)
                 | (LIBSSH2_SFTP_S_IWGRP & attrs.permissions ? S_IWGRP : 0)
                 | (LIBSSH2_SFTP_S_IXGRP & attrs.permissions ? S_IXGRP : 0)
                 | (LIBSSH2_SFTP_S_IROTH & attrs.permissions ? S_IROTH : 0)
                 | (LIBSSH2_SFTP_S_IWOTH & attrs.permissions ? S_IWOTH : 0)
                 | (LIBSSH2_SFTP_S_IXOTH & attrs.permissions ? S_IXOTH : 0);
  if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME)
    {
      buf->st_atime = attrs.atime;
      buf->st_mtime = attrs.mtime;
      /* ctime is not supported in this version of sftp. 99.9% of applications
       * should be ok with mtime. */
      buf->st_ctime = attrs.mtime;
    }

  err = 0;
exit:
  sftp_unlock (s);
  free (rpath);
  return err;
}

int
sftp_fstat (struct sftp_fd *fd, struct stat *buf)
{
  LIBSSH2_SFTP_ATTRIBUTES attrs;
  int err;

  if (NULL == fd || NULL == fd->handle || NULL == buf)
    {
      print_error ("Invalid arguments");
      return -1;
    }

  sftp_lock (fd->sftp_ctx);
  if ((err = libssh2_sftp_fstat (fd->handle, &attrs)) < 0)
    {
      print_error ("libssh2_sftp_fstat: %d", err);
      err = -1;
      goto exit;
    }

  memset (buf, 0, sizeof *buf);
  if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE)
    buf->st_size = attrs.filesize;
  if (attrs.flags & LIBSSH2_SFTP_ATTR_UIDGID)
    {
      buf->st_uid = attrs.uid;
      buf->st_gid = attrs.gid;
    }
  if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS)
    buf->st_mode = (LIBSSH2_SFTP_S_ISLNK (attrs.permissions) ? S_IFLNK : 0)
                 | (LIBSSH2_SFTP_S_ISREG (attrs.permissions) ? S_IFREG : 0)
                 | (LIBSSH2_SFTP_S_ISDIR (attrs.permissions) ? S_IFDIR : 0)
                 | (LIBSSH2_SFTP_S_ISCHR (attrs.permissions) ? S_IFCHR : 0)
                 | (LIBSSH2_SFTP_S_ISBLK (attrs.permissions) ? S_IFBLK : 0)
                 | (LIBSSH2_SFTP_S_ISFIFO (attrs.permissions) ? S_IFIFO : 0)
                 | (LIBSSH2_SFTP_S_ISSOCK (attrs.permissions) ? S_IFSOCK : 0)
                 | (LIBSSH2_SFTP_S_IRUSR & attrs.permissions ? S_IRUSR : 0)
                 | (LIBSSH2_SFTP_S_IWUSR & attrs.permissions ? S_IWUSR : 0)
                 | (LIBSSH2_SFTP_S_IXUSR & attrs.permissions ? S_IXUSR : 0)
                 | (LIBSSH2_SFTP_S_IRGRP & attrs.permissions ? S_IRGRP : 0)
                 | (LIBSSH2_SFTP_S_IWGRP & attrs.permissions ? S_IWGRP : 0)
                 | (LIBSSH2_SFTP_S_IXGRP & attrs.permissions ? S_IXGRP : 0)
                 | (LIBSSH2_SFTP_S_IROTH & attrs.permissions ? S_IROTH : 0)
                 | (LIBSSH2_SFTP_S_IWOTH & attrs.permissions ? S_IWOTH : 0)
                 | (LIBSSH2_SFTP_S_IXOTH & attrs.permissions ? S_IXOTH : 0);
  if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME)
    {
      buf->st_atime = attrs.atime;
      buf->st_mtime = attrs.mtime;
      /* ctime is not supported in this version of sftp. 99.9% of applications
       * should be ok with mtime. */
      buf->st_ctime = attrs.mtime;
    }

  err = 0;
exit:
  sftp_unlock (fd->sftp_ctx);
  return err;
}

int
sftp_lstat (struct sftp *s, const char *path, struct stat *buf)
{
  LIBSSH2_SFTP_ATTRIBUTES attrs;
  char *rpath;
  int err;

  if (NULL == s || NULL == s->sftp || NULL == path || NULL == buf)
    {
      print_error ("Invalid arguments");
      return -1;
    }

  if (NULL == (rpath = resolve_path (s, path, NULL)))
    {
      print_error ("resolve_path: trying to resolve `%s'", path);
      return -1;
    }

  sftp_lock (s);
  if ((err = libssh2_sftp_lstat (s->sftp, rpath, &attrs)) < 0)
    {
      print_error ("libssh2_sftp_stat: %d", err);
      print_error ("trying to lstat: %s", rpath);
      err = -1;
      goto exit;
    }

  memset (buf, 0, sizeof *buf);
  if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE)
    buf->st_size = attrs.filesize;
  if (attrs.flags & LIBSSH2_SFTP_ATTR_UIDGID)
    {
      buf->st_uid = attrs.uid;
      buf->st_gid = attrs.gid;
    }
  if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS)
    buf->st_mode = (LIBSSH2_SFTP_S_ISLNK (attrs.permissions) ? S_IFLNK : 0)
                 | (LIBSSH2_SFTP_S_ISREG (attrs.permissions) ? S_IFREG : 0)
                 | (LIBSSH2_SFTP_S_ISDIR (attrs.permissions) ? S_IFDIR : 0)
                 | (LIBSSH2_SFTP_S_ISCHR (attrs.permissions) ? S_IFCHR : 0)
                 | (LIBSSH2_SFTP_S_ISBLK (attrs.permissions) ? S_IFBLK : 0)
                 | (LIBSSH2_SFTP_S_ISFIFO (attrs.permissions) ? S_IFIFO : 0)
                 | (LIBSSH2_SFTP_S_ISSOCK (attrs.permissions) ? S_IFSOCK : 0)
                 | (LIBSSH2_SFTP_S_IRUSR & attrs.permissions ? S_IRUSR : 0)
                 | (LIBSSH2_SFTP_S_IWUSR & attrs.permissions ? S_IWUSR : 0)
                 | (LIBSSH2_SFTP_S_IXUSR & attrs.permissions ? S_IXUSR : 0)
                 | (LIBSSH2_SFTP_S_IRGRP & attrs.permissions ? S_IRGRP : 0)
                 | (LIBSSH2_SFTP_S_IWGRP & attrs.permissions ? S_IWGRP : 0)
                 | (LIBSSH2_SFTP_S_IXGRP & attrs.permissions ? S_IXGRP : 0)
                 | (LIBSSH2_SFTP_S_IROTH & attrs.permissions ? S_IROTH : 0)
                 | (LIBSSH2_SFTP_S_IWOTH & attrs.permissions ? S_IWOTH : 0)
                 | (LIBSSH2_SFTP_S_IXOTH & attrs.permissions ? S_IXOTH : 0);
  if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME)
    {
      buf->st_atime = attrs.atime;
      buf->st_mtime = attrs.mtime;
      /* ctime is not supported in this version of sftp. 99.9% of applications
       * should be ok with mtime. */
      buf->st_ctime = attrs.mtime;
    }

  err = 0;
exit:
  sftp_unlock (s);
  free (rpath);
  return err;
}

ssize_t
sftp_readlink (struct sftp *s, const char *path, char *buf, size_t bufsize)
{
  char *rpath;
  int err;

  if (NULL == s || NULL == s->sftp || NULL == path || NULL == buf
      || 0 == bufsize)
    {
      print_error ("Invalid arguments");
      return -1;
    }

  if (NULL == (rpath = resolve_path (s, path, NULL)))
    {
      print_error ("resolve_path");
      return -1;
    }

  sftp_lock (s);
  if ((err = libssh2_sftp_readlink (s->sftp, rpath, buf, bufsize)) < 0)
    {
      print_error ("libssh2_sftp_readlink: %d", err);
      err = -1;
    }
  sftp_unlock (s);
  free (rpath);
  return err;
}

ssize_t
sftp_realpath (struct sftp *s, const char *path, char *buf, size_t bufsize)
{
  char *rpath;
  int err;

  if (NULL == s || NULL == s->sftp || NULL == path || NULL == buf
      || 0 == bufsize)
    {
      print_error ("Invalid arguments");
      return -1;
    }

  if (NULL == (rpath = resolve_path (s, path, NULL)))
    {
      print_error ("resolve_path");
      return -1;
    }

  sftp_lock (s);
  if ((err = libssh2_sftp_realpath (s->sftp, rpath, buf, bufsize)) < 0)
    {
      print_error ("libssh2_sftp_readlink: %d", err);
      err = -1;
    }
  sftp_unlock (s);

  if (0 < err)
    {
      size_t i;
      char *p;
      for (i = 0; i < s->mount_size; i++)
        buf[i] = s->mount_point[i];
      for (p = buf+i, i = s->jail_len; i < (size_t) err; i++)
        *p++ = buf[i];
      *p = '\0';
    }

  free (rpath);
  return err;
}

struct sftp_fd *
sftp_open (struct sftp *s, const char *path, int flags, mode_t mode)
{
  struct sftp_fd *fd = NULL;
  unsigned long libssh2_flags = 0;
  char *rpath;

  if (NULL == s || NULL == s->sftp || NULL == path)
    {
      print_error ("Invalid arguments");
      return NULL;
    }

  /* make sure the file access/status modes are valid and supported */
  if (((O_RDONLY & flags) && (O_WRONLY & flags))
      || ((O_RDONLY & flags) && (O_RDWR & flags))
      || ((O_WRONLY & flags) && (O_RDWR & flags))
      || (O_CREAT & flags) || (O_EXCL & flags)
      || (O_NOCTTY & flags) || (O_TRUNC & flags))
    {
      print_error ("Invalid flags");
      return NULL;
    }


  if (NULL == (fd = calloc (1, sizeof *fd)))
    {
      print_error ("Out of memory");
      return NULL;
    }

  if (NULL == (rpath = resolve_path (s, path, NULL)))
    {
      print_error ("resolve_path");
      return NULL;
    }

  libssh2_flags = (O_RDONLY & flags ? LIBSSH2_FXF_READ : 0)
                  | (O_WRONLY & flags ? LIBSSH2_FXF_WRITE : 0)
                  | (O_RDWR & flags ? LIBSSH2_FXF_READ & LIBSSH2_FXF_WRITE : 0)
                  | (O_APPEND & flags ? LIBSSH2_FXF_APPEND : 0);

  sftp_lock (s);
  if (NULL == (fd->handle = libssh2_sftp_open (s->sftp, rpath, libssh2_flags,
                                               mode)))
    {
      free (fd);
      fd = NULL;
      print_error ("libssh2_sftp_open");
      goto exit;
    }

  fd->sftp_ctx = s;
  fd->sftp = s->sftp;

exit:
  sftp_unlock (s);
  free (rpath);
  return fd;
}

int
sftp_close (struct sftp_fd * fd)
{
  int err;

  if (NULL == fd || NULL == fd->handle)
    {
      print_error ("Invalid arguments");
      return -1;
    }

  sftp_lock (fd->sftp_ctx);
  if ((err = libssh2_sftp_close (fd->handle)) < 0)
    {
      print_error ("libssh2_sftp_close: %d", err);
      err = -1;
      goto exit;
    }
 
  err = 0;
exit:
  sftp_unlock (fd->sftp_ctx);
  free (fd);
  return err;
}

int
sftp_read (struct sftp_fd *fd, void *buf, size_t nbyte, off_t offset)
{
  int amount_read;

  if (NULL == fd || NULL == fd->handle || NULL == buf || 0 == nbyte)
    {
      print_error ("Invalid arguments");
      return -1;
    }

  sftp_lock (fd->sftp_ctx);
  /* if the requested offset is not sequential then seek */
  if (offset != fd->offset)
    {
      libssh2_sftp_seek64 (fd->handle, offset);
      fd->offset = offset;
    }
  if ((amount_read = libssh2_sftp_read (fd->handle, buf, nbyte)) < 0)
    {
      int err;
      if (LIBSSH2_FX_EOF == (err = libssh2_sftp_last_error (fd->sftp)))
        errno = EOF;
      print_error ("libssh2_sftp_read: %d", err);
      amount_read = -1;
    }
  else
    fd->offset += amount_read;
  sftp_unlock (fd->sftp_ctx);
  return amount_read;
}

int
sftp_statvfs (struct sftp *s, const char *path, struct statvfs *buf)
{
  LIBSSH2_SFTP_STATVFS st;
  char *rpath;
  int err;

  if (NULL == s || NULL == s->sftp || NULL == path || NULL == buf)
    {
      print_error ("Invalid arguments");
      return -1;
    }

  if (NULL == (rpath = resolve_path (s, path, NULL)))
    {
      print_error ("resolve_path");
      return -1;
    }

  sftp_lock (s);
  if ((err = libssh2_sftp_statvfs (s->sftp, rpath, strlen (rpath), &st)) < 0)
    {
      print_error ("libssh2_sftp_statvfs: %d", err);
      err = -1;
      goto exit;
    }

  buf->f_bsize = st.f_bsize;
  buf->f_frsize = st.f_frsize;
  buf->f_blocks = st.f_blocks;
  buf->f_bfree = st.f_bfree;
  buf->f_bavail = st.f_bavail;
  buf->f_files = st.f_files;
  buf->f_ffree = st.f_ffree;
  buf->f_favail = st.f_favail;
  buf->f_fsid = st.f_fsid;
  buf->f_flag = st.f_flag;
  buf->f_namemax = st.f_namemax;

  err = 0;
exit:
  sftp_unlock (s);
  free (rpath);
  return err;
}

int
sftp_fstatvfs (struct sftp_fd *fd, struct statvfs *buf)
{
  LIBSSH2_SFTP_STATVFS st;
  int err;

  if (NULL == fd || NULL == fd->handle || NULL == buf)
    {
      print_error ("Invalid arguments");
      return -1;
    }

  sftp_lock (fd->sftp_ctx);
  if ((err = libssh2_sftp_fstatvfs (fd->handle, &st)) < 0)
    {
      print_error ("libssh2_sftp_fstatvfs: %d", err);
      err = -1;
      goto exit;
    }

  buf->f_bsize = st.f_bsize;
  buf->f_frsize = st.f_frsize;
  buf->f_blocks = st.f_blocks;
  buf->f_bfree = st.f_bfree;
  buf->f_bavail = st.f_bavail;
  buf->f_files = st.f_files;
  buf->f_ffree = st.f_ffree;
  buf->f_favail = st.f_favail;
  buf->f_fsid = st.f_fsid;
  buf->f_flag = st.f_flag;
  buf->f_namemax = st.f_namemax;

  err = 0;
exit:
  sftp_unlock (fd->sftp_ctx);
  return err;
}

struct sftp_dir *
sftp_opendir (struct sftp *s, const char *path)
{
  struct sftp_dir *dir = NULL;
  LIBSSH2_SFTP_HANDLE *handle;
  char *rpath;

  if (NULL == s || NULL == s->sftp || NULL == path)
    {
      print_error ("Invalid arguments");
      return NULL;
    }

  if (NULL == (rpath = resolve_path (s, path, NULL)))
    {
      print_error ("resolve_path");
      return NULL;
    }

  sftp_lock (s);
  if (NULL == (handle = libssh2_sftp_opendir (s->sftp, rpath)))
    {
      print_error ("libssh2_sftp_opendir");
      goto exit;
    }

  if (NULL == (dir = malloc (sizeof *dir)))
    {
      int err;
      print_error ("Out of memory");
      if ((err = libssh2_sftp_closedir (handle)) < 0)
        print_error ("libssh2_sftp_closedir: %d", err);
      goto exit;
    }

  dir->handle = handle;
  dir->sftp_ctx = s;
exit:
  sftp_unlock (s);
  free (rpath);
  return dir;
}

struct dirent *
sftp_readdir (struct sftp_dir *dir)
{
  struct dirent *d = NULL;
  int err;

  if (NULL == dir || NULL == dir->sftp_ctx || NULL == dir->handle)
    {
      print_error ("Invalid arguments");
      return NULL;
    }

  if (NULL == (d = calloc (1, sizeof *d)))
    {
      print_error ("Out of memory");
      return NULL;
    }

  sftp_lock (dir->sftp_ctx);
  if ((err = libssh2_sftp_readdir (dir->handle, d->d_name, 256, NULL)) < 0)
    {
      free (d);
      d = NULL;
      goto exit;
    }
  d->d_reclen = err;

  if (0 == d->d_reclen)
    {
      free (d);
      d = NULL;
      goto exit;
    }

exit:
  sftp_unlock (dir->sftp_ctx);
  return d;
}

int
sftp_closedir (struct sftp_dir *dir)
{
  int err;

  if (NULL == dir || NULL == dir->sftp_ctx || NULL == dir->handle)
    {
      print_error ("Invalid arguments");
      return -1;
    }

  sftp_lock (dir->sftp_ctx);
  if ((err = libssh2_sftp_closedir (dir->handle)) < 0)
    {
      print_error ("libssh2_sftp_closedir: %d", err);
      err = -1;
    }
  sftp_unlock (dir->sftp_ctx);
  free (dir);
  return err;
}
