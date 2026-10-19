#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_channel.h>

#if (NGX_THREADS)
#include <ngx_thread_pool.h>
#endif


ngx_uint_t  ngx_dyn_master_generation;
ngx_str_t   ngx_dyn_master_digest = ngx_string("");
ngx_uint_t  ngx_dyn_master_pending;


static ngx_uint_t
ngx_dyn_shm_count(ngx_list_t *l)
{
    ngx_uint_t        n;
    ngx_list_part_t  *part;

    part = &l->part;
    n = part->nelts;

    while (part->next) {
        part = part->next;
        n += part->nelts;
    }

    return n;
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
ngx_dyn_diff_check(ngx_cycle_t *live, ngx_cycle_t *cand, ngx_str_t *err)
{
    ngx_uint_t        i, n, found;
    ngx_int_t         lwp, cwp;
    ngx_core_conf_t  *lccf, *cccf;
    ngx_listening_t  *ls, *nls;
    ngx_shm_zone_t   *lz, *cz;
    ngx_list_part_t  *lpart, *cpart;

    lccf = (ngx_core_conf_t *) ngx_get_conf(live->conf_ctx, ngx_core_module);
    cccf = (ngx_core_conf_t *) ngx_get_conf(cand->conf_ctx, ngx_core_module);

    lwp = lccf->worker_processes;
    cwp = cccf->worker_processes;

    if (lwp == NGX_CONF_UNSET) {
        lwp = 1;
    }

    if (cwp == NGX_CONF_UNSET) {
        cwp = 1;
    }

    if (lwp != cwp) {
        ngx_str_set(err, "worker_processes changed");
        return NGX_ERROR;
    }

    if (live->modules_n != cand->modules_n) {
        ngx_str_set(err, "load_module changed");
        return NGX_ERROR;
    }

    for (i = 0; i < live->modules_n; i++) {
        if (ngx_strcmp(live->modules[i]->name, cand->modules[i]->name) != 0) {
            ngx_str_set(err, "load_module changed");
            return NGX_ERROR;
        }
    }

    if (live->listening.nelts != cand->listening.nelts) {
        ngx_str_set(err, "listen set changed");
        return NGX_ERROR;
    }

    ls = live->listening.elts;
    nls = cand->listening.elts;

    for (i = 0; i < cand->listening.nelts; i++) {
        found = 0;

        for (n = 0; n < live->listening.nelts; n++) {
            if (ngx_dyn_listen_match(&nls[i], &ls[n]) != NGX_OK) {
                continue;
            }

            if (nls[i].backlog != ls[n].backlog
                || nls[i].rcvbuf != ls[n].rcvbuf
                || nls[i].sndbuf != ls[n].sndbuf
                || nls[i].keepalive != ls[n].keepalive)
            {
                ngx_str_set(err, "listen options changed");
                return NGX_ERROR;
            }

#if (NGX_HAVE_REUSEPORT)
            if (nls[i].reuseport != ls[n].reuseport) {
                ngx_str_set(err, "listen options changed");
                return NGX_ERROR;
            }
#endif

#if (NGX_HAVE_INET6)
            if (nls[i].ipv6only != ls[n].ipv6only) {
                ngx_str_set(err, "listen options changed");
                return NGX_ERROR;
            }
#endif

            found = 1;
            break;
        }

        if (!found) {
            ngx_str_set(err, "listen set changed");
            return NGX_ERROR;
        }
    }

    if (ngx_dyn_shm_count(&live->shared_memory)
        != ngx_dyn_shm_count(&cand->shared_memory))
    {
        ngx_str_set(err, "shared memory zone set changed");
        return NGX_ERROR;
    }

    lpart = &live->shared_memory.part;
    lz = lpart->elts;

    for (i = 0; /* void */ ; i++) {

        if (i >= lpart->nelts) {
            if (lpart->next == NULL) {
                break;
            }

            lpart = lpart->next;
            lz = lpart->elts;
            i = 0;
        }

        found = 0;

        cpart = &cand->shared_memory.part;
        cz = cpart->elts;

        for (n = 0; /* void */ ; n++) {

            if (n >= cpart->nelts) {
                if (cpart->next == NULL) {
                    break;
                }

                cpart = cpart->next;
                cz = cpart->elts;
                n = 0;
            }

            if (lz[i].shm.size != cz[n].shm.size) {
                continue;
            }

            if (lz[i].shm.name.len != cz[n].shm.name.len) {
                continue;
            }

            if (ngx_strncmp(lz[i].shm.name.data, cz[n].shm.name.data,
                            lz[i].shm.name.len)
                == 0)
            {
                found = 1;
                break;
            }
        }

        if (!found) {
            ngx_str_set(err, "shared memory zone set changed");
            return NGX_ERROR;
        }
    }

#if (NGX_THREADS)
    if (!ngx_thread_pool_equal(live, cand)) {
        ngx_str_set(err, "thread_pool changed");
        return NGX_ERROR;
    }
#endif

#if (NGX_HTTP)
    if (ngx_dyn_http_diff_check(live, cand, err) != NGX_OK) {
        return NGX_ERROR;
    }
#endif

    return NGX_OK;
}


ngx_int_t
ngx_dyn_validate(ngx_cycle_t *cycle, ngx_str_t *err)
{
    ngx_pool_t   *gp;
    ngx_cycle_t  *cand;
    ngx_uint_t    id;

    id = ngx_dyn_master_generation + 1;

    gp = ngx_dyn_gp_create(cycle->log, NGX_CYCLE_POOL_SIZE);
    if (gp == NULL) {
        ngx_str_set(err, "cannot create generation pool");
        return NGX_ERROR;
    }

    cand = ngx_dyn_cycle_build(cycle, gp, id, err);

    if (cand == NULL) {
        ngx_dyn_gp_destroy(gp, id);
        return NGX_ERROR;
    }

    if (ngx_dyn_diff_check(cycle, cand, err) != NGX_OK) {
        ngx_dyn_gp_destroy(gp, id);
        return NGX_ERROR;
    }

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                  "dyn_conf: validate ok, listening=%ui modules=%ui",
                  cand->listening.nelts, ngx_max_module);

    ngx_dyn_gp_destroy(gp, id);

    return NGX_OK;
}


ngx_int_t
ngx_dyn_master_poll(ngx_cycle_t *cycle, ngx_uint_t force)
{
    ngx_str_t  err;

    (void) force;

    if (ngx_dyn_validate(cycle, &err) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                      "dyn_conf: rejected: %V", &err);
        return NGX_ERROR;
    }

    ngx_dyn_master_generation++;
    ngx_dyn_master_pending = 1;

    ngx_dyn_broadcast(cycle, ngx_dyn_master_generation);

    return NGX_OK;
}


void
ngx_dyn_broadcast(ngx_cycle_t *cycle, ngx_uint_t gen_id)
{
    ngx_int_t       n;
    ngx_channel_t   ch;

    ngx_memzero(&ch, sizeof(ngx_channel_t));

    ch.command = NGX_CMD_DYN_CONF;
    ch.pid = ngx_pid;
    ch.slot = ngx_process_slot;
    ch.fd = NGX_INVALID_FILE;
    ch.arg = gen_id;

    for (n = 0; n < ngx_last_process; n++) {
        if (ngx_processes[n].pid == -1) {
            continue;
        }

        if (ngx_processes[n].channel[0] == -1) {
            continue;
        }

        if (ngx_write_channel(ngx_processes[n].channel[0], &ch,
                              sizeof(ngx_channel_t), cycle->log)
            != NGX_OK)
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                          "dyn_conf: write channel to %P failed",
                          ngx_processes[n].pid);
        }
    }
}
