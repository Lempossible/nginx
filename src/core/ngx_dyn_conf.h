#ifndef _NGX_DYN_CONF_H_INCLUDED_
#define _NGX_DYN_CONF_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>

typedef struct {
    ngx_uint_t   nblocks;
    size_t       bytes;
    ngx_uint_t   nlarge;
    size_t       large_bytes;
} ngx_dyn_pool_stats_t;

void ngx_dyn_pool_stats(ngx_pool_t *pool, ngx_dyn_pool_stats_t *st);

ngx_pool_t *ngx_dyn_gp_create(ngx_log_t *log, size_t size);
void ngx_dyn_gp_destroy(ngx_pool_t *gp, ngx_uint_t id);

void *ngx_dyn_find_open_file(ngx_cycle_t *cycle, ngx_str_t *name);
ngx_int_t ngx_dyn_open_log_files(ngx_cycle_t *cycle, ngx_pool_t *gp,
    ngx_str_t *err);

ngx_cycle_t *ngx_dyn_cycle_build(ngx_cycle_t *live, ngx_pool_t *gp,
    ngx_uint_t id, ngx_str_t *err);

ngx_int_t ngx_dyn_validate(ngx_cycle_t *live, ngx_str_t *err);
ngx_int_t ngx_dyn_master_poll(ngx_cycle_t *cycle, ngx_uint_t force);

ngx_int_t ngx_dyn_diff_check(ngx_cycle_t *live, ngx_cycle_t *cand,
    ngx_str_t *err);
ngx_int_t ngx_dyn_http_diff_check(ngx_cycle_t *live, ngx_cycle_t *cand,
    ngx_str_t *err);

void ngx_dyn_broadcast(ngx_cycle_t *cycle, ngx_uint_t gen_id);
void ngx_dyn_worker_apply(ngx_uint_t gen_id);

extern ngx_uint_t   ngx_dyn_master_generation;
extern ngx_str_t    ngx_dyn_master_digest;
extern ngx_uint_t   ngx_dyn_master_pending;

typedef struct ngx_dyn_gen_s  ngx_dyn_gen_t;

struct ngx_dyn_gen_s {
    ngx_pool_t     *pool;
    void           *ctx;
    ngx_uint_t      id;
    ngx_atomic_t    refs;
    u_char          digest[20];
    ngx_dyn_gen_t  *prev;
};

ngx_dyn_gen_t *ngx_dyn_gen_create(ngx_pool_t *gp, void *ctx, ngx_uint_t id,
    ngx_dyn_gen_t *prev);
ngx_dyn_gen_t *ngx_dyn_gen_ref(ngx_dyn_gen_t *g);
void ngx_dyn_gen_unref(ngx_dyn_gen_t *g);
void ngx_dyn_gen_cleanup(void *data);

void ngx_dyn_generation_swap(ngx_cycle_t *cycle, ngx_dyn_gen_t *g);
ngx_int_t ngx_dyn_http_swap(ngx_cycle_t *live, ngx_cycle_t *cand,
    ngx_dyn_gen_t *new_gen);

void ngx_dyn_cycle_cleanup(void *data);

extern ngx_uint_t   ngx_dyn_build_mode;
extern ngx_pool_t  *ngx_dyn_build_pool;

#endif /* _NGX_DYN_CONF_H_INCLUDED_ */
