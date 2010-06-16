#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include <list.h>

#define print_error(msg) { \
  fprintf (stderr, "[%s:%d in %s] ", __FILE__, __LINE__, __func__); \
  fprintf (stderr, "%s\n", msg); \
}

struct list
{
  void **data;
  uint64_t count;
  uint64_t size;
};

struct list *
list_new (void)
{
  struct list *l = calloc (1, sizeof (struct list));

  if (NULL == l)
    {
      print_error ("Out of memory");
      exit (EXIT_FAILURE);
    }

  return l;
}

void
list_add (struct list *l, void *item)
{
  if (NULL == l)
    {
      print_error ("Invalid list");
      exit (EXIT_FAILURE);
    }

  if (l->size <= l->count)
    {
      l->size = l->size ? l->size * 2 : 2;

      if (NULL == (l->data = realloc (l->data, sizeof (void *) * l->size)))
        {
          print_error ("Out of memory");
          exit (EXIT_FAILURE);
        }
    }

  l->data[l->count++] = item;
}

void *
list_get (struct list *l, uint64_t i)
{
  if (NULL == l)
    {
      print_error ("Invalid list");
      exit (EXIT_FAILURE);
    }
    
  if (NULL == l->data)
    {
      print_error ("Invalid list");
      exit (EXIT_FAILURE);
    }
 
  if (l->count <= i)
    {
      print_error ("Invalid item");
      exit (EXIT_FAILURE);
    }
 
  return l->data[i];
}

uint64_t
list_count (struct list *l)
{
  if (NULL == l)
    {
      print_error ("Invalid list");
      exit (EXIT_FAILURE);
    }
    
  return l->count;
}

void
list_free (struct list *l)
{
  free (l->data);
  free (l);
}
