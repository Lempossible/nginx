#include <ngx_config.h>
#include <ngx_core.h>

ngx_uint_t   ngx_dyn_build_mode = 0;
ngx_pool_t  *ngx_dyn_build_pool = NULL;

void
ngx_dyn_pool_stats(ngx_pool_t *pool, ngx_dyn_pool_stats_t *st)
{
    ngx_pool_t        *p;
    ngx_pool_large_t  *l;

    ngx_memzero(st, sizeof(ngx_dyn_pool_stats_t));

    if (pool == NULL) {
        return;
    }

    for (p = pool; p; p = p->d.next) {
        st->nblocks++;
        st->bytes += (size_t) (p->d.end - (u_char *) p);
    }

    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            st->nlarge++;
            st->large_bytes += l->size;
        }
    }
}

ngx_pool_t *
ngx_dyn_gp_create(ngx_log_t *log, size_t size)
{
    return ngx_create_pool(size, log);
}

void
ngx_dyn_gp_destroy(ngx_pool_t *gp, ngx_uint_t id)
{
    ngx_dyn_pool_stats_t  st;

    ngx_dyn_pool_stats(gp, &st);

    ngx_log_error(NGX_LOG_NOTICE, gp->log, 0,
                  "dyn_conf: gen=%ui freed blocks=%ui bytes=%uz large=%ui "
                  "large_bytes=%uz",
                  id, st.nblocks, st.bytes, st.nlarge, st.large_bytes);

    ngx_destroy_pool(gp);
}

void *
ngx_dyn_find_open_file(ngx_cycle_t *cycle, ngx_str_t *name)
{
    ngx_uint_t        i;
    ngx_list_part_t  *part;
    ngx_open_file_t  *file;

    part = &cycle->open_files.part;
    file = part->elts;

    for (i = 0; /* void */ ; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            file = part->elts;
            i = 0;
        }

        if (file[i].name.len != name->len) {
            continue;
        }

        if (ngx_strncmp(file[i].name.data, name->data, name->len) == 0) {
            return &file[i];
        }
    }

    return NULL;
}


static void
ngx_dyn_close_log_fd(void *data)
{
    ngx_fd_t  fd;

    fd = *(ngx_fd_t *) data;

    if (fd != NGX_INVALID_FILE) {
        (void) ngx_close_file(fd);
    }
}


ngx_int_t
ngx_dyn_open_log_files(ngx_cycle_t *cycle, ngx_pool_t *gp, ngx_str_t *err)
{
    ngx_uint_t           i;
    ngx_fd_t             fd;
    ngx_fd_t            *cfd;
    ngx_list_part_t     *part;
    ngx_open_file_t     *file;
    ngx_pool_cleanup_t  *cln;

    part = &cycle->open_files.part;
    file = part->elts;

    for (i = 0; /* void */ ; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            file = part->elts;
            i = 0;
        }

        if (file[i].name.len == 0 || file[i].fd != NGX_INVALID_FILE) {
            continue;
        }

        fd = ngx_open_file(file[i].name.data, NGX_FILE_APPEND,
                           NGX_FILE_CREATE_OR_OPEN, NGX_FILE_DEFAULT_ACCESS);

        if (fd == NGX_INVALID_FILE) {
            err->data = (u_char *) "cannot open log file";
            err->len = ngx_strlen(err->data);
            return NGX_ERROR;
        }

#if !(NGX_WIN32)
        if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
            (void) ngx_close_file(fd);
            err->data = (u_char *) "cannot open log file";
            err->len = ngx_strlen(err->data);
            return NGX_ERROR;
        }
#endif

        cln = ngx_pool_cleanup_add(gp, sizeof(ngx_fd_t));

        if (cln == NULL) {
            (void) ngx_close_file(fd);
            err->data = (u_char *) "cannot open log file";
            err->len = ngx_strlen(err->data);
            return NGX_ERROR;
        }

        cfd = cln->data;
        *cfd = fd;
        cln->handler = ngx_dyn_close_log_fd;

        file[i].fd = fd;
    }

    return NGX_OK;
}


ngx_cycle_t *
ngx_dyn_cycle_build(ngx_cycle_t *live, ngx_pool_t *gp, ngx_uint_t id,
    ngx_str_t *err)
{
    ngx_uint_t    saved_mode;
    ngx_pool_t   *saved_pool;
    ngx_cycle_t  *cand;

    saved_mode = ngx_dyn_build_mode;
    saved_pool = ngx_dyn_build_pool;

    ngx_dyn_build_mode = 1;
    ngx_dyn_build_pool = gp;

    cand = ngx_init_cycle(live);

    ngx_dyn_build_mode = saved_mode;
    ngx_dyn_build_pool = saved_pool;

    if (cand == NULL) {
        err->data = (u_char *) "config parse/merge failed";
        err->len = ngx_strlen(err->data);
        return NULL;
    }

    return cand;
}

void
ngx_dyn_generation_swap(ngx_cycle_t *cycle, ngx_dyn_gen_t *g)
{
    ngx_dyn_gen_t  *old;

    old = cycle->dyn_gen;
    cycle->dyn_gen = g;

    ngx_log_error(NGX_LOG_INFO, cycle->log, 0,
                  "dyn_conf: swap gen=%ui -> gen=%ui, old refs=%i",
                  old ? old->id : 0, g ? g->id : 0, old ? old->refs : 0);

    ngx_dyn_gen_unref(old);
}

ngx_dyn_gen_t *
ngx_dyn_gen_create(ngx_pool_t *gp, void *ctx, ngx_uint_t id,
    ngx_dyn_gen_t *prev)
{
    ngx_dyn_gen_t  *g;

    g = ngx_pcalloc(gp, sizeof(ngx_dyn_gen_t));
    if (g == NULL) {
        return NULL;
    }

    g->pool = gp;
    g->ctx = ctx;
    g->id = id;
    g->refs = 1;
    g->prev = prev;

    return g;
}

ngx_dyn_gen_t *
ngx_dyn_gen_ref(ngx_dyn_gen_t *g)
{
    if (g != NULL) {
        (void) ngx_atomic_fetch_add(&g->refs, 1);
    }

    return g;
}

void
ngx_dyn_gen_unref(ngx_dyn_gen_t *g)
{
    ngx_pool_t  *pool;
    ngx_uint_t   id;

    if (g == NULL) {
        return;
    }

    if (ngx_atomic_fetch_add(&g->refs, -1) > 1) {
        return;
    }

    pool = g->pool;
    id = g->id;

    ngx_dyn_gp_destroy(pool, id);
}

void
ngx_dyn_gen_cleanup(void *data)
{
    ngx_dyn_gen_unref(*(ngx_dyn_gen_t **) data);
}

void
ngx_dyn_cycle_cleanup(void *data)
{
    ngx_dyn_gen_t  *g;

    g = *(ngx_dyn_gen_t **) data;

    if (g == NULL) {
        return;
    }

    ngx_log_error(NGX_LOG_NOTICE, g->pool->log, 0,
                  "dyn_conf: cycle cleanup gen=%ui refs=%i", g->id, g->refs);

    ngx_dyn_gen_unref(g);
}
