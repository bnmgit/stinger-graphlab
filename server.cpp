extern "C" {
#include "stinger.h"
#include "web.h"
#include "xmalloc.h"
}

#include "stinger-batch.pb.h"

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

void
components_init(struct stinger * S, int64_t nv, int64_t * component_map);

void
components_batch(struct stinger * S, int64_t nv, int64_t * component_map);

#define V_A(X,...) fprintf(stdout, "%s %s %d:\n\t" #X "\n", __FILE__, __func__, __LINE__, __VA_ARGS__);
#define V(X) V_A(X,NULL)

static inline int64_t
int64_fetch_add (int64_t * p, int64_t incr) FN_MAY_BE_UNUSED;
static inline int
bool_int64_compare_and_swap (int64_t * p, int64_t oldval, int64_t newval)
  FN_MAY_BE_UNUSED;

MTA("mta inline") MTA("mta expect parallel")
int64_t
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

MTA("mta inline") MTA("mta expect parallel")
int
bool_int64_compare_and_swap (int64_t * p, int64_t oldval, int64_t newval)
{
#if defined(__MTA__)
  int64_t t = *p;
  if (t == oldval) {
    int64_t t;
    t = readfe (p);
    if (t == oldval) {
      writeef (p, newval);
    } else {
      writeef (p, oldval);
    }
  }
  return t == oldval;
#elif defined(_OPENMP)&&(defined(__GNUC__)||defined(__INTEL_COMPILER))
  return __sync_bool_compare_and_swap (p, oldval, newval);
#elif defined(_OPENMP)
#error "Atomics not defined..."
#else
  int64_t t = *p;
  if (t == oldval) *p = newval;
  return t == oldval;
#endif
}

MTA("mta inline") MTA("mta expect parallel context")
static inline int
append_to_vlist (int64_t * restrict nvlist,
                 int64_t * restrict vlist,
                 int64_t * restrict mark /* |V|, init to -1 */,
                 const int64_t i)
{
  int out = 0;
  if (mark[i] < 0) {
    int64_t where;
    int64_t marki;
#if defined(__MTA__)
    marki = readfe (&mark[i]);
#else
    marki = mark[i];
#endif
    if (marki == -1) { /* Superfluous test sequentially */
      if (bool_int64_compare_and_swap (&mark[i], -1, -2)) {
        /* Own it. */
        where = int64_fetch_add (nvlist, 1);
        marki = where;
        vlist[where] = i;
        out = 1;
#if !defined(__MTA__)
        mark[i] = where;
#endif
      }
    }
#if defined(__MTA__)
    writeef (&mark[i], marki);
#endif
  }

  return out;
}

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
	      int64_t * nvlist, int64_t * restrict vlist,
	      int64_t * restrict mark)
{

  *nvlist = 0;

  switch(batch.type()) {

    case NUMBERS_ONLY: OMP("omp parallel") {

      OMP("omp for nowait")
      for(int i = 0; i < batch.insertions_size(); i++) {
	const EdgeInsertion & in = batch.insertions(i);
	const int64_t u = in.source ();
	const int64_t v = in.destination ();
	stinger_incr_edge_pair(S, in.type(), u, v, in.weight(), in.time());
	append_to_vlist (nvlist, vlist, mark, u);
	append_to_vlist (nvlist, vlist, mark, v);
      }

      OMP("omp for")
      for(int d = 0; d < batch.deletions_size(); d++) {
	const EdgeDeletion & del = batch.deletions(d);
	const int64_t u = del.source ();
	const int64_t v = del.destination ();
	stinger_remove_edge_pair(S, del.type(), del.source(), del.destination());
	append_to_vlist (nvlist, vlist, mark, u);
	append_to_vlist (nvlist, vlist, mark, v);
      }

    } break;

    case STRINGS_ONLY: OMP("omp parallel") {

      OMP("omp for nowait")
      for(int i = 0; i < batch.insertions_size(); i++) {
	const EdgeInsertion & in = batch.insertions(i);
	int64_t src, dest;
	stinger_mapping_create(S, in.source_str().c_str(), in.source_str().length(), &src);
	stinger_mapping_create(S, in.destination_str().c_str(), in.destination_str().length(), &dest);

	stinger_incr_edge_pair(S, in.type(), src, dest, in.weight(), in.time());
	append_to_vlist (nvlist, vlist, mark, src);
	append_to_vlist (nvlist, vlist, mark, dest);
      }

      OMP("omp for")
      for(int d = 0; d < batch.deletions_size(); d++) {
	const EdgeDeletion & del = batch.deletions(d);
	int64_t src, dest;
	src = stinger_mapping_lookup(S, del.source_str().c_str(), del.source_str().length());
	dest = stinger_mapping_lookup(S, del.destination_str().c_str(), del.destination_str().length());

	if(src != -1 && dest != -1) {
	  stinger_remove_edge_pair(S, del.type(), src, dest);
	  append_to_vlist (nvlist, vlist, mark, src);
	  append_to_vlist (nvlist, vlist, mark, dest);
	}
      }

    } break;

    case MIXED: {

      OMP("omp parallel for")
      for(int i = 0; i < batch.insertions_size(); i++) {
	const EdgeInsertion & in = batch.insertions(i);
	int64_t src, dest;

	if(in.has_source()) {
	  src = in.source();
	} else {
	  stinger_mapping_create(S, in.source_str().c_str(), in.source_str().length(), &src);
	}

	if(in.has_destination()) {
	  dest = in.destination();
	} else {
	  stinger_mapping_create(S, in.destination_str().c_str(), in.destination_str().length(), &dest);
	}

	stinger_incr_edge_pair(S, in.type(), src, dest, in.weight(), in.time());
	append_to_vlist (nvlist, vlist, mark, src);
	append_to_vlist (nvlist, vlist, mark, dest);
      }

      OMP("omp parallel for")
      for(int d = 0; d < batch.deletions_size(); d++) {
	const EdgeDeletion & del = batch.deletions(d);
	int64_t src, dest;

	if(del.has_source()) {
	  src = del.source();
	} else {
	  src = stinger_mapping_lookup(S, del.source_str().c_str(), del.source_str().length());
	}

	if(del.has_destination()) {
	  dest = del.destination();
	} else {
	  dest = stinger_mapping_lookup(S, del.destination_str().c_str(), del.destination_str().length());
	}

	if(src != -1 && dest != -1) {
	  stinger_remove_edge_pair(S, del.type(), src, dest);
	  append_to_vlist (nvlist, vlist, mark, src);
	  append_to_vlist (nvlist, vlist, mark, dest);
	}
      }
    } break;
  }

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

  uint8_t * buffer = (uint8_t *)malloc(buffer_size);
  if(!buffer) {
    perror("Buffer alloc failed.\n");
    exit(-1);
  }

  int64_t * components = (int64_t *)xmalloc(sizeof(int64_t) * STINGER_MAX_LVERTICES);
  int64_t * communities = (int64_t *)xmalloc(sizeof(int64_t) * STINGER_MAX_LVERTICES);
  double * centralities = (double *)xmalloc(sizeof(double) * STINGER_MAX_LVERTICES);

  int64_t * mark = (int64_t*) xmalloc (sizeof (*mark) * STINGER_MAX_LVERTICES);
  int64_t nvlist = 0;
  int64_t * vlist = (int64_t*) xmalloc (sizeof (*mark) * STINGER_MAX_LVERTICES);

  OMP("omp parallel for")
    for (int64_t k = 0; k < STINGER_MAX_LVERTICES; ++k)
      mark[k] = -1;

  web_set_label_container("ConnectedComponentIDs", components);
  web_set_label_container("CommunityIDs", communities);
  web_set_score_container("BetweennessCentralities", centralities);

  components_init(S, STINGER_MAX_LVERTICES, components);

  while(1) {
    int accept_handle = accept(sock_handle, NULL, NULL);

    V("Ready to accept messages.");
    while(1) {

      ssize_t bytes_received = 0;

      while(bytes_received < sizeof(int64_t))
	bytes_received += recv(accept_handle, buffer, sizeof(int64_t) - bytes_received, 0);

      int64_t message_size = *((int64_t *)buffer);
      if(message_size > 0) {
	buffer += sizeof(int64_t);

	bytes_received = 0;

	while(bytes_received < message_size)
	  bytes_received += recv(accept_handle, buffer, message_size - bytes_received, 0);

	V_A("Received message of size %ld", (long)message_size);
	StingerBatch batch;

	if(batch.ParseFromString((const char *)buffer)) {

	  //batch.PrintDebugString();

	  process_batch(S, batch, &nvlist, vlist, mark);

	  components_batch(S, STINGER_MAX_LVERTICES, components);

	  OMP("omp parallel for")
	    for (int64_t k = 0; k < nvlist; ++k)
	      mark[vlist[k]] = -1;
	}

	buffer -= sizeof(int64_t);
      } else if(message_size == -1) {
	break;
      } else {
	V_A("Received message of size %ld", (long)message_size);
	abort();
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
