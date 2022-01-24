//
//  ARC cache replacement algorithm
//  libCacheSim
//
//  Created by Juncheng on 09/28/20.
//  Copyright © 2020 Juncheng. All rights reserved.
//


#include "../dataStructure/hashtable/hashtable.h"
#include "../include/libCacheSim/evictionAlgo/LRU.h"
#include "../include/libCacheSim/evictionAlgo/ARC.h"

#ifdef __cplusplus
extern "C" {
#endif

cache_t *ARC_init(common_cache_params_t ccache_params_, void *init_params_) {
  cache_t *cache = cache_struct_init("ARC", ccache_params_);
  cache->cache_init = ARC_init;
  cache->cache_free = ARC_free;
  cache->get = ARC_get;
  cache->check = ARC_check;
  cache->insert = ARC_insert;
  cache->evict = ARC_evict;
  cache->remove = ARC_remove;

  cache->init_params = init_params_;
  ARC_init_params_t *init_params = (ARC_init_params_t *) init_params_;

  cache->eviction_params = my_malloc_n(ARC_params_t, 1);
  ARC_params_t *params = (ARC_params_t *) (cache->eviction_params);
  if (init_params_ != NULL)
    params->ghost_list_factor = init_params->ghost_list_factor;
  else
    params->ghost_list_factor = 1;

  /* the two LRU are initialized with cache_size, but they will not be full */
  params->LRU1 = LRU_init(ccache_params_, NULL);
  params->LRU2 = LRU_init(ccache_params_, NULL);

  ccache_params_.cache_size = (uint64_t) (
      (double) ccache_params_.cache_size / 2 * params->ghost_list_factor);
  params->LRU1g = LRU_init(ccache_params_, NULL);
  params->LRU2g = LRU_init(ccache_params_, NULL);

  return cache;
}

void ARC_free(cache_t *cache) {
  ARC_params_t *ARC_params = (ARC_params_t *) (cache->eviction_params);
  ARC_params->LRU1->cache_free(ARC_params->LRU1);
  ARC_params->LRU1g->cache_free(ARC_params->LRU1g);
  ARC_params->LRU2->cache_free(ARC_params->LRU2);
  ARC_params->LRU2g->cache_free(ARC_params->LRU2g);
  my_free(sizeof(ARC_params_t), ARC_params);
  cache_struct_free(cache);
}

void _verify(cache_t *cache, request_t *req) {
  ARC_params_t *params = (ARC_params_t *) (cache->eviction_params);
  cache_ck_res_e hit1 = params->LRU1->check(params->LRU1, req, false);
  cache_ck_res_e hit2 = params->LRU2->check(params->LRU2, req, false);

  if (hit1 == cache_ck_hit)
    DEBUG_ASSERT(hit2 != cache_ck_hit);

  if (hit2 == cache_ck_hit)
    DEBUG_ASSERT(hit1 != cache_ck_hit);

//  printf("%llu obj %llu %s %s\n", req->n_req, req->obj_id, CACHE_CK_STATUS_STR[hit1], CACHE_CK_STATUS_STR[hit2]);
}

cache_ck_res_e ARC_check(cache_t *cache, request_t *req, bool update_cache) {
  static __thread request_t *req_local = NULL;
  if (req_local == NULL)
    req_local = new_request();

  ARC_params_t *params = (ARC_params_t *) (cache->eviction_params);

  cache_ck_res_e hit1, hit2, hit1g = cache_ck_invalid, hit2g = cache_ck_invalid;
  /* LRU1 does not update because if it is a hit, we will move it to LRU2 */
  hit1 = params->LRU1->check(params->LRU1, req, false);
  hit2 = params->LRU2->check(params->LRU2, req, update_cache);
  cache_ck_res_e hit = hit1;
  if (hit1 == cache_ck_hit || hit2 == cache_ck_hit) {
    hit = cache_ck_hit;
  }

  if (!update_cache)
    return hit;

  cache->n_req += 1;
  if (hit != cache_ck_hit) {
    hit1g = params->LRU1g->check(params->LRU1g, req, false);
    hit2g = params->LRU2g->check(params->LRU2g, req, false);
    if (hit1g == cache_ck_hit) {
      /* hit on LRU1 ghost list */
      params->evict_lru = 2;
      DEBUG_ASSERT(hit2g != cache_ck_hit);
      params->LRU1g->remove(params->LRU1g, req->obj_id);
    } else {
      if (hit2g == cache_ck_hit) {
        params->evict_lru = 1;
        params->LRU2g->remove(params->LRU2g, req->obj_id);
      }
    }
  }

  if (hit1 == cache_ck_hit) {
    DEBUG_ASSERT(hit2 != cache_ck_hit);
    params->LRU1->remove(params->LRU1, req->obj_id);
    params->LRU2->insert(params->LRU2, req);
  } else if (hit2 == cache_ck_hit) {
    /* moving to the tail of LRU2 has already been done */
  }
  cache->occupied_size =
      params->LRU1->occupied_size + params->LRU2->occupied_size;
  DEBUG_ASSERT(cache->n_obj == params->LRU1->n_obj + params->LRU2->n_obj);


  return hit;
}

cache_ck_res_e ARC_get(cache_t *cache, request_t *req) {
  return cache_get_base(cache, req);
}

void ARC_insert(cache_t *cache, request_t *req) {
  /* first time add, then it should be add to LRU1 */
  ARC_params_t *params = (ARC_params_t *) (cache->eviction_params);

  params->LRU1->insert(params->LRU1, req);

  cache->occupied_size += req->obj_size + cache->per_obj_overhead;
  cache->n_obj += 1;
  DEBUG_ASSERT(cache->occupied_size ==
      params->LRU1->occupied_size + params->LRU2->occupied_size);

}

void ARC_evict(cache_t *cache, request_t *req, cache_obj_t *evicted_obj) {
  cache_obj_t obj;
  static __thread request_t *req_local = NULL;
  if (req_local == NULL) {
    req_local = new_request();
  }

  ARC_params_t *params = (ARC_params_t *) (cache->eviction_params);
  cache_t *cache_evict, *cache_evict_ghost;

  if ((params->evict_lru == 1 || params->LRU2->n_obj == 0) &&
        params->LRU1->n_obj != 0) {
    cache_evict = params->LRU1;
    cache_evict_ghost = params->LRU1g;
  } else {
    cache_evict = params->LRU2;
    cache_evict_ghost = params->LRU2g;
  }

  cache_evict->evict(cache_evict, req, &obj);
  if (evicted_obj != NULL) {
    memcpy(evicted_obj, &obj, sizeof(cache_obj_t));
  }
  copy_cache_obj_to_request(req_local, &obj);
  cache_ck_res_e ck = cache_evict_ghost->get(cache_evict_ghost, req_local);
  DEBUG_ASSERT(ck == cache_ck_miss);
  cache->occupied_size -= (obj.obj_size + cache->per_obj_overhead);
  cache->n_obj -= 1;
}

void ARC_remove(cache_t *cache, obj_id_t obj_id) {
  ARC_params_t *params = (ARC_params_t *) (cache->eviction_params);
  cache_obj_t *obj = cache_get_obj_by_id(params->LRU1, obj_id);
  if (obj != NULL) {
    params->LRU1->remove(params->LRU1, obj_id);
  } else {
    obj = cache_get_obj_by_id(params->LRU2, obj_id);
    if (obj != NULL) {
      params->LRU2->remove(params->LRU2, obj_id);
    } else {
      ERROR("remove object %"PRIu64 "that is not cached\n", obj_id);
      return;
    }
  }

  cache->occupied_size -= (obj->obj_size + cache->per_obj_overhead);
  cache->n_obj -= 1;
}

#ifdef __cplusplus
}
#endif