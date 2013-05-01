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
	} break;
	case STRINGS_ONLY: {
	} break;
	case MIXED: {
	} break;
      }
    }
  }

  return 0;
}
