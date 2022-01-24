#pragma once

#include "../../include/libCacheSim/enum.h"
#include "../../include/libCacheSim/evictionAlgo.h"
#include "../../include/libCacheSim/reader.h"
#include <inttypes.h>


typedef struct {
  char *trace_path;
  char *alg;
  trace_type_e trace_type;
  obj_id_type_e obj_id_type;
  int per_obj_metadata;

  int64_t cache_size;
  struct {
    uint64_t cache_sizes[128];
    int n_cache_size;
  };

  int n_thread;
  bool debug;

#if defined(ENABLE_L2CACHE) && ENABLE_L2CACHE == 1
  struct {
    LSC_type_e lsc_type;
    bucket_type_e bucket_type;
    int seg_size;
    int n_merge;
    int rank_intvl;
    int snapshot_intvl;
    int size_bucket_base;
    int age_shift;
    int min_start_train_seg;
    int max_start_train_seg;
    int n_train_seg_growth;
    int re_train_intvl;
    int sample_every_n_seg_for_training;
  };
#endif
  reader_t *reader;
  cache_t *cache;
} sim_arg_t;


sim_arg_t parse_cmd(int argc, char *argv[]);
