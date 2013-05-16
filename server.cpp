extern "C" {
#include "stinger.h"
}

#include "stinger-batch.pb.h"

#include <cstdio>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

using namespace gt::stinger;

#define V_A(X,...) fprintf(stdout, "%s %s %d:\n\t" #X "\n", __FILE__, __func__, __LINE__, __VA_ARGS__);
#define V(X) V_A(X,NULL)

int
main(int argc, char *argv[])
{
  struct stinger * S = stinger_new();

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

  while(1) {
    V("Ready to accept messages.");

    int accept_handle = accept(sock_handle, NULL, NULL);
    ssize_t bytes_received = recv(accept_handle, buffer, buffer_size, 0);

    V_A("Received message of size %ld", (long)bytes_received);
    if(bytes_received > 0) {
      StingerBatch batch;
      batch.ParseFromString((const char *)buffer);

      batch.PrintDebugString();

      switch(batch.type()) {
	case NUMBERS_ONLY: {
	  OMP("omp parallel for")
	  for(int i = 0; i < batch.insertions_size(); i++) {
	    const EdgeInsertion & in = batch.insertions(i);
	    stinger_incr_edge_pair(S, in.type(), in.source(), in.destination(), in.weight(), in.time());
	  }
	  OMP("omp parallel for")
	  for(int d = 0; d < batch.deletions_size(); d++) {
	    const EdgeDeletion & del = batch.deletions(d);
	    stinger_remove_edge_pair(S, del.type(), del.source(), del.destination());
	  }
	} break;
	case STRINGS_ONLY: {
	  OMP("omp parallel for")
	  for(int i = 0; i < batch.insertions_size(); i++) {
	    const EdgeInsertion & in = batch.insertions(i);
	    int64_t src, dest;
	    stinger_mapping_create(S, in.source_str().c_str(), in.source_str().length(), &src);
	    stinger_mapping_create(S, in.destination_str().c_str(), in.destination_str().length(), &dest);

	    stinger_incr_edge_pair(S, in.type(), src, dest, in.weight(), in.time());
	  }
	  OMP("omp parallel for")
	  for(int d = 0; d < batch.deletions_size(); d++) {
	    const EdgeDeletion & del = batch.deletions(d);
	    int64_t src, dest;
	    src = stinger_mapping_lookup(S, del.source_str().c_str(), del.source_str().length());
	    dest = stinger_mapping_lookup(S, del.destination_str().c_str(), del.destination_str().length());

	    if(src != -1 && dest != -1)
	      stinger_remove_edge_pair(S, del.type(), src, dest);
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

	    if(src != -1 && dest != -1)
	      stinger_remove_edge_pair(S, del.type(), src, dest);
	  }
	} break;
      }
    }
  }

  return 0;
}
