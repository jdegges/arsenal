#include <sftp_tree.h>
#include <sftp.h>
#include <list.h>
#include <debug.h>

#include <libxml/parser.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum sftp_type
{
  SFTP_VOL,
  SFTP_MIR,
  SFTP_DST
};

struct sftp_node
{
  enum sftp_type type;
  struct sftp *sftp_ctx;

  struct list *children;
  size_t last_child;
};

struct args
{
  void *a0;
  void *a1;
  void *a2;
  void *a3;
  void *a4;
};

#define parse_option(v, k){ \
  if (!xmlStrcmp (cur->name, (const xmlChar *) k)) \
    { \
      key = xmlNodeListGetString (doc, cur->xmlChildrenNode, 1); \
      strcpy (v, (const char *) key); \
      xmlFree (key); \
    } \
}

static struct volume *
parse_volume (xmlDocPtr doc, xmlNodePtr cur)
{
  struct volume *v;
  xmlChar *key;

  if (NULL == (v = calloc (1, sizeof *v)))
    return NULL;

  cur = cur->xmlChildrenNode;
  while (cur != NULL)
    {
      parse_option (v->name, "name");
      parse_option (v->root, "root");
      parse_option (v->addr, "address");
      parse_option (v->port, "port");
      parse_option (v->public_key, "public_key");
      parse_option (v->private_key, "private_key");
      parse_option (v->username, "username");
      parse_option (v->passphrase, "passphrase");
      cur = cur->next;
    }
  return v;
}

static struct list *
parse_nodes (xmlDocPtr doc, xmlNodePtr cur, const char *mount_point)
{
  struct list *list;

  if (NULL == (list = list_new ()))
    {
      print_error ("Out of memory");
      return NULL;
    }

  cur = cur->xmlChildrenNode;
  while (cur != NULL)
    {
      if (!xmlStrcmp (cur->name, (const xmlChar *) "volume"))
        {
          struct sftp_node *node;
          struct sftp *s;
          struct volume *v;

          if (NULL == (v = parse_volume (doc, cur)))
            {
              print_error ("Out of memory");
              return NULL;
            }
          if (NULL == (s = sftp_init (v, mount_point)))
            {
              print_error ("");
              return NULL;
            }
          if (NULL == (node = malloc (sizeof *node)))
            {
              print_error ("");
              return NULL;
            }

          node->type = SFTP_VOL;
          node->sftp_ctx = s;
          node->children = NULL;
          node->last_child = 0;
          list_add (list, node);
        }
      else if (!xmlStrcmp (cur->name, (const xmlChar *) "mirror"))
        {
          struct sftp_node *node;
          if (NULL == (node = malloc (sizeof *node)))
            {
              print_error ("");
              return NULL;
            }
          node->type = SFTP_MIR;
          node->sftp_ctx = NULL;
          node->last_child = 0;
          node->children = parse_nodes (doc, cur, mount_point);
          if (NULL == node->children)
            {
              print_error ("");
              return NULL;
            }
          list_add (list, node);
        }
      else if (!xmlStrcmp (cur->name, (const xmlChar *) "distribute"))
        {
          struct sftp_node *node;
          if (NULL == (node = malloc (sizeof *node)))
            {
              print_error ("");
              return NULL;
            }
          node->type = SFTP_DST;
          node->sftp_ctx = NULL;
          node->last_child = 0;
          node->children = parse_nodes (doc, cur, mount_point);
          if (NULL == node->children)
            {
              print_error ("");
              return NULL;
            }
          list_add (list, node);
        }
      else
        {
          //print_error ("Invalid token `%s' in configuration",
          //             (char *) cur->name);
          //return NULL;
        }
      cur = cur->next;
    }

  if (0 == list_count (list))
    {
      print_error ("Empty field...");
      return NULL;
    }

  return list;
}

struct sftp_node *
sftp_tree_init (const char *path, const char *mount_point)
{
  struct sftp_node *root;
  struct list *list;
  xmlDocPtr doc;
  xmlNodePtr cur;

  if (NULL == (DEBUGFP = fopen (DEBUGLOG, "a+")))
    return NULL;

  if (NULL == path || NULL == mount_point)
    {
      print_error ("Invalid inputs");
      return NULL;
    }

  if (NULL == (doc = xmlParseFile (path)))
    {
      print_error ("Document not parsed successfully.");
      return NULL;
    }

  cur = xmlDocGetRootElement (doc);
  if (NULL == (cur = xmlDocGetRootElement (doc)))
    {
      print_error ("empty document");
      xmlFreeDoc (doc);
      return NULL;
    }

  if (xmlStrcmp (cur->name, (const xmlChar *) "arsenal"))
    {
      print_error ("document of the wrong type, root node != arsenal");
      xmlFreeDoc (doc);
      return NULL;
    }

  if (NULL == (list = parse_nodes (doc, cur, mount_point)))
    {
      print_error ("Invalid configuration");
      return NULL;
    }

  if (1 != list_count (list))
    {
      print_error ("Configuration file--expected only one root node, got %lu",
                   list_count (list));
      return NULL;
    }

  if (NULL == (root = list_get (list, 0)))
    {
      print_error ("List error?");
      return NULL;
    }

  xmlFreeDoc (doc);
  xmlCleanupParser ();
  list_free (list);

  print_error ("Successful startup!");

  return root;
}

void
sftp_tree_destroy (struct sftp_node *root)
{
  uint64_t i;

  if (NULL == root)
    return;

  switch (root->type)
    {
      case SFTP_VOL:
        assert (root->sftp_ctx);
        assert (!root->children);
        assert (!root->last_child);
        sftp_destroy (root->sftp_ctx);
        free (root);
        return;
      case SFTP_MIR:
      case SFTP_DST:
        assert (!root->sftp_ctx);
        assert (root->children);
        for (i = 0; i < list_count (root->children); i++)
          sftp_tree_destroy (list_get (root->children, i));
        list_free (root->children);
        free (root);
        return;
    }

  print_error ("Unknown node type");
}

static void *
traverse_tree (struct sftp_node *root, void *(*func)(), struct args *a,
               size_t nargs, void *error_code, int(*is_error)(void *, void *))
{
  struct sftp_node *node;
  void *r;
  size_t i;

  if (NULL == root || NULL == func || NULL == a || NULL == is_error)
    {
      print_error ("Invalid inputs");
      return error_code;
    }

  switch (root->type)
    {
      case SFTP_VOL:
        switch (nargs)
          {
            case 1: return func (root->sftp_ctx, a->a0);
            case 2: return func (root->sftp_ctx, a->a0, a->a1);
            case 3: return func (root->sftp_ctx, a->a0, a->a1, a->a2);
          }
        print_error ("Invalid number of arguments");
        return error_code;
      case SFTP_MIR:
        /* query children in a round-robbin fashion */
        i = root->last_child++ % list_count (root->children);
        node = (struct sftp_node *) list_get (root->children, i);
        if (NULL == node)
          {
            print_error ("");
            return error_code;
          }
        return traverse_tree (node, func, a, nargs, error_code, is_error);
      case SFTP_DST:
        /* step through children sequentially (depth first search)
         * FIXME: this should be random! (or more accurate to avoid hammering
         * the first listed node) */
        r = error_code;
        for (i = 0; i < list_count (root->children); i++)
          {
            node = (struct sftp_node *) list_get (root->children, i);
            if (NULL == node)
              {
                print_error ("");
                return error_code;
              }

            r = traverse_tree (node, func, a, nargs, error_code, is_error);
            if (!is_error (a, r))
              return r;
          }
        return r;
    }

  print_error ("Invalid node type");
  return error_code;
}

static int
is_nz_stat (void *v, void *p)
{
  struct args *a = v;
  struct stat *buf = a->a1;
  if (0 != (ssize_t) p)
    return 1;
  if (buf->st_size)
    return 0;
  return 1;
}

int
sftp_tree_stat (struct sftp_node *root, const char *path, struct stat *buf)
{
  struct args a = {(char *) path, buf};
  return (ssize_t) traverse_tree (root, (void *(*)()) sftp_stat, &a, 2,
                                  (void *) -1, is_nz_stat);
}

int
sftp_tree_lstat (struct sftp_node *root, const char *path, struct stat *buf)
{
  struct args a = {(char *) path, buf};
  return (ssize_t ) traverse_tree (root, (void *(*)()) sftp_lstat, &a, 2,
                                   (void *) -1, is_nz_stat);
}

static struct statvfs sbuf;

static int
is_nz_statvfs (void *v, void *p)
{
  struct args *a = v;
  struct statvfs *buf = a->a1;
  if (0 != (ssize_t) p)
    return 1;
  buf->f_blocks = sbuf.f_blocks += buf->f_blocks;
  buf->f_bfree = sbuf.f_bfree += buf->f_bfree;
  buf->f_bavail = sbuf.f_bavail += buf->f_bavail;
  buf->f_files = sbuf.f_files += buf->f_files;
  buf->f_ffree = sbuf.f_ffree += buf->f_ffree;
  buf->f_favail = sbuf.f_favail += buf->f_favail;
  return 1;
}

int
sftp_tree_statvfs (struct sftp_node *root, const char *path,
                   struct statvfs *buf)
{
  struct args a = {(char *) path, buf};
  sbuf.f_blocks = 0;
  sbuf.f_bfree = 0;
  sbuf.f_bavail = 0;
  sbuf.f_files = 0;
  sbuf.f_ffree = 0;
  sbuf.f_favail = 0;
  return (ssize_t) traverse_tree (root, (void *(*)()) sftp_statvfs, &a, 2,
                                  (void *) -1, is_nz_statvfs);
}

static int
is_ltz (void *a, void *p)
{
  (void) a;
  return (ssize_t) p < 0 ? 1 : 0;
}

ssize_t
sftp_tree_realpath (struct sftp_node *root, const char *path, char *buf,
                    size_t bufsize)
{
  struct args a = {(char *) path, buf, (void *) bufsize};
  return (ssize_t) traverse_tree (root, (void *(*)()) sftp_realpath, &a, 3,
                                  (void *) -1, is_ltz);
}

static int
is_null_open (void *a, void *p)
{
  (void) a;
  struct stat buf;
  struct sftp_fd *fd = p;
  if (NULL == fd)
    return 1;
  if (sftp_fstat (fd, &buf))
    return 1;
  if (buf.st_size)
    return 0;
  sftp_close (fd);
  return 1;
}

struct sftp_fd *
sftp_tree_open (struct sftp_node *root, const char *path, int flags,
                mode_t mode)
{
  struct args a = {(char *) path, (void *) (size_t) flags,
                   (void *) (size_t) mode};
  return (struct sftp_fd *) traverse_tree (root, (void *(*)()) sftp_open, &a,
                                           3, NULL, is_null_open);
}

static int
is_null (void *v, void *p)
{
  (void) v;
  return p ? 0 : 1;
}

struct sftp_dir *
sftp_tree_opendir (struct sftp_node *root, const char *path)
{
  struct args a = {(char *) path};
  return (struct sftp_dir *) traverse_tree (root, (void *(*)()) sftp_opendir,
                                            &a, 1, NULL, is_null);
}
