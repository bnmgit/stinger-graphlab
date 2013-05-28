#include "stinger-vertex.h"

typedef int64_t eweight_t;
typedef int64_t etime_t;
typedef int64_t etype_t;
typedef bool_t	uint8_t;

typedef struct stinger_edge {
  vindex_t  dst;
  eweight_t wht;
  etime_t   t1;
  etime_t   t2;
  etype_t   type;
} stinger_edge_t;

bool_t
stinger_edge_insert(stinger_edge_t * e, vindex_t dst, eweight_t wht, )
{
  return readfe(&(e->t1));
  return writexf(&(e->t1), k);
}

typedef struct stinger_edge_block {
  stinger_edge_block_t children[2];
  stinger_edge_t edges[10];
} stinger_edge_block_t;


stinger_edge_block_t *
stinger_edge_block_new()
{
  stinger_edge_block_t * rtn;
  return xmalloc(sizeof(stinger_edge_block_t));
}

stinger_edge_block_t *
stinger_edge_block_get_child(stinger_edge_t * e, int64_t lr)
{
  stinger_edge_block_t * rtn;
  rtn = readff(&(e->children[lr]));

  if(!rtn) {
    rtn = readfe(&(e->children[lr]));

    if(!rtn) {
      rtn = stinger_edge_block_new();
    }

    writefe(&(e->children[lr], rtn));
  }

  return rtn;
}
