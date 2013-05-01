extern "C" {
#include "stinger.h"
#include "csv.h"
}

#include "stinger-batch.pb.h"

#include <cstdio>
#include <cstdlib>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>

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
  struct sockaddr_in server_addr;
  struct hostent * server;

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
	/* TODO
	server = gethostbyname(optarg);
	*/
      } break;

      case 's': {
	use_strings = 1;
      } break;

      case '?':
      case 'h': {
	printf("Usage:    %s [-p src_port] [-d dst_port] [-a server_addr] [-b buffer_size] [-s for strings] <input_csv>\n", argv[0]);
	printf("Defaults: src_port: %d buffer_size: %lu use_strings: %d\n", src_port, (unsigned long) buffer_size, use_strings);
	printf("\nCSV file format is is_delete,source,dest,weight,time where weight and time are"
		 "optional 64-bit integers and source and dest are either strings or"
		 "64-bit integers depending on options flags. is_delete should be 0 for edge\n"
		 "insertions and 1 for edge deletions.\n");
	exit(0);
      } break;
    }
  }

  if(optind >= argc) {
    perror("Expected input CSV filename. -? for help");
    exit(0);
  }

  V_A("Running with: src_port: %d dst_port: %d buffer_size: %lu string-vertices: %d\n", src_port, dst_port, (unsigned long) buffer_size, use_strings);

  int sock_handle;
  struct sockaddr_in sock_addr = { AF_INET, (in_port_t)src_port, 0};

  if(-1 == (sock_handle = socket(AF_INET, SOCK_STREAM, 0))) {
    perror("Socket create failed");
    exit(-1);
  }

  if(-1 == bind(sock_handle, (const sockaddr *)&sock_addr, sizeof(sock_addr))) {
    perror("Socket bind failed");
    exit(-1);
  }

  if(-1 == listen(sock_handle, 1)) {
    perror("Socket listen failed");
    exit(-1);
  }

  uint8_t * buffer = (uint8_t *)malloc(buffer_size);
  if(!buffer) {
    perror("Buffer alloc failed");
    exit(-1);
  }

  V("Parsing messages.");

  FILE * fp;
  char * buf, ** fields;
  uint64_t bufSize, * lengths, fieldsSize, count;
  int64_t line = 0;

  StingerBatch batch;
  batch.set_make_undirected(true);

  while(!feof(fp)) {
    line++;
    readCSVLineDynamic(',', fp, &buf, &bufSize, &fields, &lengths, &fieldsSize, &count);

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


  return 0;
}
