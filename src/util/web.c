#include "util/json-support.h"
#include "astring.h"

#include "util/web.h"
#include "csv.h"
#include "kv_store.h"

#include <inttypes.h>
#include <errno.h>

/* XXX: Yuck, gross, don't do this. */
#include "../../src-demo/defs.h"
#include "../../src-demo/graph-el.h"
#include "../../src-demo/community.h"
#include "../../src-demo/community-update.h"

static stinger_t * S = NULL;
static kv_element_t labels;
static kv_element_t scores;
static kv_element_t tracker;
static int64_t vtxlimit = 0;

void
web_start_stinger(stinger_t * stinger, const char * port) {
  S = stinger;

  kv_store_init(&labels);
  kv_store_init(&scores);
  kv_tracker_init(&tracker);

  struct mg_context *ctx;
  struct mg_callbacks callbacks;

  // List of options. Last element must be NULL.
  const char *opts[] = {"listening_ports", port, NULL};

  // Prepare callbacks structure. We have only one callback, the rest are NULL.
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.begin_request = begin_request_handler;

  // Start the web server.
  ctx = mg_start(&callbacks, NULL, opts);
}

void
web_set_label_container(const char * name, int64_t * label) {
  kv_element_t * kve_name = new_kve_from_str_static(name);
  kv_tracker_append(&tracker, kve_name);

  kv_element_t * kve_labels = new_kve_from_ptr(label);
  kv_tracker_append(&tracker, kve_labels);

  kv_store_set(&labels, kve_name, kve_labels);
}

void
web_set_score_container(const char * name, double * score) {
  kv_element_t * kve_name = new_kve_from_str_static(name);
  kv_tracker_append(&tracker, kve_name);

  kv_element_t * kve_scores = new_kve_from_ptr(score);
  kv_tracker_append(&tracker, kve_scores);

  kv_store_set(&scores, kve_name, kve_scores);
}

string_t *
get_label_containers_json() {
  string_t * rtn = string_new_from_cstr("{\n\"labels\": [");
  kv_element_t ** ik, ** iv, * key, * val;
  int first = 1;

  kv_store_first(&labels, &ik, &iv, &key, &val);
  while (key) {
    string_t * name = kve_get_str(key);
    if(first) {
      string_append_cstr_len(rtn, "\"", 1);
      first = 0;
    } else {
      string_append_cstr_len(rtn, ",\n\"", 3);
    }
    string_append_string(rtn, name);
    string_append_cstr_len(rtn, "\"", 1);
    kv_store_next(&labels, &ik, &iv, &key, &val);
  }
  string_append_cstr_len(rtn, "\n] }", 4);
  return rtn;
}

string_t *
get_label_stats_json(char * name) {
  string_t * rtn = string_new_from_cstr("{\n");

  kv_element_t key = kve_from_str_static(name);
  kv_element_t * val;

  if(KV_SUCCESS == kv_store_get(&labels, &key, &val)) {
    int64_t * int_labels = kve_get_ptr(val);
    int64_t * counts = calloc(sizeof(int64_t), STINGER_MAX_LVERTICES);
    int64_t nv = stinger_mapping_nv(S);
    for(int64_t v = 0; v < nv; v++) {
      int64_t lab = int_labels[v];
      if(lab >= 0 && lab < STINGER_MAX_LVERTICES)
	counts[lab]++;
    }

    int64_t largest = 0;
    int64_t largest_size = INT64_MIN;
    int64_t smallest = 0;
    int64_t smallest_size = INT64_MAX;
    int64_t count = 0;
    double mean = 0;
    double mean_sq = 0;
    for(int64_t v = 0; v < nv; v++) {
      int64_t cur = counts[v];
      if(cur) {
	count++;
	if(cur < smallest_size) {
	  smallest = v;
	  smallest_size = cur;
	}
	if(cur > largest_size) {
	  largest = v;
	  largest_size = cur;
	}
	mean += cur;
	mean_sq += (cur * cur);
      }
    }
    mean /= count;
    mean_sq /= count;
    double var = mean_sq - (mean * mean);

    char stat[1024];

    sprintf(stat, "\"largest\":%ld,\n", largest);
    string_append_cstr(rtn, stat);

    sprintf(stat, "\"largest_size\":%ld,\n", largest_size);
    string_append_cstr(rtn, stat);

    sprintf(stat, "\"smallest\":%ld,\n", smallest);
    string_append_cstr(rtn, stat);

    sprintf(stat, "\"smallest_size\":%ld,\n", smallest_size);
    string_append_cstr(rtn, stat);

    sprintf(stat, "\"count\":%ld,\n", count);
    string_append_cstr(rtn, stat);

    sprintf(stat, "\"mean\":%lf,\n", mean);
    string_append_cstr(rtn, stat);

    sprintf(stat, "\"mean_sq\":%lf,\n", mean_sq);
    string_append_cstr(rtn, stat);

    sprintf(stat, "\"variance\":%lf\n", var);
    string_append_cstr(rtn, stat);

    free(counts);
  }
  string_append_cstr(rtn, "\n}");

  return rtn;
}

string_t *
get_labels_json(char * name) {
  string_t * rtn = string_new_from_cstr("{\n");

  kv_element_t key = kve_from_str_static(name);
  kv_element_t * val;

  if(KV_SUCCESS == kv_store_get(&labels, &key, &val)) {
    int64_t * int_labels = kve_get_ptr(val);
    int64_t nv = stinger_mapping_nv(S);
    for(int64_t v = 0; v < nv; v++) {
      char num[128];
      if(v != 0)
        sprintf(num, ",\n\"%ld\": %ld", v, int_labels[v]);
      else
        sprintf(num, "\"%ld\": %ld", v, int_labels[v]);
      string_append_cstr(rtn, num);
    }
  }
  string_append_cstr(rtn, "\n}");

  return rtn;
}

string_t *
get_scores_json(char * name) {
  string_t * rtn = string_new_from_cstr("{\n");

  kv_element_t key = kve_from_str_static(name);
  kv_element_t * val;

  if(KV_SUCCESS == kv_store_get(&scores, &key, &val)) {
    double * dbl_scores = kve_get_ptr(val);
    int64_t nv = stinger_mapping_nv(S);
    for(int64_t v = 0; v < nv; v++) {
      char num[128];
      if(v != 0)
        sprintf(num, ",\n\"%ld\": %lf", v, dbl_scores[v]);
      else
        sprintf(num, "\"%ld\": %lf", v, dbl_scores[v]);
      string_append_cstr(rtn, num);
    }
  }
  string_append_cstr(rtn, "\n}");

  return rtn;
}
string_t *
get_score_containers_json() {
  string_t * rtn = string_new_from_cstr("{\n\"scores\": [");
  kv_element_t ** ik, ** iv, * key, * val;
  int first = 1;

  kv_store_first(&scores, &ik, &iv, &key, &val);
  while (key) {
    string_t * name = kve_get_str(key);
    if(first) {
      string_append_cstr_len(rtn, "\"", 1);
      first = 0;
    } else {
      string_append_cstr_len(rtn, ",\n\"", 3);
    }
    string_append_string(rtn, name);
    string_append_cstr_len(rtn, "\"", 1);
    kv_store_next(&scores, &ik, &iv, &key, &val);
  }
  string_append_cstr_len(rtn, "\n] }", 4);
  return rtn;
}

string_t *
get_physid_json(stinger_t * S, char * stinger_ids) {
  string_t * rtn = string_new_from_cstr("{");

  char * sub = calloc(strlen(stinger_ids) + 2, sizeof(char));
  memcpy(sub, stinger_ids, strlen(stinger_ids));
  char * buf = NULL;
  uint64_t bufSize = 0;
  char ** fields = NULL;
  uint64_t * lengths = NULL;
  uint64_t fieldsSize = 0;
  uint64_t count = 0;

  splitLineCSVDynamic(',', sub, strlen(sub), &buf, &bufSize, &fields, &lengths, &fieldsSize, &count);

  int64_t found = 0;
  for(uint64_t i = 0; i < count; i++) {
    char * mapping;
    int64_t mapping_len;
    int64_t vtx = atol(fields[i]);
    if(-1 != stinger_mapping_physid_direct(S, vtx, &mapping, &mapping_len)) {
      if(found) {
	string_append_cstr(rtn, ",\n");
      }

      string_append_cstr(rtn, "  \"");
      string_append_cstr_len(rtn, fields[i], lengths[i]);
      string_append_cstr(rtn, "\": \"");
      string_append_cstr_len(rtn, mapping, mapping_len);
      string_append_cstr(rtn, "\"");

      found++;
    }
  }

  string_append_cstr(rtn, "\n}");

  free(sub);

  return rtn;
}

string_t *
get_stingerid_json(stinger_t * S, char * phys_ids) {
  string_t * rtn = string_new_from_cstr("{");

  char * sub = calloc(strlen(phys_ids) + 2, sizeof(char));
  memcpy(sub, phys_ids, strlen(phys_ids));
  char * buf = NULL;
  uint64_t bufSize = 0;
  char ** fields = NULL;
  uint64_t * lengths = NULL;
  uint64_t fieldsSize = 0;
  uint64_t count = 0;

  splitLineCSVDynamic(',', sub, strlen(sub), &buf, &bufSize, &fields, &lengths, &fieldsSize, &count);

  int64_t found = 0;
  char intbuf[256];
  for(uint64_t i = 0; i < count; i++) {
    int64_t vtx = stinger_mapping_lookup(S, fields[i], lengths[i]);
    if(vtx < STINGER_MAX_LVERTICES && vtx >= 0) {
      if(found) {
	string_append_cstr(rtn, ",\n");
      }

      string_append_cstr(rtn, "  \"");
      string_append_cstr(rtn, fields[i]);
      string_append_cstr(rtn, "\":");
      sprintf(intbuf, "%ld", vtx);
      string_append_cstr(rtn, intbuf);

      found++;
    }
  }

  string_append_cstr(rtn, "\n}");

  free(sub);

  return rtn;
}

int
begin_request_handler(struct mg_connection *conn)
{
  const struct mg_request_info *request_info = mg_get_request_info(conn);

  if(request_info->uri && (0 == strncmp(request_info->uri, "/data", 5) || strlen(request_info->uri) <= 1)) {
    return 0;
  }

  char * content = calloc(1048576, sizeof(char));
  int content_size = 1048576;
  int content_length = 0;

  /* XXX: Replace this with a kv-dumper someday */
  if(0 == strncmp(request_info->uri, "/uglystatshack", 14)) {
    char ugh[2049];
    extern void grotty_nasty_stats_hack (char *);
    grotty_nasty_stats_hack (ugh);
    mg_printf(conn,
	      "HTTP/1.1 200 OK\r\n"
	      "Content-Type: text/plain\r\n"
	      "Content-Length: %d\r\n"        // Always set Content-Length
	      "\r\n"
	      "%s",
	      strlen (ugh), ugh);
    return 1;
  }

  if(0 == strncmp(request_info->uri, "/vtxlimit", 9)) {
    const char * suburi = request_info->uri + 9;
    int64_t old_vtxlimit = vtxlimit;
    char limitbuf[64];

    if(suburi[0] == '/') {
      int64_t new_vtxlimit;
      /* setting the vertex limit. */
      suburi++;
      errno = 0;
      new_vtxlimit = strtol (suburi, NULL, 10);
      if (!errno && new_vtxlimit >= 0) vtxlimit = new_vtxlimit;
    }

    sprintf (limitbuf, "%" PRId64, old_vtxlimit);
    mg_printf(conn,
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/plain\r\n"
	"Content-Length: %d\r\n"        // Always set Content-Length
	"\r\n"
	"%s",
        strlen (limitbuf), limitbuf);
    return 1;
  }

  if(0 == strncmp(request_info->uri, "/getphysid", 10)) {
    const char * suburi = request_info->uri + 10;
    if(suburi[0] == '/') suburi++;

    string_t * mappings = get_physid_json(S, suburi);
    mg_printf(conn,
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/plain\r\n"
	"Content-Length: %d\r\n"        // Always set Content-Length
	"\r\n"
	"%s",
	mappings->len, mappings->str);
    string_free(mappings);
    return 1;
  }

  if(0 == strncmp(request_info->uri, "/getlabels", 10)) {
    const char * suburi = request_info->uri + 10;
    if(suburi[0] == '/') suburi++;

    if(0 == strlen(suburi)) {
      string_t * rslt = get_label_containers_json();
      mg_printf(conn,
	  "HTTP/1.1 200 OK\r\n"
	  "Content-Type: text/plain\r\n"
	  "Content-Length: %d\r\n"        // Always set Content-Length
	  "\r\n"
	  "%s",
	  rslt->len, rslt->str);
      string_free(rslt);
      return 1;
    } else {
      char tmp[1024];
      char * len = strchr(suburi, '/');
      int all = 0;
      if(len && len == (suburi + strlen(suburi - 1))) {
	strncpy(tmp, suburi, len - suburi);
	suburi = tmp;
      }
      if(!len || all) {
	string_t * rslt = get_labels_json(suburi);
	mg_printf(conn,
	    "HTTP/1.1 200 OK\r\n"
	    "Content-Type: text/plain\r\n"
	    "Content-Length: %d\r\n"        // Always set Content-Length
	    "\r\n"
	    "%s",
	    rslt->len, rslt->str);
	string_free(rslt);
	return 1;
      }
    }
  }

  if(0 == strncmp(request_info->uri, "/getlabelstats", 14)) {
    const char * suburi = request_info->uri + 14;
    if(suburi[0] == '/') suburi++;

    if(0 != strlen(suburi)) {
      char tmp[1024];
      char * len = strchr(suburi, '/');
      int all = 0;
      if(len && len == (suburi + strlen(suburi - 1))) {
	strncpy(tmp, suburi, len - suburi);
	suburi = tmp;
      }
      if(!len || all) {
	string_t * rslt = get_label_stats_json(suburi);
	mg_printf(conn,
	    "HTTP/1.1 200 OK\r\n"
	    "Content-Type: text/plain\r\n"
	    "Content-Length: %d\r\n"        // Always set Content-Length
	    "\r\n"
	    "%s",
	    rslt->len, rslt->str);
	string_free(rslt);
	return 1;
      }
    }
  }

  if(0 == strncmp(request_info->uri, "/getscores", 10)) {
    const char * suburi = request_info->uri + 10;
    if(suburi[0] == '/') suburi++;

    if(0 == strlen(suburi)) {
      string_t * rslt = get_score_containers_json(S, suburi);
      mg_printf(conn,
	  "HTTP/1.1 200 OK\r\n"
	  "Content-Type: text/plain\r\n"
	  "Content-Length: %d\r\n"        // Always set Content-Length
	  "\r\n"
	  "%s",
	  rslt->len, rslt->str);
      string_free(rslt);
      return 1;
    } else {
      char tmp[1024];
      char * len = strchr(suburi, '/');
      int all = 0;
      if(len && len == (suburi + strlen(suburi - 1))) {
	strncpy(tmp, suburi, len - suburi);
	suburi = tmp;
      }
      if(!len || all) {
	string_t * rslt = get_scores_json(suburi);
	mg_printf(conn,
	    "HTTP/1.1 200 OK\r\n"
	    "Content-Type: text/plain\r\n"
	    "Content-Length: %d\r\n"        // Always set Content-Length
	    "\r\n"
	    "%s",
	    rslt->len, rslt->str);
	string_free(rslt);
	return 1;
      }
    }
  }

  if(0 == strncmp(request_info->uri, "/getlabelnet", 12)) {
    const char * suburi = request_info->uri + 12;
    if(suburi[0] == '/') suburi++;

    char tmp[1024];
    char * len = strchr(suburi, '/');
    int all = 0;
    if(len) {
      strncpy(tmp, suburi, len - suburi);
      suburi = len+1;

      kv_element_t key = kve_from_str_static(tmp);
      kv_element_t * val;

      if(KV_SUCCESS == kv_store_get(&labels, &key, &val)) {
	int64_t vtx = 0;
	if(0 == strncmp(suburi, "byphysid/", 9)) {
	  suburi += 9;
	  vtx = stinger_mapping_lookup(S, suburi, strlen(suburi));
	} else {
	  vtx = atoi(suburi);
	}

	if(vtx < STINGER_MAX_LVERTICES && vtx >= 0) {
	    string_t * rslt = labeled_subgraph_to_json(S, vtx, kve_get_ptr(val), vtxlimit);
	    mg_printf(conn,
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/plain\r\n"
		"Content-Length: %d\r\n"        // Always set Content-Length
		"\r\n"
		"%s",
		rslt->len, rslt->str);
	    string_free(rslt);
	  return 1;
	}
      }
    }

    return 0;
  }

  if(0 == strncmp(request_info->uri, "/getstingerid", 13)) {
    const char * suburi = request_info->uri + 13;
    if(suburi[0] == '/') suburi++;

    string_t * mappings = get_stingerid_json(S, suburi);
    mg_printf(conn,
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/plain\r\n"
	"Content-Length: %d\r\n"        // Always set Content-Length
	"\r\n"
	"%s",
	mappings->len, mappings->str);
    string_free(mappings);
    return 1;
  }

  if(0 == strncmp(request_info->uri, "/getegonet", 10)) {
    const char * suburi = request_info->uri + 10;
    if(suburi[0] == '/') suburi++;

    int64_t vtx = 0;
    if(0 == strncmp(suburi, "byphysid/", 9)) {
      suburi += 9;
      vtx = stinger_mapping_lookup(S, suburi, strlen(suburi));
    } else {
      vtx = atoi(suburi);
    }
    
    if(vtx < STINGER_MAX_LVERTICES && vtx >= 0) {
      string_t * egonet = egonet_to_json(S, vtx);
      mg_printf(conn,
	  "HTTP/1.1 200 OK\r\n"
	  "Content-Type: text/plain\r\n"
	  "Content-Length: %d\r\n"        // Always set Content-Length
	  "\r\n"
	  "%s",
	  string_length(egonet), egonet->str);
      string_free(egonet);
      return 1;
    }
  }

  if(0 == strncmp(request_info->uri, "/getsubgraph", 12)) {
    const char * suburi = request_info->uri + 12;
    if(suburi[0] == '/') suburi++;

    const char * vtx = suburi;
    char * tmp_buf = NULL;
    if(0 == strncmp(suburi, "byphysid/", 9)) {
      suburi += 9;

      char * sub = calloc(strlen(suburi) + 2, sizeof(char));
      memcpy(sub, suburi, strlen(suburi));
      char * buf = NULL;
      uint64_t bufSize = 0;
      char ** fields = NULL;
      uint64_t * lengths = NULL;
      uint64_t fieldsSize = 0;
      uint64_t count = 0;

      splitLineCSVDynamic(',', sub, strlen(sub), &buf, &bufSize, &fields, &lengths, &fieldsSize, &count);

      int64_t * group_vertices = malloc(sizeof(int64_t) * count);
      int64_t found = 0;
      for(uint64_t i = 0; i < count; i++) {
	group_vertices[found] = stinger_mapping_lookup(S, fields[i], lengths[i]);
	if(group_vertices[found] >= STINGER_MAX_LVERTICES || group_vertices[found] < 0) {
	  group_vertices[found] = 0;
	} else {
	  found++;
	}
      }

      string_t * group = group_to_json(S, group_vertices, found);

      mg_printf(conn,
	  "HTTP/1.1 200 OK\r\n"
	  "Content-Type: text/plain\r\n"
	  "Content-Length: %d\r\n"        // Always set Content-Length
	  "\r\n"
	  "%s",
	  string_length(group), group->str);

      string_free(group);
      free(buf); free(fields); free(lengths); free(group_vertices); free(sub);

      return 1;

    } else {
      
      char * sub = calloc(strlen(suburi) + 2, sizeof(char));
      memcpy(sub, suburi, strlen(suburi));
      char * buf = NULL;
      uint64_t bufSize = 0;
      char ** fields = NULL;
      uint64_t * lengths = NULL;
      uint64_t fieldsSize = 0;
      uint64_t count = 0;

      splitLineCSVDynamic(',', sub, strlen(sub), &buf, &bufSize, &fields, &lengths, &fieldsSize, &count);

      int64_t * group_vertices = malloc(sizeof(int64_t) * count);
      int64_t found = 0;
      for(uint64_t i = 0; i < count; i++) {
	group_vertices[found] = atol(fields[i]);
	if(group_vertices[found] >= STINGER_MAX_LVERTICES || group_vertices[found] < 0) {
	  group_vertices[found] = 0;
	} else {
	  found++;
	}
      }

      string_t * group = group_to_json(S, group_vertices, found);

      mg_printf(conn,
	  "HTTP/1.1 200 OK\r\n"
	  "Content-Type: text/plain\r\n"
	  "Content-Length: %d\r\n"        // Always set Content-Length
	  "\r\n"
	  "%s",
	  string_length(group), group->str);

      string_free(group);
      free(buf); free(fields); free(lengths); free(group_vertices); free(sub);

      return 1;
    }
  }

  return 0;
}
