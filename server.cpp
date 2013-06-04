extern "C" {
#include "stinger.h"
#include "timer.h"
#include "web.h"
#include "xmalloc.h"
#include "src-demo/defs.h"
#include "src-demo/graph-el.h"
#include "src-demo/community.h"
#include "src-demo/community-update.h"
}

#include "stinger-batch.pb.h"
#include "send_rcv.h"

#if !defined(MTA)
#define MTA(x)
#endif

#if defined(__GNUC__)
#define FN_MAY_BE_UNUSED __attribute__((unused))
#else
#define FN_MAY_BE_UNUSED
#endif

#include <cstdio>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

using namespace gt::stinger;

struct community_state cstate;

MTA("mta inline")
MTA("mta expect parallel")
static inline int64_t
int64_fetch_add (int64_t * p, int64_t incr)
{
#if defined(__MTA__)
  return int_fetch_add (p, incr);
#elif defined(_OPENMP)
#if defined(__GNUC__)
  return __sync_fetch_and_add (p, incr);
#elif defined(__INTEL_COMPILER)
  return __sync_fetch_and_add (p, incr);
#else
#error "Atomics not defined..."
#endif
#else
  int64_t out = *p;
  *p += incr;
  return out;
#endif
}

void
components_init(struct stinger * S, int64_t nv, int64_t * component_map);

void
components_batch(struct stinger * S, int64_t nv, int64_t * component_map);

#define V_A(X,...) do { fprintf(stdout, "%s %s %d:\n\t" #X "\n", __FILE__, __func__, __LINE__, __VA_ARGS__); fflush (stdout); } while (0)
#define V(X) V_A(X,NULL)

#define ACCUM_INCR do {							\
  int64_t where;							\
  stinger_incr_edge_pair(S, in.type(), u, v, in.weight(), in.time());	\
  where = int64_fetch_add (&nincr, 1);					\
  incr[3*where+0] = u;							\
  incr[3*where+1] = v;							\
  incr[3*where+2] = in.weight();					\
 } while (0)

#define ACCUM_REM do {					\
  int64_t where;					\
  stinger_remove_edge_pair(S, del.type(), u, v);	\
  where = int64_fetch_add (&nrem, 1);			\
  rem[2*where+0] = u;					\
  rem[2*where+1] = v;					\
 } while (0)

/**
* @brief Inserts and removes the edges contained in a batch.
*
* Maps new vertices as needed for insertions.  Deletions with non-existant vertices
* are ignored.
*
* @param S A pointer to the STINGER structure.
* @param batch A reference to the protobuf
*
* @return 0 on success.
*/
int
process_batch(stinger_t * S, StingerBatch & batch,
	      struct community_state * cstate)
{
  int64_t nincr = 0, nrem = 0;
  int64_t * restrict incr = NULL;
  int64_t * restrict rem = NULL;

  incr = (int64_t*)xmalloc ((3 * batch.insertions_size () + 2 * batch.deletions_size ()) * sizeof (*incr));
  rem = &incr[3 * batch.insertions_size ()];

  OMP("omp parallel")
    switch (batch.type ()) {
    case NUMBERS_ONLY:
      OMP("omp for")
	for (size_t i = 0; i < batch.insertions_size(); i++) {
	  const EdgeInsertion & in = batch.insertions(i);
	  const int64_t u = in.source ();
	  const int64_t v = in.destination ();
	  ACCUM_INCR;
	}
      assert (nincr == batch.insertions_size ());

      OMP("omp for")
	for(size_t d = 0; d < batch.deletions_size(); d++) {
	  const EdgeDeletion & del = batch.deletions(d);
	  const int64_t u = del.source ();
	  const int64_t v = del.destination ();
	  ACCUM_REM;
	}
      assert (nrem == batch.deletions_size ());
      break;

    case STRINGS_ONLY:
      OMP("omp for")
	for (size_t i = 0; i < batch.insertions_size(); i++) {
	  const EdgeInsertion & in = batch.insertions(i);
	  int64_t u, v;
	  stinger_mapping_create(S, in.source_str().c_str(), in.source_str().length(), &u);
	  stinger_mapping_create(S, in.destination_str().c_str(), in.destination_str().length(), &v);
	  ACCUM_INCR;
	}
      assert (nincr == batch.insertions_size ());

      OMP("omp for")
	for(size_t d = 0; d < batch.deletions_size(); d++) {
	  const EdgeDeletion & del = batch.deletions(d);
	  int64_t u, v;
	  u = stinger_mapping_lookup(S, del.source_str().c_str(), del.source_str().length());
	  v = stinger_mapping_lookup(S, del.destination_str().c_str(), del.destination_str().length());

	  if(u != -1 && v != -1)
	    ACCUM_REM;
	}
      assert (nrem == batch.deletions_size ());
      break;

    case MIXED:
      OMP("omp for")
	for (size_t i = 0; i < batch.insertions_size(); i++) {
	  const EdgeInsertion & in = batch.insertions(i);
	  int64_t u, v;
	  if (in.has_source())
	    u = in.source();
	  else
	    stinger_mapping_create (S, in.source_str().c_str(),
				    in.source_str().length(), &u);

	  if (in.has_destination())
	    v = in.destination();
	  else
	    stinger_mapping_create(S, in.destination_str().c_str(),
				   in.destination_str().length(), &v);

	  ACCUM_INCR;
	}
      assert (nincr == batch.insertions_size ());

      OMP("omp for")
	for(size_t d = 0; d < batch.deletions_size(); d++) {
	  const EdgeDeletion & del = batch.deletions(d);
	  int64_t u, v;

	  if (del.has_source())
	    u = del.source();
	  else
	    u = stinger_mapping_lookup(S, del.source_str().c_str(), del.source_str().length());

	  if (del.has_destination())
	    v = del.destination();
	  else
	    v = stinger_mapping_lookup(S, del.destination_str().c_str(),
				       del.destination_str().length());

	  if(u != -1 && v != -1)
	    ACCUM_REM;
	}
      assert (nrem == batch.deletions_size ());
      break;

    default:
      abort ();
    }

  cstate_preproc (cstate, S, nincr, incr, nrem, rem);

  free (incr);
  return 0;
}

int
main(int argc, char *argv[])
{
  struct stinger * S = stinger_new();

  /* register edge and vertex types */
  int64_t vtype_twitter_handle;
  stinger_vtype_names_create_type(S, "TwitterHandle", &vtype_twitter_handle);

  int64_t etype_mention;
  stinger_vtype_names_create_type(S, "Mention", &etype_mention);

  web_start_stinger(S, "8088");

  /* global options */
  int port = 10101;
  uint64_t buffer_size = 1ULL << 28ULL;

  int opt = 0;
  while(-1 != (opt = getopt(argc, argv, "p:b:"))) {
    switch(opt) {
      case 'p': {
	port = atoi(optarg);
      } break;

      case 'b': {
	buffer_size = atol(optarg);
      } break;

      case '?':
      case 'h': {
	printf("Usage:    %s [-p port] [-b buffer_size]\n", argv[0]);
	printf("Defaults: port: %d buffer_size: %lu\n", port, (unsigned long) buffer_size);
	exit(0);
      } break;

    }
  }

  int sock_handle;
  struct sockaddr_in sock_addr = { AF_INET, (in_port_t)port, 0};

  if(-1 == (sock_handle = socket(AF_INET, SOCK_STREAM, 0))) {
    perror("Socket create failed.\n");
    exit(-1);
  }

  if(-1 == bind(sock_handle, (const sockaddr *)&sock_addr, sizeof(sock_addr))) {
    perror("Socket bind failed.\n");
    exit(-1);
  }

  if(-1 == listen(sock_handle, 1)) {
    perror("Socket listen failed.\n");
    exit(-1);
  }

  const char * buffer = (char *)malloc(buffer_size);
  if(!buffer) {
    perror("Buffer alloc failed.\n");
    exit(-1);
  }

  int64_t * components = (int64_t *)xmalloc(sizeof(int64_t) * STINGER_MAX_LVERTICES);
  double * centralities = (double *)xmalloc(sizeof(double) * STINGER_MAX_LVERTICES);
  double processing_time;

  init_empty_community_state (&cstate, STINGER_MAX_LVERTICES, 2*STINGER_MAX_LVERTICES);

  web_set_label_container("ConnectedComponentIDs", components);
  web_set_label_container("CommunityIDs", cstate.cmap);
  web_set_score_container("BetweennessCentralities", centralities);

  components_init(S, STINGER_MAX_LVERTICES, components);

  V_A("STINGER server listening for input on port %d, web server on port 8088.",
      (int)port);

  while(1) {
    int accept_handle = accept(sock_handle, NULL, NULL);

    V("Ready to accept messages.");
    while(1) {

      StingerBatch batch;
      if(recv_message(accept_handle, batch)) {

	V_A("Received message of size %ld", (long)batch.ByteSize());

	double processing_time_start;
	//batch.PrintDebugString();

	processing_time_start = tic ();

	process_batch(S, batch, &cstate);

#pragma omp parallel sections
	{
#pragma omp section
	  components_batch(S, STINGER_MAX_LVERTICES, components);

#pragma omp section
	  cstate_update (&cstate, S);
	}

	processing_time = tic () - processing_time_start;

	V_A("Number of non-singleton communities %ld/%ld, max size %ld, modularity %g",
	    (long)cstate.n_nonsingletons, (long)cstate.cg.nv,
	    (long)cstate.max_csize,
	    cstate.modularity);

	V_A("Total processing time: %g", processing_time);

	if(!batch.keep_alive())
	  break;

      } else {
	V("ERROR Parsing failed.\n");
      }
    }
  }

  return 0;
}

void
components_init(struct stinger * S, int64_t nv, int64_t * component_map) {
  /* Initialize each vertex with its own component label in parallel */
  OMP ("omp parallel for")
    for (uint64_t i = 0; i < nv; i++) {
      component_map[i] = i;
    }
  
  components_batch(S, nv, component_map);
}

void
components_batch(struct stinger * S, int64_t nv, int64_t * component_map) {
  /* Iterate until no changes occur */
  while (1) {
    int changed = 0;

    /* For all edges in the STINGER graph of type 0 in parallel, attempt to assign
       lesser component IDs to neighbors with greater component IDs */
    for(uint64_t t = 0; t < STINGER_NUMETYPES; t++) {
      STINGER_READ_ONLY_PARALLEL_FORALL_EDGES_BEGIN (S, t) {
	if (component_map[STINGER_RO_EDGE_DEST] <
	    component_map[STINGER_RO_EDGE_SOURCE]) {
	  component_map[STINGER_RO_EDGE_SOURCE] = component_map[STINGER_RO_EDGE_DEST];
	  changed++;
	}
      }
      STINGER_READ_ONLY_PARALLEL_FORALL_EDGES_END ();
    }

    /* if nothing changed */
    if (!changed)
      break;

    /* Tree climbing with OpenMP parallel for */
    OMP ("omp parallel for")
      MTA ("mta assert nodep")
      for (uint64_t i = 0; i < nv; i++) {
        while (component_map[i] != component_map[component_map[i]])
          component_map[i] = component_map[component_map[i]];
      }
  }
}
