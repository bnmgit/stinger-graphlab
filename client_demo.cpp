extern "C" {
#include "stinger.h"
#include "csv.h"
}

#include "stinger-batch.pb.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>
#include <netdb.h>

using namespace gt::stinger;

#define E_A(X,...) fprintf(stderr, "%s %s %d:\n\t" #X "\n", __FILE__, __func__, __LINE__, __VA_ARGS__);
#define E(X) E_A(X,NULL)
#define V_A(X,...) fprintf(stdout, "%s %s %d:\n\t" #X "\n", __FILE__, __func__, __LINE__, __VA_ARGS__);
#define V(X) V_A(X,NULL)

enum csv_fields {
  FIELD_IS_DELETE,
  FIELD_SOURCE,
  FIELD_DEST,
  FIELD_WEIGHT,
  FIELD_TIME
};

int
main(int argc, char *argv[])
{
  /* global options */
  int src_port = 10102;
  int dst_port = 10101;
  uint64_t buffer_size = 1ULL << 28ULL;
  int use_strings = 0;
  struct sockaddr_in server_addr = { 0 };
  struct hostent * server = NULL;
  char * filename;

  int opt = 0;
  while(-1 != (opt = getopt(argc, argv, "p:b:d:a:s"))) {
    switch(opt) {
      case 'p': {
	src_port = atoi(optarg);
      } break;

      case 'd': {
	dst_port = atoi(optarg);
      } break;

      case 'b': {
	buffer_size = atol(optarg);
      } break;

      case 'a': {
	server = gethostbyname(optarg);
	if(NULL == server) {
	  E_A("ERROR: server %s could not be resolved.", optarg);
	  exit(-1);
	}
      } break;

      case 's': {
	use_strings = 1;
      } break;

      case '?':
      case 'h': {
	printf("Usage:    %s [-p src_port] [-d dst_port] [-a server_addr] [-b buffer_size] [-s for strings] <input_csv>\n", argv[0]);
	printf("Defaults: src_port: %d dest_port: %d server: localhost buffer_size: %lu use_strings: %d\n", src_port, dst_port, (unsigned long) buffer_size, use_strings);
	printf("\nCSV file format is is_delete,source,dest,weight,time where weight and time are\n"
		 "optional 64-bit integers and source and dest are either strings or\n"
		 "64-bit integers depending on options flags. is_delete should be 0 for edge\n"
		 "insertions and 1 for edge deletions.\n");
	exit(0);
      } break;
    }
  }

  if(optind >= argc) {
    perror("Expected input CSV filename. -? for help");
    exit(0);
  } else {
    filename = argv[optind];
  }

  V_A("Running with: src_port: %d dst_port: %d buffer_size: %lu string-vertices: %d\n", src_port, dst_port, (unsigned long) buffer_size, use_strings);

  if(NULL == server) {
    server = gethostbyname("localhost");
    if(NULL == server) {
      E_A("ERROR: server %s could not be resolved.", "localhost");
      exit(-1);
    }
  }

  server_addr.sin_family = AF_INET;
  memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
  server_addr.sin_port = dst_port;

  int sock_handle;
  struct sockaddr_in sock_addr = { AF_INET, (in_port_t)src_port, 0};

  if(-1 == (sock_handle = socket(AF_INET, SOCK_STREAM, 0))) {
    perror("Socket create failed");
    exit(-1);
  }

  /*if(-1 == bind(sock_handle, (const sockaddr *)&sock_addr, sizeof(sock_addr))) {
    perror("Socket bind failed");
    exit(-1);
  }*/

  /*if(-1 == listen(sock_handle, 1)) {
    perror("Socket listen failed");
    exit(-1);
  }*/

  uint8_t * buffer = (uint8_t *)malloc(buffer_size);
  if(!buffer) {
    perror("Buffer alloc failed");
    exit(-1);
  }

  if(-1 == connect(sock_handle, (const sockaddr *)&server_addr, sizeof(server_addr))) {
    perror("Connection failed");
    exit(-1);
  }

  V("Parsing messages.");

  FILE * fp = fopen(filename, "r");
  char * buf = NULL, ** fields = NULL;
  uint64_t bufSize = 0, * lengths = NULL, fieldsSize = 0, count = 0;
  int64_t line = 0;

  StingerBatch batch;
  batch.set_make_undirected(true);

  while(!feof(fp)) {
    line++;
    readCSVLineDynamic(',', fp, &buf, &bufSize, &fields, &lengths, &fieldsSize, &count);

    if(count <= 1)
      continue;
    if(count < 3) {
      E_A("ERROR: too few elements on line %ld", (long) line);
      continue;
    }

    /* is insert? */
    if(fields[FIELD_IS_DELETE][0] == '0') {
      EdgeInsertion * insertion = batch.add_insertions();

      if(use_strings) {
	insertion->set_source_str(fields[FIELD_SOURCE]);
	insertion->set_destination_str(fields[FIELD_DEST]);
      } else {
	insertion->set_source(atol(fields[FIELD_SOURCE]));
	insertion->set_destination(atol(fields[FIELD_DEST]));
      }

      if(count > 3) {
	insertion->set_weight(atol(fields[FIELD_WEIGHT]));
	if(count > 4) {
	  insertion->set_time(atol(fields[FIELD_TIME]));
	}
      }

    } else {
      EdgeDeletion * deletion = batch.add_deletions();

      if(use_strings) {
	deletion->set_source_str(fields[FIELD_SOURCE]);
	deletion->set_destination_str(fields[FIELD_DEST]);
      } else {
	deletion->set_source(atol(fields[FIELD_SOURCE]));
	deletion->set_destination(atol(fields[FIELD_DEST]));
      }
    }
  }

  V("Sending messages.");

  std::string out_buffer;

  batch.SerializeToString(&out_buffer);

  write(sock_handle, out_buffer.c_str(), out_buffer.size());

  free(buffer);
  return 0;
}
