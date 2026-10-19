#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_sha1.h>

ngx_pool_t *
ngx_dyn_http_gp(ngx_cycle_t *cycle)
{
    return ngx_dyn_build_pool;
}


static ngx_int_t
ngx_dyn_listen_match(ngx_listening_t *a, ngx_listening_t *b)
{
    if (a->type != b->type) {
        return NGX_DECLINED;
    }

    if (a->handler != b->handler) {
        return NGX_DECLINED;
    }

    return ngx_cmp_sockaddr(a->sockaddr, a->socklen,
                            b->sockaddr, b->socklen, 1);
}


ngx_int_t
ngx_dyn_http_swap(ngx_cycle_t *live, ngx_cycle_t *cand, ngx_dyn_gen_t *new_gen)
{
    ngx_uint_t        i, n, matched;
    ngx_listening_t  *ls, *nls, *cls;
    ngx_list_part_t  *lpart, *cpart;
    ngx_shm_zone_t   *lshm, *cshm;

    ls = live->listening.elts;
    nls = cand->listening.elts;

    if (live->listening.nelts != cand->listening.nelts) {
        return NGX_DECLINED;
    }

    for (i = 0; i < cand->listening.nelts; i++) {
        matched = 0;

        for (n = 0; n < live->listening.nelts; n++) {
            if (ngx_dyn_listen_match(&nls[i], &ls[n]) != NGX_OK) {
                continue;
            }

            matched = 1;
            break;
        }

        if (!matched) {
            return NGX_DECLINED;
        }
    }

    for (i = 0; i < cand->listening.nelts; i++) {

        if (nls[i].handler != ngx_http_init_connection) {
            continue;
        }

        for (n = 0; n < live->listening.nelts; n++) {
            if (ngx_dyn_listen_match(&nls[i], &ls[n]) != NGX_OK) {
                continue;
            }

            if (ls[n].handler != ngx_http_init_connection) {
                continue;
            }

            cls = &ls[n];
            cls->sockaddr = nls[i].sockaddr;
            cls->socklen = nls[i].socklen;
            cls->addr_text = nls[i].addr_text;
            cls->servers = nls[i].servers;
            cls->logp = nls[i].logp;
            cls->pool_size = nls[i].pool_size;

            if (cls->logp) {
                cls->log = *cls->logp;
            }

            break;
        }
    }

    live->conf_ctx[ngx_http_module.index] = new_gen->ctx;

    lpart = &live->shared_memory.part;
    lshm = lpart->elts;

    for (i = 0; /* void */ ; i++) {

        if (i >= lpart->nelts) {
            if (lpart->next == NULL) {
                break;
            }

            lpart = lpart->next;
            lshm = lpart->elts;
            i = 0;
        }

        cpart = &cand->shared_memory.part;
        cshm = cpart->elts;

        for (n = 0; /* void */ ; n++) {

            if (n >= cpart->nelts) {
                if (cpart->next == NULL) {
                    break;
                }

                cpart = cpart->next;
                cshm = cpart->elts;
                n = 0;
            }

            if (lshm[i].shm.name.len != cshm[n].shm.name.len) {
                continue;
            }

            if (ngx_strncmp(lshm[i].shm.name.data, cshm[n].shm.name.data,
                            lshm[i].shm.name.len)
                != 0)
            {
                continue;
            }

            lshm[i].data = cshm[n].data;
            break;
        }
    }

    ngx_dyn_generation_swap(live, new_gen);

    return NGX_OK;
}


void
ngx_dyn_worker_apply(ngx_uint_t gen_id)
{
    void           *ctx;
    ngx_pool_t     *gp;
    ngx_cycle_t    *cand;
    ngx_str_t       err;
    ngx_dyn_gen_t  *g;
    ngx_sha1_t      sha;

    if (ngx_process != NGX_PROCESS_WORKER) {
        return;
    }

    gp = ngx_dyn_gp_create(ngx_cycle->log, NGX_CYCLE_POOL_SIZE);
    if (gp == NULL) {
        return;
    }

    cand = ngx_dyn_cycle_build(ngx_cycle, gp, gen_id, &err);

    if (cand == NULL) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                      "dyn_conf: worker build gen=%ui failed: %V", gen_id, &err);
        ngx_dyn_gp_destroy(gp, gen_id);
        return;
    }

    ngx_log_error(NGX_LOG_NOTICE, ngx_cycle->log, 0,
                  "dyn_conf: worker built gen=%ui", gen_id);

    if (ngx_dyn_diff_check(ngx_cycle, cand, &err) != NGX_OK) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                      "dyn_conf: worker rejected: %V", &err);
        ngx_dyn_gp_destroy(gp, gen_id);
        return;
    }

    if (ngx_dyn_open_log_files(cand, gp, &err) != NGX_OK) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                      "dyn_conf: worker rejected: %V", &err);
        ngx_dyn_gp_destroy(gp, gen_id);
        return;
    }

    ctx = cand->conf_ctx[ngx_http_module.index];

    g = ngx_dyn_gen_create(gp, ctx, gen_id, NULL);
    if (g == NULL) {
        ngx_dyn_gp_destroy(gp, gen_id);
        return;
    }

    ngx_sha1_init(&sha);
    ngx_sha1_update(&sha, (u_char *) "conf", 4);
    ngx_sha1_final(g->digest, &sha);

    if (ngx_dyn_http_swap(ngx_cycle, cand, g) != NGX_OK) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                      "dyn_conf: swap rejected for gen=%ui", gen_id);
        ngx_dyn_gp_destroy(gp, gen_id);
        return;
    }

    ngx_log_error(NGX_LOG_NOTICE, ngx_cycle->log, 0,
                  "dyn_conf: worker swapped to gen=%ui", gen_id);
}


static void
ngx_dyn_resolver_hash(u_char acc[20], ngx_http_core_loc_conf_t *clcf,
    ngx_resolver_t *inherit);


static void
ngx_dyn_digest_add(u_char acc[20], u_char d[20])
{
    ngx_uint_t  i, car;

    car = 0;

    for (i = 20; i > 0; i--) {
        car += acc[i - 1] + d[i - 1];
        acc[i - 1] = (u_char) car;
        car >>= 8;
    }
}


static void
ngx_dyn_resolver_hash_array(u_char acc[20],
    ngx_http_core_loc_conf_t **clcfp, ngx_resolver_t *inherit)
{
    if (clcfp == NULL) {
        return;
    }

    while (*clcfp) {
        ngx_dyn_resolver_hash(acc, *clcfp, inherit);
        clcfp++;
    }
}


static void
ngx_dyn_resolver_hash_node(u_char acc[20],
    ngx_http_location_tree_node_t *node, ngx_resolver_t *inherit)
{
    if (node == NULL) {
        return;
    }

    ngx_dyn_resolver_hash_node(acc, node->left, inherit);
    ngx_dyn_resolver_hash_node(acc, node->right, inherit);

    if (node->exact) {
        ngx_dyn_resolver_hash(acc, node->exact, inherit);
    }

    if (node->inclusive) {
        ngx_dyn_resolver_hash(acc, node->inclusive, inherit);
    }

    ngx_dyn_resolver_hash_node(acc, node->tree, inherit);
}


static void
ngx_dyn_resolver_hash(u_char acc[20], ngx_http_core_loc_conf_t *clcf,
    ngx_resolver_t *inherit)
{
    ngx_uint_t                  i;
    ngx_sha1_t                  sha;
    u_char                      d[20];
    ngx_resolver_t             *r;
    ngx_resolver_connection_t  *c;

    r = clcf->resolver;

    if (r != NULL && r != inherit && r->connections.nelts > 0) {
        ngx_sha1_init(&sha);
        ngx_sha1_update(&sha, (u_char *) "R", 1);

        c = r->connections.elts;

        for (i = 0; i < r->connections.nelts; i++) {
            ngx_sha1_update(&sha, c[i].server.data, c[i].server.len);
            ngx_sha1_update(&sha, (u_char *) c[i].sockaddr, c[i].socklen);
        }

        ngx_sha1_final(d, &sha);

        ngx_dyn_digest_add(acc, d);
    }

    ngx_dyn_resolver_hash_array(acc, clcf->predicate_locations, r);

#if (NGX_PCRE)
    ngx_dyn_resolver_hash_array(acc, clcf->regex_locations, r);
#endif

    ngx_dyn_resolver_hash_node(acc, clcf->static_locations, r);
}


static void
ngx_dyn_http_resolver_digest(ngx_cycle_t *cycle, u_char digest[20])
{
    ngx_uint_t                     s;
    u_char                         acc[20];
    ngx_http_conf_ctx_t           *ctx;
    ngx_http_core_main_conf_t     *cmcf;
    ngx_http_core_srv_conf_t     **cscfp;
    ngx_http_core_loc_conf_t      *clcf, *sclcf;
    ngx_resolver_t                *inherit;

    ngx_memzero(acc, 20);

    ctx = (ngx_http_conf_ctx_t *) cycle->conf_ctx[ngx_http_module.index];

    if (ctx != NULL) {
        clcf = ctx->loc_conf[ngx_http_core_module.ctx_index];

        if (clcf != NULL) {
            ngx_dyn_resolver_hash(acc, clcf, NULL);
            inherit = clcf->resolver;

        } else {
            inherit = NULL;
        }

        cmcf = ctx->main_conf[ngx_http_core_module.ctx_index];
        cscfp = cmcf->servers.elts;

        for (s = 0; s < cmcf->servers.nelts; s++) {
            sclcf = cscfp[s]->ctx->loc_conf[ngx_http_core_module.ctx_index];

            ngx_dyn_resolver_hash(acc, sclcf, inherit);
            ngx_dyn_resolver_hash_array(acc, cscfp[s]->named_locations,
                                        sclcf->resolver);
        }
    }

    ngx_memcpy(digest, acc, 20);
}


ngx_int_t
ngx_dyn_http_diff_check(ngx_cycle_t *live, ngx_cycle_t *cand, ngx_str_t *err)
{
    u_char  ld[20], cd[20];

    ngx_dyn_http_resolver_digest(live, ld);
    ngx_dyn_http_resolver_digest(cand, cd);

    if (ngx_memcmp(ld, cd, 20) != 0) {
        ngx_str_set(err, "resolver changed");
        return NGX_ERROR;
    }

    return NGX_OK;
}
