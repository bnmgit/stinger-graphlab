#include "util/json-support.h"
#include "fmemopen.h"
#include "xmalloc.h"
#include "int-hm-seq.h"

/* produces the egoent of the stinger vertex ID given in JSON form */
string_t *
egonet_to_json(stinger_t * S, int64_t vtx) {
  string_t vertices, edges;
  string_init_from_cstr(&vertices, "\"vertices\" : [ \n");
  string_init_from_cstr(&edges, "\"edges\" : [ \n");

  char *vtx_str, *edge_str;
  FILE * vtx_file;
  int edge_added = 0;

  vtx_str = xmalloc (2UL<<20UL);
  edge_str = &vtx_str[1UL<<20UL];
  vtx_file = fmemopen(vtx_str, sizeof(vtx_str) * 1UL<<20UL, "w");

  stinger_vertex_to_json_with_type_strings(stinger_vertices_get(S), stinger_vtype_names_get(S), stinger_physmap_get(S), vtx, vtx_file, 2);
  fflush(vtx_file);
  string_append_cstr(&vertices, vtx_str);
  fseek(vtx_file, 0, SEEK_SET);

  int_hm_seq_t * neighbors = int_hm_seq_new(stinger_outdegree_get(S, vtx));
  int64_t which = 1;

  STINGER_FORALL_EDGES_OF_VTX_BEGIN(S, vtx) {

    int64_t source = int_hm_seq_get(neighbors, STINGER_EDGE_DEST);
    if(INT_HT_SEQ_EMPTY == source) {
      source = which++;
      int_hm_seq_insert(neighbors, STINGER_EDGE_DEST, source);
      fprintf(vtx_file, ",\n");
      stinger_vertex_to_json_with_type_strings(stinger_vertices_get(S), stinger_vtype_names_get(S), stinger_physmap_get(S), STINGER_EDGE_DEST, vtx_file, 2);
      fputc('\0',vtx_file);
      fflush(vtx_file);
      string_append_cstr(&vertices, vtx_str);
      fseek(vtx_file, 0, SEEK_SET);
    }

    char * etype = stinger_etype_names_lookup_name(S, STINGER_EDGE_TYPE);
    if(!edge_added) {
      edge_added = 1;
      if(etype) {
	sprintf(edge_str, "{ \"type\" : \"%s\", \"src\" : %ld, \"dest\" : %ld, \"weight\" : %ld, \"time1\" : %ld, \"time2\" : %ld, \"source\" : %ld, \"target\": %ld }", 
	  etype, STINGER_EDGE_SOURCE, STINGER_EDGE_DEST, STINGER_EDGE_WEIGHT, STINGER_EDGE_TIME_FIRST, STINGER_EDGE_TIME_RECENT, 0, source);
      } else {
	sprintf(edge_str, "{ \"type\" : \"%ld\", \"src\" : %ld, \"dest\" : %ld, \"weight\" : %ld, \"time1\" : %ld, \"time2\" : %ld, \"source\" : %ld, \"target\": %ld }", 
	  STINGER_EDGE_TYPE, STINGER_EDGE_SOURCE, STINGER_EDGE_DEST, STINGER_EDGE_WEIGHT, STINGER_EDGE_TIME_FIRST, STINGER_EDGE_TIME_RECENT, 0, source);
      }
    } else {
      if(etype) {
	sprintf(edge_str, ",\n{ \"type\" : \"%s\", \"src\" : %ld, \"dest\" : %ld, \"weight\" : %ld, \"time1\" : %ld, \"time2\" : %ld, \"source\" : %ld, \"target\": %ld }", 
	  etype, STINGER_EDGE_SOURCE, STINGER_EDGE_DEST, STINGER_EDGE_WEIGHT, STINGER_EDGE_TIME_FIRST, STINGER_EDGE_TIME_RECENT, 0, source);
      } else {
	sprintf(edge_str, ",\n{ \"type\" : \"%ld\", \"src\" : %ld, \"dest\" : %ld, \"weight\" : %ld, \"time1\" : %ld, \"time2\" : %ld, \"source\" : %ld, \"target\": %ld }", 
	  STINGER_EDGE_TYPE, STINGER_EDGE_SOURCE, STINGER_EDGE_DEST, STINGER_EDGE_WEIGHT, STINGER_EDGE_TIME_FIRST, STINGER_EDGE_TIME_RECENT, 0, source);
      }
    }

    string_append_cstr(&edges, edge_str);
    uint64_t dest = STINGER_EDGE_DEST;

    STINGER_FORALL_EDGES_OF_VTX_BEGIN(S, dest) {

      int64_t target = int_hm_seq_get(neighbors, STINGER_EDGE_DEST);
      if(INT_HT_SEQ_EMPTY != target) {
	char * etype = stinger_etype_names_lookup_name(S, STINGER_EDGE_TYPE);
	if(etype) {
	  sprintf(edge_str, ",\n{ \"type\" : \"%s\", \"src\" : %ld, \"dest\" : %ld, \"weight\" : %ld, \"time1\" : %ld, \"time2\" : %ld, \"source\" : %ld, \"target\": %ld }", 
	    etype, STINGER_EDGE_SOURCE, STINGER_EDGE_DEST, STINGER_EDGE_WEIGHT, STINGER_EDGE_TIME_FIRST, STINGER_EDGE_TIME_RECENT, source, target);
	} else {
	  sprintf(edge_str, ",\n{ \"type\" : \"%ld\", \"src\" : %ld, \"dest\" : %ld, \"weight\" : %ld, \"time1\" : %ld, \"time2\" : %ld, \"source\" : %ld, \"target\": %ld }", 
	    STINGER_EDGE_TYPE, STINGER_EDGE_SOURCE, STINGER_EDGE_DEST, STINGER_EDGE_WEIGHT, STINGER_EDGE_TIME_FIRST, STINGER_EDGE_TIME_RECENT, source, target);
	}
	string_append_cstr(&edges, edge_str);
      }

    } STINGER_FORALL_EDGES_OF_VTX_END();

  } STINGER_FORALL_EDGES_OF_VTX_END();

  string_append_cstr(&vertices, "\n], ");
  string_append_cstr(&edges, "\n]\n} ");

  string_t * egonet = string_new_from_cstr("{\n\t");
  string_append_string(egonet, &vertices);
  string_append_string(egonet, &edges);

  int_hm_seq_free(neighbors);
  fclose(vtx_file);
  string_free_internal(&vertices);
  string_free_internal(&edges);
  free (vtx_str);
  return egonet;
}

/* produces the union of the egonets of the stinger vertex IDs in
the group array in JSON form */
string_t *
group_to_json(stinger_t * S, int64_t * group, int64_t groupsize) {
  string_t vertices, edges;
  string_init_from_cstr(&vertices, "\"vertices\" : [ \n");
  string_init_from_cstr(&edges, "\"edges\" : [ \n");

  char *vtx_str, *edge_str;
  FILE * vtx_file;
  int edge_added = 0;

  int64_t which = 0;
  int first_vtx = 1;
  int_hm_seq_t * neighbors = int_hm_seq_new(1);

  vtx_str = xmalloc (2UL<<20UL);
  edge_str = &vtx_str[1UL<<20UL];
  vtx_file = fmemopen(vtx_str, sizeof(vtx_str) * 1UL<<20UL, "w");

  for(int64_t v = 0; v < groupsize; v++) {
    int64_t group_vtx = int_hm_seq_get(neighbors, group[v]);
    if(INT_HT_SEQ_EMPTY == group_vtx) {
      group_vtx = which++;
      int_hm_seq_insert(neighbors, group[v], group_vtx);
      if(!first_vtx) {
	fprintf(vtx_file, ",\n");
      } else {
	first_vtx = 0;
      }
      stinger_vertex_to_json_with_type_strings(stinger_vertices_get(S), stinger_vtype_names_get(S), stinger_physmap_get(S), group[v], vtx_file, 2);
      fputc('\0',vtx_file);
      fflush(vtx_file);
      string_append_cstr(&vertices, vtx_str);
      fseek(vtx_file, 0, SEEK_SET);
    }

    STINGER_FORALL_EDGES_OF_VTX_BEGIN(S, group[v]) {
      int64_t source = int_hm_seq_get(neighbors, STINGER_EDGE_DEST);
      if(INT_HT_SEQ_EMPTY == source) {
	source = which++;
	int_hm_seq_insert(neighbors, STINGER_EDGE_DEST, source);
	fprintf(vtx_file, ",\n");
	stinger_vertex_to_json_with_type_strings(stinger_vertices_get(S), stinger_vtype_names_get(S), stinger_physmap_get(S), STINGER_EDGE_DEST, vtx_file, 2);
	fputc('\0',vtx_file);
	fflush(vtx_file);
	string_append_cstr(&vertices, vtx_str);
	fseek(vtx_file, 0, SEEK_SET);
      }
    } STINGER_FORALL_EDGES_OF_VTX_END();
  }

  for(int64_t v = 0; v < neighbors->size; v++) {
    int64_t cur = neighbors->keys[v];
    if(cur != INT_HT_SEQ_EMPTY) {
      int64_t cur_val = neighbors->vals[v];
      STINGER_FORALL_EDGES_OF_VTX_BEGIN(S, cur) {
	if(int_hm_seq_get(neighbors, STINGER_EDGE_DEST) != INT_HT_SEQ_EMPTY && STINGER_EDGE_SOURCE < STINGER_EDGE_DEST) {
	  char * etype = stinger_etype_names_lookup_name(S, STINGER_EDGE_TYPE);
	  if(!edge_added) {
	    edge_added = 1;
	    if(etype) {
	      sprintf(edge_str, "{ \"type\" : \"%s\", \"src\" : %ld, \"dest\" : %ld, \"weight\" : %ld, \"time1\" : %ld, \"time2\" : %ld, \"source\" : %ld, \"target\": %ld }", 
		etype, STINGER_EDGE_SOURCE, STINGER_EDGE_DEST, STINGER_EDGE_WEIGHT, STINGER_EDGE_TIME_FIRST, STINGER_EDGE_TIME_RECENT, cur_val, int_hm_seq_get(neighbors, STINGER_EDGE_DEST));
	    } else {
	      sprintf(edge_str, "{ \"type\" : \"%ld\", \"src\" : %ld, \"dest\" : %ld, \"weight\" : %ld, \"time1\" : %ld, \"time2\" : %ld, \"source\" : %ld, \"target\": %ld }", 
		STINGER_EDGE_TYPE, STINGER_EDGE_SOURCE, STINGER_EDGE_DEST, STINGER_EDGE_WEIGHT, STINGER_EDGE_TIME_FIRST, STINGER_EDGE_TIME_RECENT, cur_val, int_hm_seq_get(neighbors, STINGER_EDGE_DEST));
	    }
	  } else {
	    if(etype) {
	      sprintf(edge_str, ",\n{ \"type\" : \"%s\", \"src\" : %ld, \"dest\" : %ld, \"weight\" : %ld, \"time1\" : %ld, \"time2\" : %ld, \"source\" : %ld, \"target\": %ld }", 
		etype, STINGER_EDGE_SOURCE, STINGER_EDGE_DEST, STINGER_EDGE_WEIGHT, STINGER_EDGE_TIME_FIRST, STINGER_EDGE_TIME_RECENT, cur_val, int_hm_seq_get(neighbors, STINGER_EDGE_DEST));
	    } else {
	      sprintf(edge_str, ",\n{ \"type\" : \"%ld\", \"src\" : %ld, \"dest\" : %ld, \"weight\" : %ld, \"time1\" : %ld, \"time2\" : %ld, \"source\" : %ld, \"target\": %ld }", 
		STINGER_EDGE_TYPE, STINGER_EDGE_SOURCE, STINGER_EDGE_DEST, STINGER_EDGE_WEIGHT, STINGER_EDGE_TIME_FIRST, STINGER_EDGE_TIME_RECENT, cur_val, int_hm_seq_get(neighbors, STINGER_EDGE_DEST));
	    }
	  }
	  string_append_cstr(&edges, edge_str);
	}
      } STINGER_FORALL_EDGES_OF_VTX_END();
    }
  }

  string_append_cstr(&vertices, "\n], ");
  string_append_cstr(&edges, "\n]\n} ");

  string_t * group_str = string_new_from_cstr("{\n\t");
  string_append_string(group_str, &vertices);
  string_append_string(group_str, &edges);

  /* fprintf(stderr, "GROUP OF VERTICES ("); */
  /* for(int64_t v = 0; v < groupsize; v++) { */
  /*   fprintf(stderr, "%ld,", group[v]); */
  /* } */
  /* fprintf(stderr, "):\n%s", group_str->str); */

  int_hm_seq_free(neighbors);
  fclose(vtx_file);
  string_free_internal(&vertices);
  string_free_internal(&edges);
  free (vtx_str);
  return group_str;
}

/* produces the subgraph starting at vtx and including all neighboring
 * vertices with the same label */
string_t *
labeled_subgraph_to_json(stinger_t * S, int64_t src, int64_t * labels, const int64_t vtxlimit_in) {
  const int64_t vtxlimit = (vtxlimit_in < 1 || vtxlimit_in > STINGER_MAX_LVERTICES? STINGER_MAX_LVERTICES : vtxlimit_in);
  string_t vertices, edges;
  string_init_from_cstr(&vertices, "\"vertices\" : [ \n");
  string_init_from_cstr(&edges, "\"edges\" : [ \n");

  int_hm_seq_t * neighbors = int_hm_seq_new(stinger_outdegree_get(S, src));

  char *vtx_str, *edge_str;
  FILE * vtx_file;

  int64_t which = 0;

  vtx_str = xmalloc (2UL<<20UL);
  edge_str = &vtx_str[1UL<<20UL];
  vtx_file = fmemopen(vtx_str, sizeof(vtx_str) * 1UL<<20UL, "w");

  uint8_t * found = xcalloc(sizeof(uint8_t), STINGER_MAX_LVERTICES);
  int64_t * queue = xmalloc(sizeof(int64_t) * (vtxlimit < 1? 1 : vtxlimit));

  queue[0] = src;
  found[src] = 1;
  int64_t q_top = 1;

  int first_vtx = 1;
  int edge_added = 0;

  for(int64_t q_cur = 0; q_cur < q_top; q_cur++) {
    int64_t group_vtx = which++;
    int_hm_seq_insert(neighbors, queue[q_cur], group_vtx);
    if(!first_vtx) {
      fprintf(vtx_file, ",\n");
    } else {
      first_vtx = 0;
    }
    stinger_vertex_to_json_with_type_strings(stinger_vertices_get(S), stinger_vtype_names_get(S), stinger_physmap_get(S), queue[q_cur], vtx_file, 2);
    fputc('\0',vtx_file);
    fflush(vtx_file);
    string_append_cstr(&vertices, vtx_str);
    fseek(vtx_file, 0, SEEK_SET);

    found[queue[q_cur]] = 2;

    STINGER_FORALL_EDGES_OF_VTX_BEGIN(S, queue[q_cur]) {
      if(found[STINGER_EDGE_DEST] > 1) {
	char * etype = stinger_etype_names_lookup_name(S, STINGER_EDGE_TYPE);
	if(!edge_added) {
	  edge_added = 1;
	  if(etype) {
	    sprintf(edge_str, "{ \"type\" : \"%s\", \"src\" : %ld, \"dest\" : %ld, \"weight\" : %ld, \"time1\" : %ld, \"time2\" : %ld, \"source\" : %ld, \"target\": %ld }", 
	      etype, STINGER_EDGE_SOURCE, STINGER_EDGE_DEST, STINGER_EDGE_WEIGHT, STINGER_EDGE_TIME_FIRST, STINGER_EDGE_TIME_RECENT, group_vtx, int_hm_seq_get(neighbors, STINGER_EDGE_DEST));
	  } else {
	    sprintf(edge_str, "{ \"type\" : \"%ld\", \"src\" : %ld, \"dest\" : %ld, \"weight\" : %ld, \"time1\" : %ld, \"time2\" : %ld, \"source\" : %ld, \"target\": %ld }", 
	      STINGER_EDGE_TYPE, STINGER_EDGE_SOURCE, STINGER_EDGE_DEST, STINGER_EDGE_WEIGHT, STINGER_EDGE_TIME_FIRST, STINGER_EDGE_TIME_RECENT, group_vtx, int_hm_seq_get(neighbors, STINGER_EDGE_DEST));
	  }
	} else {
	  if(etype) {
	    sprintf(edge_str, ",\n{ \"type\" : \"%s\", \"src\" : %ld, \"dest\" : %ld, \"weight\" : %ld, \"time1\" : %ld, \"time2\" : %ld, \"source\" : %ld, \"target\": %ld }", 
	      etype, STINGER_EDGE_SOURCE, STINGER_EDGE_DEST, STINGER_EDGE_WEIGHT, STINGER_EDGE_TIME_FIRST, STINGER_EDGE_TIME_RECENT, group_vtx, int_hm_seq_get(neighbors, STINGER_EDGE_DEST));
	  } else {
	    sprintf(edge_str, ",\n{ \"type\" : \"%ld\", \"src\" : %ld, \"dest\" : %ld, \"weight\" : %ld, \"time1\" : %ld, \"time2\" : %ld, \"source\" : %ld, \"target\": %ld }", 
	      STINGER_EDGE_TYPE, STINGER_EDGE_SOURCE, STINGER_EDGE_DEST, STINGER_EDGE_WEIGHT, STINGER_EDGE_TIME_FIRST, STINGER_EDGE_TIME_RECENT, group_vtx, int_hm_seq_get(neighbors, STINGER_EDGE_DEST));
	  }
	}
	string_append_cstr(&edges, edge_str);
      } else if(q_top < vtxlimit && !found[STINGER_EDGE_DEST] && labels[STINGER_EDGE_SOURCE] == labels[STINGER_EDGE_DEST]) {
	found[STINGER_EDGE_DEST] = 1;
	queue[q_top++] = STINGER_EDGE_DEST;
      }
    } STINGER_FORALL_EDGES_OF_VTX_END();
  }

  string_append_cstr(&vertices, "\n], ");
  string_append_cstr(&edges, "\n]\n} ");

  string_t * group_str = string_new_from_cstr("{\n\t");
  string_append_string(group_str, &vertices);
  string_append_string(group_str, &edges);

  int_hm_seq_free(neighbors);
  free(found);
  free(queue);
  fclose(vtx_file);
  string_free_internal(&vertices);
  string_free_internal(&edges);
  free (vtx_str);
  return group_str;
}

/*
stupid quick single-seet-set expansion hack.  easily generalizable, but kinda sucky.

first entry into queue:
  scan neighbors for total vol, current W
subsequent notices:
  increment current W

volume of region increases with every new vertex.  changes *all* priorities.

so...  keep frontier as (vtx, W, vol).  find least every time, swap to first.
*/

static void
expand_vtx (int64_t vtx, int64_t *nset_out, int64_t * restrict set,
	    int64_t * restrict mark, const int64_t * restrict label,
	    int64_t vtxlimit, const struct stinger * restrict S)
{
  assert (nset_out);
  assert (set);
  int64_t nset = 1, front_end = 1;
  const int64_t NV = STINGER_MAX_LVERTICES;
  int64_t * restrict Wvol = xmalloc (NV * 2 * sizeof (*Wvol));
  int64_t setvol = 0;
  const int64_t totweight = S->vertices->total_weight;
  const int64_t vlbl = (label? label[vtx] : -1);

  if (vtxlimit > NV) vtxlimit = NV;
  /* XXX: Might be wise: if (vtxlimit > 100) vtxlimit = 100; */
  set[0] = vtx;
  mark[vtx] = 0;

  while (nset < vtxlimit) {
    /* Expand last vtx. */
    const int64_t cur = nset - 1;
    const int64_t u = set[cur];
    assert(u >= 0);
    assert(u < NV);
    int64_t uvol = 0;

    STINGER_FORALL_EDGES_OF_VTX_BEGIN(S, u) {
      const int64_t v = STINGER_EDGE_DEST;
      assert(v >= 0);
      assert(v < NV);
      if (!label || vlbl == label[v]) {
	const int64_t wgt = STINGER_EDGE_WEIGHT;
	uvol += wgt;
	int64_t where = mark[v];
	assert(where < NV);
	if (where >= 0) {
	  /* XXX: May be over-counting, but produces a more clustered-looking visual. */
	  Wvol[2*where] += wgt;
	} else {
	  int64_t vW = 0;
	  int64_t vvol = 0;
	  where = front_end++;
	  assert(where >= 0);
	  assert(where < NV);
	  STINGER_FORALL_EDGES_OF_VTX_BEGIN(S, v) {
	    const int64_t z = STINGER_EDGE_DEST;
	    assert(z >= 0);
	    assert(z < NV);
	    const int64_t vz_weight = STINGER_EDGE_WEIGHT;
	    vvol += vz_weight;
	    if (mark[z] >= 0) vW += vz_weight;
	  } STINGER_FORALL_EDGES_OF_VTX_END();
	  set[where] = v;
	  Wvol[2*where] = vW;
	  Wvol[1+2*where] = vvol;
	  mark[v] = where;
	}
      }
    } STINGER_FORALL_EDGES_OF_VTX_END();

    setvol += uvol;

    if (nset == front_end) break;

    /* Choose next vtx. */
    double best_score;
    const double setvol_scale = setvol / (double)totweight;
    int64_t best_loc;

    best_loc = nset;
    best_score = Wvol[2*best_loc] - Wvol[1+2*best_loc] * setvol_scale;

    for (int64_t k = nset+1; k < front_end; ++k) {
      /* constant factor of two and iteration constant total_weight ignored. */
      const double score = Wvol[2*k] - Wvol[1+2*k] * setvol_scale;
      if ((score > best_score) || (score == best_score && (Wvol[2*k] > Wvol[2*best_loc]))) {
	best_loc = k;
	best_score = score;
      }
    }

    if ((!label && best_score < 0) ) /* Nothing improves the set, done. */
      break;

    assert (best_loc < NV);
    int64_t where = nset++;
    assert (where >= 0);
    assert (where < NV);
    if (best_loc != where) {
      /* Move into position */
      int64_t t = set[best_loc];
      Wvol[2*best_loc] = Wvol[2*where];
      Wvol[1+2*best_loc] = Wvol[1+2*where];
      set[best_loc] = set[where];
      set[where] = t;
    }
  }

  *nset_out = nset;
  free (Wvol);
}

string_t *
vtxcomm_to_json(stinger_t * S, int64_t vtx, int64_t vtxlimit) {
  const int64_t NV = STINGER_MAX_LVERTICES;
  //if (vtxlimit == 0 || vtxlimit > 100) vtxlimit = 100;
  int64_t nset = 0;
  int64_t * set = xmalloc (2 * NV * sizeof (*set));
  int64_t * restrict mark = &set[NV];
  string_t * out = NULL;

  for (int64_t k = 0; k < NV; ++k)
    mark[k] = -1;
  expand_vtx (vtx, &nset, set, mark, NULL, vtxlimit, S);
  for (int64_t k = 0; k < NV; ++k)
    mark[k] = 0;

  for (int64_t k = 0; k < nset; ++k) {
    assert(set[k] >= 0);
    assert(set[k] < NV);
    mark[set[k]] = 1;
  }
  out = labeled_subgraph_to_json (S, vtx, mark, vtxlimit);
  /* Expands too much? out = group_to_json (S, set, nset); */
  free (set);
  return out;
}

string_t *
vtxcomm_label_to_json(stinger_t * S, int64_t vtx, int64_t vtxlimit,
		      const int64_t * label)
{
  const int64_t NV = STINGER_MAX_LVERTICES;
  int64_t nset = 0;
  int64_t * set = xmalloc (2 * NV * sizeof (*set));
  int64_t * restrict mark = &set[NV];
  string_t * out = NULL;

  for (int64_t k = 0; k < NV; ++k)
    mark[k] = -1;
  expand_vtx (vtx, &nset, set, mark, label, vtxlimit, S);
  for (int64_t k = 0; k < NV; ++k)
    mark[k] = 0;

  for (int64_t k = 0; k < nset; ++k) {
    assert(set[k] >= 0);
    assert(set[k] < NV);
    mark[set[k]] = 1;
  }
  out = labeled_subgraph_to_json (S, vtx, mark, vtxlimit);
  /* Expands too much? out = group_to_json (S, set, nset); */
  free (set);
  return out;
}
