
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

/* ngx_spinlock is defined without a matching unlock primitive */
#define ngx_spinlock_unlock(lock)       (void) ngx_atomic_cmp_set(lock, ngx_pid, 0)

static ngx_int_t ngx_http_check_get_shm_name(ngx_str_t *shm_name, 
        ngx_pool_t *pool);
static ngx_int_t ngx_http_upstream_check_init_shm_zone(ngx_shm_zone_t *shm_zone,
        void *data);

static void ngx_http_check_peek_handler(ngx_event_t *event);

static void ngx_http_check_send_handler(ngx_event_t *event);
static void ngx_http_check_recv_handler(ngx_event_t *event);

static ngx_int_t ngx_http_check_http_init(ngx_http_check_peer_t *peer);
static ngx_int_t ngx_http_check_http_parse(ngx_http_check_peer_t *peer);
static void ngx_http_check_http_reinit(ngx_http_check_peer_t *peer);

static ngx_int_t ngx_http_check_ssl_hello_init(ngx_http_check_peer_t *peer);
static ngx_int_t ngx_http_check_ssl_hello_parse(ngx_http_check_peer_t *peer);
static void ngx_http_check_ssl_hello_reinit(ngx_http_check_peer_t *peer);

static ngx_int_t ngx_http_check_smtp_init(ngx_http_check_peer_t *peer);
static ngx_int_t ngx_http_check_smtp_parse(ngx_http_check_peer_t *peer);
static void ngx_http_check_smtp_reinit(ngx_http_check_peer_t *peer);

static ngx_int_t ngx_http_check_mysql_init(ngx_http_check_peer_t *peer);
static ngx_int_t ngx_http_check_mysql_parse(ngx_http_check_peer_t *peer);
static void ngx_http_check_mysql_reinit(ngx_http_check_peer_t *peer);

static ngx_int_t ngx_http_check_pop3_init(ngx_http_check_peer_t *peer);
static ngx_int_t ngx_http_check_pop3_parse(ngx_http_check_peer_t *peer);
static void ngx_http_check_pop3_reinit(ngx_http_check_peer_t *peer);

static ngx_int_t ngx_http_check_imap_init(ngx_http_check_peer_t *peer);
static ngx_int_t ngx_http_check_imap_parse(ngx_http_check_peer_t *peer);
static void ngx_http_check_imap_reinit(ngx_http_check_peer_t *peer);


#define RANDOM "NGX_HTTP_CHECK_SSL_HELLO\n\n\n\n\n"

/* This is the SSLv3 CLIENT HELLO packet used in conjunction with the
 * check type of ssl_hello to ensure that the remote server speaks SSL.
 *
 * Check RFC 2246 (TLSv1.0) sections A.3 and A.4 for details.
 *
 * Some codes copy from HAProxy 1.4.1
 */
const char sslv3_client_hello_pkt[] = {
    "\x16"                /* ContentType         : 0x16 = Hanshake           */
        "\x03\x00"            /* ProtocolVersion     : 0x0300 = SSLv3            */
        "\x00\x79"            /* ContentLength       : 0x79 bytes after this one */
        "\x01"                /* HanshakeType        : 0x01 = CLIENT HELLO       */
        "\x00\x00\x75"        /* HandshakeLength     : 0x75 bytes after this one */
        "\x03\x00"            /* Hello Version       : 0x0300 = v3               */
        "\x00\x00\x00\x00"    /* Unix GMT Time (s)   : filled with <now> (@0x0B) */
        RANDOM                /* Random   : must be exactly 28 bytes  */
        "\x00"                /* Session ID length   : empty (no session ID)     */
        "\x00\x4E"            /* Cipher Suite Length : 78 bytes after this one   */
        "\x00\x01" "\x00\x02" "\x00\x03" "\x00\x04" /* 39 most common ciphers :  */
        "\x00\x05" "\x00\x06" "\x00\x07" "\x00\x08" /* 0x01...0x1B, 0x2F...0x3A  */
        "\x00\x09" "\x00\x0A" "\x00\x0B" "\x00\x0C" /* This covers RSA/DH,       */
        "\x00\x0D" "\x00\x0E" "\x00\x0F" "\x00\x10" /* various bit lengths,      */
        "\x00\x11" "\x00\x12" "\x00\x13" "\x00\x14" /* SHA1/MD5, DES/3DES/AES... */
        "\x00\x15" "\x00\x16" "\x00\x17" "\x00\x18"
        "\x00\x19" "\x00\x1A" "\x00\x1B" "\x00\x2F"
        "\x00\x30" "\x00\x31" "\x00\x32" "\x00\x33"
        "\x00\x34" "\x00\x35" "\x00\x36" "\x00\x37"
        "\x00\x38" "\x00\x39" "\x00\x3A"
        "\x01"                /* Compression Length  : 0x01 = 1 byte for types   */
        "\x00"                /* Compression Type    : 0x00 = NULL compression   */
};


#define HANDSHAKE    0x16
#define SERVER_HELLO 0x02

check_conf_t  ngx_check_types[] = {
    {
        NGX_HTTP_CHECK_TCP,
        "tcp",
        ngx_null_string,
        0,
        ngx_http_check_peek_handler,
        ngx_http_check_peek_handler,
        NULL,
        NULL,
        NULL,
        0
    },
    {
        NGX_HTTP_CHECK_HTTP,
        "http",
        ngx_string("GET / HTTP/1.0\r\n\r\n"),
        NGX_CONF_BITMASK_SET | NGX_CHECK_HTTP_2XX | NGX_CHECK_HTTP_3XX,
        ngx_http_check_send_handler,
        ngx_http_check_recv_handler,
        ngx_http_check_http_init,
        ngx_http_check_http_parse,
        ngx_http_check_http_reinit,
        1
    },
    {
        NGX_HTTP_CHECK_SSL_HELLO,
        "ssl_hello",
        ngx_string(sslv3_client_hello_pkt),
        0,
        ngx_http_check_send_handler,
        ngx_http_check_recv_handler,
        ngx_http_check_ssl_hello_init,
        ngx_http_check_ssl_hello_parse,
        ngx_http_check_ssl_hello_reinit,
        1
    },
    {
        NGX_HTTP_CHECK_SMTP,
        "smtp",
        ngx_string("HELO smtp.localdomain\r\n"),
        NGX_CONF_BITMASK_SET | NGX_CHECK_SMTP_2XX,
        ngx_http_check_send_handler,
        ngx_http_check_recv_handler,
        ngx_http_check_smtp_init,
        ngx_http_check_smtp_parse,
        ngx_http_check_smtp_reinit,
        1
    },
    {
        NGX_HTTP_CHECK_MYSQL,
        "mysql",
        ngx_null_string,
        0,
        ngx_http_check_send_handler,
        ngx_http_check_recv_handler,
        ngx_http_check_mysql_init,
        ngx_http_check_mysql_parse,
        ngx_http_check_mysql_reinit,
        1
    },
    {
        NGX_HTTP_CHECK_POP3,
        "pop3",
        ngx_null_string,
        0,
        ngx_http_check_send_handler,
        ngx_http_check_recv_handler,
        ngx_http_check_pop3_init,
        ngx_http_check_pop3_parse,
        ngx_http_check_pop3_reinit,
        1
    },
    {
        NGX_HTTP_CHECK_IMAP,
        "imap",
        ngx_null_string,
        0,
        ngx_http_check_send_handler,
        ngx_http_check_recv_handler,
        ngx_http_check_imap_init,
        ngx_http_check_imap_parse,
        ngx_http_check_imap_reinit,
        1
    },

    {0, "", ngx_null_string, 0, NULL, NULL, NULL, NULL, NULL, 0}
};


static ngx_uint_t ngx_http_check_shm_generation = 0;
static ngx_http_check_peers_t *check_peers_ctx = NULL;


ngx_uint_t 
ngx_http_check_peer_down(ngx_uint_t index)
{
    ngx_http_check_peer_t     *peer;

    if (check_peers_ctx == NULL || index >= check_peers_ctx->peers.nelts) {
        return 1;
    }

    peer = check_peers_ctx->peers.elts;

    return (peer[index].shm->down);
}


void 
ngx_http_check_get_peer(ngx_uint_t index) 
{
    ngx_http_check_peer_t     *peer;

    if (check_peers_ctx == NULL || index >= check_peers_ctx->peers.nelts) {
        return;
    }

    peer = check_peers_ctx->peers.elts;

    ngx_spinlock(&peer[index].shm->lock, ngx_pid, 1024);

    peer[index].shm->business++;
    peer[index].shm->access_count++;

    ngx_spinlock_unlock(&peer[index].shm->lock);
}


void 
ngx_http_check_free_peer(ngx_uint_t index) 
{
    ngx_http_check_peer_t     *peer;

    if (check_peers_ctx == NULL || index >= check_peers_ctx->peers.nelts) {
        return;
    }

    peer = check_peers_ctx->peers.elts;

    ngx_spinlock(&peer[index].shm->lock, ngx_pid, 1024);

    if (peer[index].shm->business > 0) {
        peer[index].shm->business--;
    }

    ngx_spinlock_unlock(&peer[index].shm->lock);
}


#define SHM_NAME_LEN 256

static ngx_int_t
ngx_http_upstream_check_init_shm_zone(ngx_shm_zone_t *shm_zone, void *data) 
{
    ngx_uint_t                       i;
    ngx_slab_pool_t                 *shpool;
    ngx_http_check_peers_t          *peers;
    ngx_http_check_peer_shm_t       *peer_shm;
    ngx_http_check_peers_shm_t      *peers_shm;

    peers = shm_zone->data;

    if (peers == NULL || peers->peers.nelts == 0) {
        return NGX_OK;
    }

    if (data) {
        peers_shm = data;
    }
    else {
        shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;

        peers_shm = ngx_slab_alloc(shpool, sizeof(*peers_shm) +
                (peers->peers.nelts - 1) * sizeof(ngx_http_check_peer_shm_t));

        if (peers_shm == NULL) {
            ngx_log_error(NGX_LOG_EMERG, shm_zone->shm.log, 0,
                    "http upstream check_shm_size is too small, you should set a larger size.");
            return NGX_ERROR;
        }
    }

    peers_shm->generation = ngx_http_check_shm_generation;

    for (i = 0; i < peers->peers.nelts; i++) {
        peer_shm = &peers_shm->peers[i];

        peer_shm->owner = NGX_INVALID_PID;

        peer_shm->access_time = 0;
        peer_shm->access_count = 0;

        peer_shm->fall_count = 0;
        peer_shm->rise_count = 0;

        peer_shm->business = 0;
        peer_shm->down = 1;
    }

    peers->peers_shm = peers_shm;

    return NGX_OK;
}


static ngx_int_t 
ngx_http_check_get_shm_name(ngx_str_t *shm_name, ngx_pool_t *pool) 
{
    u_char    *last;

    shm_name->data = ngx_palloc(pool, SHM_NAME_LEN);
    if (shm_name->data == NULL) {
        return NGX_ERROR;
    }

    last = ngx_snprintf(shm_name->data, SHM_NAME_LEN, "%s#%ui", "ngx_http_upstream_check", 
            ngx_http_check_shm_generation);

    shm_name->len = last - shm_name->data;

    return NGX_OK;
}


static ngx_shm_zone_t *
ngx_shared_memory_find(ngx_cycle_t *cycle, ngx_str_t *name, void *tag)
{
    ngx_uint_t        i;
    ngx_shm_zone_t   *shm_zone;
    ngx_list_part_t  *part;

    part = (ngx_list_part_t *) & (cycle->shared_memory.part);
    shm_zone = part->elts;

    for (i = 0; /* void */ ; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            shm_zone = part->elts;
            i = 0;
        }

        if (name->len != shm_zone[i].shm.name.len) {
            continue;
        }

        if (ngx_strncmp(name->data, shm_zone[i].shm.name.data, name->len)
                != 0)
        {
            continue;
        }

        if (tag != shm_zone[i].tag) {
            continue;
        }

        return &shm_zone[i];
    }

    return NULL;
}


static void 
ngx_http_check_clean_event(ngx_http_check_peer_t *peer) 
{
    ngx_connection_t             *c;

    c = peer->pc.connection;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0, 
            "http check clean event: index:%d, fd: %d", 
            peer->index, c->fd);

    ngx_close_connection(c);

    if (peer->check_timeout_ev.timer_set) {
        ngx_del_timer(&peer->check_timeout_ev);
    }

    peer->state = NGX_HTTP_CHECK_ALL_DONE;

    if (peer->check_data != NULL && peer->reinit) {
        peer->reinit(peer);
    }

    ngx_spinlock(&peer->shm->lock, ngx_pid, 1024);

    peer->shm->owner = NGX_INVALID_PID;

    ngx_spinlock_unlock(&peer->shm->lock);
}


static ngx_flag_t has_cleared = 0;

static void 
ngx_http_check_clear_all_events()
{
    ngx_uint_t                      i;
    ngx_http_check_peer_t          *peer;
    ngx_http_check_peers_t         *peers;
    ngx_http_check_peer_shm_t      *peer_shm;
    ngx_http_check_peers_shm_t     *peers_shm;

    if (has_cleared || check_peers_ctx == NULL) {
        return;
    }

    has_cleared = 1;

    peers = check_peers_ctx;
    peers_shm = peers->peers_shm;

    peer = peers->peers.elts;
    peer_shm = peers_shm->peers;
    for (i = 0; i < peers->peers.nelts; i++) {
        if (peer[i].check_ev.timer_set) {
            ngx_del_timer(&peer[i].check_ev);
        }
        if (peer_shm[i].owner == ngx_pid) {
            ngx_http_check_clean_event(&peer[i]);
        }
        if (peer[i].pool != NULL) {
            ngx_destroy_pool(peer[i].pool);
        }
    }
}


static ngx_int_t 
ngx_http_check_need_exit() 
{
    if (ngx_terminate || ngx_exiting || ngx_quit) {
        ngx_http_check_clear_all_events();
        return 1;
    }

    return 0;
}


static void 
ngx_http_check_finish_handler(ngx_event_t *event) 
{
    ngx_http_check_peer_t          *peer;

    if (ngx_http_check_need_exit()) {
        return;
    }

    peer = event->data;
}


static void 
ngx_http_check_status_update(ngx_http_check_peer_t *peer, ngx_int_t result) 
{
    ngx_http_upstream_check_srv_conf_t   *ucscf;

    ucscf = peer->conf;

    if (result) {
        peer->shm->rise_count++; 
        peer->shm->fall_count = 0; 
        if (peer->shm->down && peer->shm->rise_count >= ucscf->rise_count) {
            peer->shm->down = 0; 
        } 
    }
    else {
        peer->shm->rise_count = 0; 
        peer->shm->fall_count++; 
        if (!peer->shm->down && peer->shm->fall_count >= ucscf->fall_count) {
            peer->shm->down = 1; 
        }
    }

    peer->shm->access_time = ngx_current_msec; 
}


static void 
ngx_http_check_timeout_handler(ngx_event_t *event) 
{
    ngx_http_check_peer_t          *peer;

    if (ngx_http_check_need_exit()) {
        return;
    }

    peer = event->data;

    ngx_log_error(NGX_LOG_ERR, event->log, 0,
            "check time out with peer: %V ", &peer->peer_addr->name);

    ngx_http_check_status_update(peer, 0);
    ngx_http_check_clean_event(peer);
}


static void 
ngx_http_check_peek_handler(ngx_event_t *event)
{
    char                           buf[1];
    ngx_int_t                      n;
    ngx_err_t                      err;
    ngx_connection_t              *c;
    ngx_http_check_peer_t         *peer;

    if (ngx_http_check_need_exit()) {
        return;
    }

    c = event->data;
    peer = c->data;

    n = recv(c->fd, buf, 1, MSG_PEEK);

    err = ngx_socket_errno;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, err, 
            "http check upstream recv(): %d, fd: %d",
            n, c->fd);

    if (n >= 0 || err == NGX_EAGAIN) {
        ngx_http_check_status_update(peer, 1);
    }
    else {
        c->error = 1;
        ngx_http_check_status_update(peer, 0);
    }

    ngx_http_check_clean_event(peer);

    /*dummy*/
    ngx_http_check_finish_handler(event);
}


void 
http_field(void *data, const char *field, 
        size_t flen, const char *value, size_t vlen)
{
    ngx_str_t str_field, str_value;

    str_field.data = (u_char *) field;
    str_field.len = flen;

    str_value.data = (u_char *) value;
    str_value.len = vlen;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0, 
            "%V: %V", &str_field, &str_value);
}


void 
http_version(void *data, const char *at, size_t length)
{
    ngx_str_t str;

    str.data = (u_char *) at;
    str.len = length;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0, 
            "VERSION: \"%V\"", &str);
}


void 
status_code(void *data, const char *at, size_t length)
{
    ngx_http_check_peer_t *peer = data;
    ngx_http_check_ctx         *ctx;
    http_parser               *hp;
    ngx_str_t                  str;
    int                        code;

    str.data = (u_char *) at;
    str.len = length;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0, 
            "STATUS_CODE: \"%V\"", &str);

    ctx = peer->check_data;
    hp = ctx->parser;

    code = ngx_atoi((u_char*)at, length);

    if (code >= 200 && code < 300) {
        hp->status_code_n = NGX_CHECK_HTTP_2XX;
    }
    else if (code >= 300 && code < 400) {
        hp->status_code_n = NGX_CHECK_HTTP_3XX;
    }
    else if (code >= 400 && code < 500) {
        hp->status_code_n = NGX_CHECK_HTTP_4XX;
    }
    else if (code >= 500 && code < 600) {
        hp->status_code_n = NGX_CHECK_HTTP_5XX;
    }
    else {
        hp->status_code_n = NGX_CHECK_HTTP_ERR;
    }
}


void 
reason_phrase(void *data, const char *at, size_t length)
{
    ngx_str_t str;

    str.data = (u_char *) at;
    str.len = length;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0, 
            "REASON_PHRASE: \"%V\"", &str);
}


void 
header_done(void *data, const char *at, size_t length)
{
    /* foo */
}


static void 
check_http_parser_init(http_parser *hp, void *data) 
{
    hp->data = data;
    hp->http_field = http_field;
    hp->http_version = http_version;
    hp->status_code = status_code;
    hp->status_code_n = 0;
    hp->reason_phrase = reason_phrase;
    hp->header_done = header_done;

    http_parser_init(hp);
}


static ngx_int_t 
ngx_http_check_http_init(ngx_http_check_peer_t *peer) 
{
    ngx_http_check_ctx                  *ctx;
    ngx_http_upstream_check_srv_conf_t  *ucscf;

    ctx = peer->check_data;
    ucscf = peer->conf;

    ctx->send.start = ctx->send.pos = (u_char *)ucscf->send.data;
    ctx->send.end = ctx->send.last = ctx->send.start + ucscf->send.len;

    ctx->recv.start = ctx->recv.pos = NULL;
    ctx->recv.end = ctx->recv.last = NULL;

    ctx->parser = ngx_pcalloc(peer->pool, sizeof(http_parser));
    if (ctx->parser == NULL) {
        return NGX_ERROR;
    }

    check_http_parser_init(ctx->parser, peer);

    return NGX_OK;
}


static ngx_int_t 
ngx_http_check_http_parse(ngx_http_check_peer_t *peer) 
{
    ssize_t                              n, offset, length;
    http_parser                         *hp;
    ngx_http_check_ctx                  *ctx;
    ngx_http_upstream_check_srv_conf_t  *ucscf;

    ucscf = peer->conf;
    ctx = peer->check_data;
    hp = ctx->parser;

    if ((ctx->recv.last - ctx->recv.pos) > 0) {
        offset = ctx->recv.pos - ctx->recv.start;
        length = ctx->recv.last - ctx->recv.start;

        n = http_parser_execute(hp, (char *)ctx->recv.start, length, offset);
        ctx->recv.pos += n;

        if (http_parser_finish(hp) == -1) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                    "http parse error with peer: %V, recv data: %s", 
                    &peer->peer_addr->name, ctx->recv.start);
            return NGX_ERROR;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0, 
                "http_parse: hp->status_code_n: %d, conf: %d",
                hp->status_code_n, ucscf->status_alive);

        if (hp->status_code_n == 0) {
            return NGX_AGAIN;
        }
        else if (hp->status_code_n & ucscf->status_alive) {
            return NGX_OK;
        }
        else {
            return NGX_ERROR;
        }
    }
    else {
        return NGX_AGAIN;
    }

    return NGX_OK;
}


static void 
ngx_http_check_http_reinit(ngx_http_check_peer_t *peer) 
{
    ngx_http_check_ctx *ctx;

    ctx = peer->check_data;

    ctx->send.pos = ctx->send.start;
    ctx->send.last = ctx->send.end;

    ctx->recv.pos = ctx->recv.last = ctx->recv.start;

    check_http_parser_init(ctx->parser, peer);
}


static ngx_int_t 
ngx_http_check_ssl_hello_init(ngx_http_check_peer_t *peer) 
{
    ngx_http_check_ctx                  *ctx;
    ngx_http_upstream_check_srv_conf_t  *ucscf;

    ctx = peer->check_data;
    ucscf = peer->conf;

    ctx->send.start = ctx->send.pos = (u_char *)ucscf->send.data;
    ctx->send.end = ctx->send.last = ctx->send.start + ucscf->send.len;

    ctx->recv.start = ctx->recv.pos = NULL;
    ctx->recv.end = ctx->recv.last = NULL;

    return NGX_OK;
}


/* a rough check of server ssl_hello responses */
static ngx_int_t 
ngx_http_check_ssl_hello_parse(ngx_http_check_peer_t *peer)
{
    size_t                        size;
    server_ssl_hello_t           *resp;
    ngx_http_check_ctx            *ctx;

    ctx = peer->check_data;

    size = ctx->recv.last - ctx->recv.pos;
    if (size < sizeof(server_ssl_hello_t)) {
        return NGX_AGAIN;
    } 

    resp = (server_ssl_hello_t *) ctx->recv.pos;

    ngx_log_debug5(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0, 
            "http check ssl_parse, type: %d, version: %d.%d, length: %d, handshanke_type: %d, "
            "hello_version: %d.%d", 
            resp->msg_type, resp->version.major, resp->version.minor, 
            ntohs(resp->length), resp->handshake_type, 
            resp->hello_version.major, resp->hello_version.minor);

    if (resp->msg_type != HANDSHAKE) {
        return NGX_ERROR;
    }

    if (resp->handshake_type != SERVER_HELLO) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void 
ngx_http_check_ssl_hello_reinit(ngx_http_check_peer_t *peer) 
{
    ngx_http_check_ctx *ctx;

    ctx = peer->check_data;

    ctx->send.pos = ctx->send.start;
    ctx->send.last = ctx->send.end;

    ctx->recv.pos = ctx->recv.last = ctx->recv.start;
}


static void 
domain(void *data, const char *at, size_t length)
{
    ngx_str_t str;

    str.data = (u_char *) at;
    str.len = length;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0, 
            "DOMAIN: \"%V\"", &str);
}


static void 
greeting_text(void *data, const char *at, size_t length)
{
    ngx_str_t str;

    str.data = (u_char *) at;
    str.len = length;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0, 
            "GREETING_TEXT: \"%V\"", &str);
}


static void 
reply_code(void *data, const char *at, size_t length)
{
    int                         code;
    ngx_str_t                   str;
    smtp_parser                *sp;
    ngx_http_check_ctx         *ctx;
    ngx_http_check_peer_t      *peer = data;

    str.data = (u_char *) at;
    str.len = length;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0, 
            "REPLY_CODE: \"%V\"", &str);

    ctx = peer->check_data;
    sp = ctx->parser;

    code = ngx_atoi((u_char*)at, length);

    if (code >= 200 && code < 300) {
        sp->hello_reply_code = NGX_CHECK_SMTP_2XX;
    }
    else if (code >= 300 && code < 400) {
        sp->hello_reply_code = NGX_CHECK_SMTP_3XX;
    }
    else if (code >= 400 && code < 500) {
        sp->hello_reply_code = NGX_CHECK_SMTP_4XX;
    }
    else if (code >= 500 && code < 600) {
        sp->hello_reply_code = NGX_CHECK_SMTP_5XX;
    }
    else {
        sp->hello_reply_code = NGX_CHECK_SMTP_ERR;
    }
}


static void 
reply_text(void *data, const char *at, size_t length)
{
    ngx_str_t str;

    str.data = (u_char *) at;
    str.len = length;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0, 
            "REPLY_TEXT: \"%V\"", &str);
}


static void 
smtp_done(void *data, const char *at, size_t length)
{
    /* foo */
}


static void 
check_smtp_parser_init(smtp_parser *sp, void *data) 
{
    sp->data = data;
    sp->hello_reply_code = 0;

    sp->domain = domain;
    sp->greeting_text = greeting_text;
    sp->reply_code = reply_code;
    sp->reply_text = reply_text;
    sp->smtp_done = smtp_done;

    smtp_parser_init(sp);
}


static ngx_int_t 
ngx_http_check_smtp_init(ngx_http_check_peer_t *peer) 
{
    ngx_http_check_ctx                  *ctx;
    ngx_http_upstream_check_srv_conf_t  *ucscf;

    ctx = peer->check_data;
    ucscf = peer->conf;

    ctx->send.start = ctx->send.pos = (u_char *)ucscf->send.data;
    ctx->send.end = ctx->send.last = ctx->send.start + ucscf->send.len;

    ctx->recv.start = ctx->recv.pos = NULL;
    ctx->recv.end = ctx->recv.last = NULL;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0, 
            "smtp_init: send:%V", &ucscf->send);

    ctx->parser = ngx_pcalloc(peer->pool, sizeof(smtp_parser));
    if (ctx->parser == NULL) {
        return NGX_ERROR;
    }

    check_smtp_parser_init(ctx->parser, peer);

    return NGX_OK;
}


static ngx_int_t 
ngx_http_check_smtp_parse(ngx_http_check_peer_t *peer)
{
    ssize_t                        n, offset, length;
    smtp_parser                   *sp;
    ngx_http_check_ctx            *ctx;
    ngx_http_upstream_check_srv_conf_t  *ucscf;

    ucscf = peer->conf;
    ctx = peer->check_data;
    sp = ctx->parser;

    if (ctx->recv.last - ctx->recv.pos <= 0 ) {
        return NGX_AGAIN;
    }

    offset = ctx->recv.pos - ctx->recv.start;
    length = ctx->recv.last - ctx->recv.start;

    n = smtp_parser_execute(sp, (char *)ctx->recv.start, length, offset);
    ctx->recv.pos += n;

    if (smtp_parser_finish(sp) == -1) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                "smtp parse error with peer: %V, recv data: %s", 
                &peer->peer_addr->name, ctx->recv.pos);

        /*Some SMTP servers are not strictly designed with the RFC2821, but it does work*/
        if (*ctx->recv.start == '2') {
            return NGX_OK;
        }

        return NGX_ERROR;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0, 
            "smtp_parse: sp->hello_reply_code: %d, conf: %d",
            sp->hello_reply_code, ucscf->status_alive);

    if (sp->hello_reply_code == 0) {
        return NGX_AGAIN;
    }
    else if (sp->hello_reply_code & ucscf->status_alive) {
        return NGX_OK;
    }
    else {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_http_check_smtp_reinit(ngx_http_check_peer_t *peer) 
{
    ngx_http_check_ctx *ctx;

    ctx = peer->check_data;

    ctx->send.pos = ctx->send.start;
    ctx->send.last = ctx->send.end;

    ctx->recv.pos = ctx->recv.last = ctx->recv.start;

    check_smtp_parser_init(ctx->parser, peer);
}


static ngx_int_t 
ngx_http_check_mysql_init(ngx_http_check_peer_t *peer) 
{
    ngx_http_check_ctx                  *ctx;
    ngx_http_upstream_check_srv_conf_t  *ucscf;

    ctx = peer->check_data;
    ucscf = peer->conf;

    ctx->send.start = ctx->send.pos = (u_char *)ucscf->send.data;
    ctx->send.end = ctx->send.last = ctx->send.start + ucscf->send.len;

    ctx->recv.start = ctx->recv.pos = NULL;
    ctx->recv.end = ctx->recv.last = NULL;

    return NGX_OK;
}


static ngx_int_t 
ngx_http_check_mysql_parse(ngx_http_check_peer_t *peer) 
{
    ngx_http_check_ctx            *ctx;
    mysql_handshake_init_t        *handshake;

    ctx = peer->check_data;

    if (ctx->recv.last - ctx->recv.pos <= 0 ) {
        return NGX_AGAIN;
    }

    handshake = (mysql_handshake_init_t *) ctx->recv.pos;

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0, 
            "mysql_parse: packet_number=%d, protocol=%d, server=%s", 
            handshake->packet_number,
            handshake->protocol_version,
            handshake->others);

    /* The mysql greeting packet's serial number always begin with 0. */
    if (handshake->packet_number != 0x00) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_http_check_mysql_reinit(ngx_http_check_peer_t *peer) 
{
    ngx_http_check_ctx *ctx;

    ctx = peer->check_data;

    ctx->send.pos = ctx->send.start;
    ctx->send.last = ctx->send.end;

    ctx->recv.pos = ctx->recv.last = ctx->recv.start;
}


static ngx_int_t 
ngx_http_check_pop3_init(ngx_http_check_peer_t *peer) 
{
    ngx_http_check_ctx                  *ctx;
    ngx_http_upstream_check_srv_conf_t  *ucscf;

    ctx = peer->check_data;
    ucscf = peer->conf;

    ctx->send.start = ctx->send.pos = (u_char *)ucscf->send.data;
    ctx->send.end = ctx->send.last = ctx->send.start + ucscf->send.len;

    ctx->recv.start = ctx->recv.pos = NULL;
    ctx->recv.end = ctx->recv.last = NULL;

    return NGX_OK;
}


static ngx_int_t 
ngx_http_check_pop3_parse(ngx_http_check_peer_t *peer) 
{
    u_char                         ch;
    ngx_http_check_ctx            *ctx;

    ctx = peer->check_data;

    if (ctx->recv.last - ctx->recv.pos <= 0 ) {
        return NGX_AGAIN;
    }

    ch = *(ctx->recv.start);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0, 
            "pop3_parse: packet_greeting \"%s\"", ctx->recv.start);

    /* RFC 1939
       There are currently two status indicators: positive ("+OK") and 
       negative ("-ERR").  Servers MUST send the "+OK" and "-ERR" in upper case.
       */
    if (ch != '+') {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_http_check_pop3_reinit(ngx_http_check_peer_t *peer) 
{

    ngx_http_check_ctx *ctx;

    ctx = peer->check_data;

    ctx->send.pos = ctx->send.start;
    ctx->send.last = ctx->send.end;

    ctx->recv.pos = ctx->recv.last = ctx->recv.start;
}


static ngx_int_t 
ngx_http_check_imap_init(ngx_http_check_peer_t *peer) 
{
    ngx_http_check_ctx                  *ctx;
    ngx_http_upstream_check_srv_conf_t  *ucscf;

    ctx = peer->check_data;
    ucscf = peer->conf;

    ctx->send.start = ctx->send.pos = (u_char *)ucscf->send.data;
    ctx->send.end = ctx->send.last = ctx->send.start + ucscf->send.len;

    ctx->recv.start = ctx->recv.pos = NULL;
    ctx->recv.end = ctx->recv.last = NULL;

    return NGX_OK;
}


static ngx_int_t 
ngx_http_check_imap_parse(ngx_http_check_peer_t *peer) 
{
    u_char                        *p;
    ngx_http_check_ctx            *ctx;

    ctx = peer->check_data;

    if (ctx->recv.last - ctx->recv.pos <= 0 ) {
        return NGX_AGAIN;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0, 
            "imap_parse: packet_greeting \"%s\"", ctx->recv.start);

    /* RFC 3501
       command         = tag SP (command-any / command-auth / command-nonauth /
       command-select) CRLF
       */

    p = ctx->recv.start;
    while (p < ctx->recv.last) {

        if (*p == ' ') {
            if ((p + 2) >= ctx->recv.last) {
                return NGX_AGAIN;
            }
            else if (*(p + 1) == 'O' && *(p + 2) == 'K') {
                return NGX_OK;
            }
            else {
                return NGX_ERROR;
            }
        }

        p++;
    }

    return NGX_AGAIN;
}


static void
ngx_http_check_imap_reinit(ngx_http_check_peer_t *peer) 
{
    ngx_http_check_ctx *ctx;

    ctx = peer->check_data;

    ctx->send.pos = ctx->send.start;
    ctx->send.last = ctx->send.end;

    ctx->recv.pos = ctx->recv.last = ctx->recv.start;
}


static void 
ngx_http_check_send_handler(ngx_event_t *event) 
{
    ssize_t                         size;
    ngx_err_t                       err;
    ngx_connection_t               *c;
    ngx_http_check_ctx             *ctx;
    ngx_http_check_peer_t          *peer;

    if (ngx_http_check_need_exit()) {
        return;
    }

    c = event->data;
    peer = c->data;

    if (c->pool == NULL) {
        ngx_log_error(NGX_LOG_ERR, event->log, 0,
                "check pool NULL with peer: %V ", &peer->peer_addr->name);

        goto check_send_fail;
    }

    if (peer->state != NGX_HTTP_CHECK_CONNECT_DONE) {
        if (ngx_handle_write_event(c->write, 0) != NGX_OK) {
            goto check_send_fail;
        }

        return;
    }

    if (peer->check_data == NULL) {

        peer->check_data = ngx_pcalloc(c->pool, sizeof(ngx_http_check_ctx));
        if (peer->check_data == NULL) {
            goto check_send_fail;
        }

        if (peer->init == NULL || peer->init(peer) != NGX_OK) {

            ngx_log_error(NGX_LOG_ERR, event->log, 0,
                    "check init error with peer: %V ", &peer->peer_addr->name);

            goto check_send_fail;
        }
    }

    ctx = peer->check_data;

    while (ctx->send.pos < ctx->send.last) {

        size = c->send(c, ctx->send.pos, ctx->send.last - ctx->send.pos);
        err = (size >=0) ? 0 : ngx_socket_errno;

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, err, 
                "http check send size: %d, total: %d", size, ctx->send.last - ctx->send.pos);

        if (size >= 0) {
            ctx->send.pos += size;
        }
        else if (size == NGX_AGAIN) {
            return;
        }
        else {
            c->error = 1;
            goto check_send_fail;
        }
    }

    if (ctx->send.pos == ctx->send.last) {
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, c->log, 0, "http check send done.");
        peer->state = NGX_HTTP_CHECK_SEND_DONE;
    }

    return;

check_send_fail:
    ngx_http_check_status_update(peer, 0);
    ngx_http_check_clean_event(peer);
    return;
}


static void 
ngx_http_check_recv_handler(ngx_event_t *event) 
{
    u_char                         *new_buf;
    ssize_t                         size, n, rc;
    ngx_err_t                       err;
    ngx_connection_t               *c;
    ngx_http_check_ctx             *ctx;
    ngx_http_check_peer_t          *peer;

    if (ngx_http_check_need_exit()) {
        return;
    }

    c = event->data;
    peer = c->data;

    if (peer->state != NGX_HTTP_CHECK_SEND_DONE) {

        if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
            goto check_recv_fail;
        }

        return;
    }

    ctx = peer->check_data;

    if (ctx->recv.start == NULL) {
        /* 2048, is it enough? */
        ctx->recv.start = ngx_palloc(c->pool, ngx_pagesize/2);
        if (ctx->recv.start == NULL) {
            goto check_recv_fail;
        }

        ctx->recv.last = ctx->recv.pos = ctx->recv.start;
        ctx->recv.end = ctx->recv.start + ngx_pagesize/2;
    }

    while (1) {
        n = ctx->recv.end - ctx->recv.last;
        /*Not enough buffer? Enlarge twice*/
        if (n == 0) {
            size = ctx->recv.end - ctx->recv.start;
            new_buf = ngx_palloc(c->pool, size * 2);
            if (new_buf == NULL) {
                goto check_recv_fail;
            }

            ngx_memcpy(new_buf, ctx->recv.start, size);

            ctx->recv.pos = ctx->recv.start = new_buf;
            ctx->recv.last = new_buf + size;
            ctx->recv.end = new_buf + size * 2;

            n = ctx->recv.end - ctx->recv.last;
        }

        size = c->recv(c, ctx->recv.last, n);
        err = (size >= 0) ? 0 : ngx_socket_errno;

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, err, 
                "http check recv size: %d, peer: %V", size, &peer->peer_addr->name);

        if (size > 0) {
            ctx->recv.last += size;
            continue;
        } else if (size == 0 || size == NGX_AGAIN) {
            break;
        }
        else {
            c->error = 1;
            goto check_recv_fail;
        }
    }

    rc = peer->parse(peer); 

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0, 
            "http check parse rc: %d, peer: %V", rc, &peer->peer_addr->name);

    switch (rc) {
    case NGX_AGAIN:
        return;
    case NGX_ERROR:
        ngx_log_error(NGX_LOG_ERR, event->log, 0,
                "check protocol %s error with peer: %V ", 
                peer->conf->check_type_conf->name, &peer->peer_addr->name);
        ngx_http_check_status_update(peer, 0);
        break;
    case NGX_OK:
    default:
        ngx_http_check_status_update(peer, 1);
    }

    peer->state = NGX_HTTP_CHECK_RECV_DONE;
    ngx_http_check_clean_event(peer);
    return;

check_recv_fail:
    ngx_http_check_status_update(peer, 0);
    ngx_http_check_clean_event(peer);
    return;
}


static void 
ngx_http_check_connect_handler(ngx_event_t *event) 
{
    ngx_int_t                            rc;
    ngx_connection_t                    *c;
    ngx_http_check_peer_t               *peer;
    ngx_http_upstream_check_srv_conf_t  *ucscf;

    if (ngx_http_check_need_exit()) {
        return;
    }

    peer = event->data;
    ucscf = peer->conf;

    ngx_memzero(&peer->pc, sizeof(ngx_peer_connection_t));

    peer->pc.sockaddr = peer->peer_addr->sockaddr;
    peer->pc.socklen = peer->peer_addr->socklen;
    peer->pc.name = &peer->peer_addr->name;

    peer->pc.get = ngx_event_get_peer;
    peer->pc.log = event->log;
    peer->pc.log_error = NGX_ERROR_ERR; 

    peer->pc.cached = 0;
    peer->pc.connection = NULL;

    rc = ngx_event_connect_peer(&peer->pc);

    if (rc == NGX_ERROR || rc == NGX_DECLINED) {
        ngx_http_check_status_update(peer, 0);
        return;
    }

    /*NGX_OK or NGX_AGAIN*/
    c = peer->pc.connection;
    c->data = peer;
    c->log = peer->pc.log;
    c->sendfile = 0;
    c->read->log = c->log;
    c->write->log = c->log;
    c->pool = peer->pool;

    peer->state = NGX_HTTP_CHECK_CONNECT_DONE;

    c->write->handler = peer->send_handler;
    c->read->handler = peer->recv_handler;

    ngx_add_timer(&peer->check_timeout_ev, ucscf->check_timeout);
}


static void 
ngx_http_check_begin_handler(ngx_event_t *event) 
{
    ngx_http_check_peer_t                *peer;
    ngx_http_upstream_check_srv_conf_t   *ucscf;

    if (ngx_http_check_need_exit()) {
        return;
    }

    peer = event->data;
    ucscf = peer->conf;

    ngx_add_timer(event, ucscf->check_interval/2);

    /*This process are processing the event now.*/
    if (peer->shm->owner == ngx_pid) {
        return;
    }

    ngx_log_debug4(NGX_LOG_DEBUG_HTTP, event->log, 0, 
            "http check begin handler index:%ud, owner: %d, ngx_pid: %ud, time:%ud", 
            peer->index, peer->shm->owner, ngx_pid, 
            (ngx_current_msec - peer->shm->access_time));

    ngx_spinlock(&peer->shm->lock, ngx_pid, 1024);

    if (((ngx_current_msec - peer->shm->access_time) >= ucscf->check_interval) && 
            peer->shm->owner == NGX_INVALID_PID)
    {
        peer->shm->owner = ngx_pid;
    }

    ngx_spinlock_unlock(&peer->shm->lock);

    if (peer->shm->owner == ngx_pid) {
        ngx_http_check_connect_handler(event);
    }
}

char * 
ngx_http_upstream_check_init_shm(ngx_conf_t *cf, void *conf)
{
    ngx_str_t                            *shm_name;
    ngx_uint_t                            shm_size;
    ngx_shm_zone_t                       *shm_zone;
    ngx_http_upstream_main_conf_t        *umcf;
    ngx_http_upstream_check_main_conf_t  *ucmcf = conf;

    umcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_upstream_module);

    if (ucmcf->peers->peers.nelts > 0) {
        ngx_http_check_shm_generation++;

        shm_name = &ucmcf->peers->check_shm_name;

        if (ngx_http_check_get_shm_name(shm_name, cf->pool) == NGX_ERROR) {
            return NGX_CONF_ERROR;
        }

        /*the default check shmare memory size*/
        shm_size = (umcf->upstreams.nelts + 1 )* ngx_pagesize;

        shm_size = shm_size < ucmcf->check_shm_size ? ucmcf->check_shm_size : shm_size;

        shm_zone = ngx_shared_memory_add(cf, shm_name, shm_size, &ngx_http_upstream_check_module);

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, cf->log, 0,
                "[http_upstream] upsteam:%V, shm_zone size:%ui", shm_name, shm_size);

        shm_zone->data = ucmcf->peers;
        check_peers_ctx = ucmcf->peers;

        shm_zone->init = ngx_http_upstream_check_init_shm_zone;
    }

    return NGX_CONF_OK;
}


ngx_int_t 
ngx_http_check_add_timers(ngx_cycle_t *cycle) 
{
    ngx_uint_t                          i;
    ngx_msec_t                          t, delay;
    check_conf_t                       *cf;
    ngx_http_check_peer_t              *peer;
    ngx_http_check_peers_t             *peers;
    ngx_http_check_peer_shm_t          *peer_shm;
    ngx_http_check_peers_shm_t         *peers_shm;
    ngx_http_upstream_check_srv_conf_t *ucscf;

    peers = check_peers_ctx;
    peers_shm = peers->peers_shm;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, cycle->log, 0, 
            "http check upstream init_process, shm_name: %V, peer number: %ud",
            &peers->check_shm_name, peers->peers.nelts);

    srand(ngx_pid);

    peer = peers->peers.elts;
    peer_shm = peers_shm->peers;

    for (i = 0; i < peers->peers.nelts; i++) {
        peer[i].shm = &peer_shm[i];

        peer[i].check_ev.handler = ngx_http_check_begin_handler;
        peer[i].check_ev.log = cycle->log;
        peer[i].check_ev.data = &peer[i];
        peer[i].check_ev.timer_set = 0;

        peer[i].check_timeout_ev.handler = ngx_http_check_timeout_handler;
        peer[i].check_timeout_ev.log = cycle->log;
        peer[i].check_timeout_ev.data = &peer[i];
        peer[i].check_timeout_ev.timer_set = 0;

        ucscf = peer[i].conf;
        cf = ucscf->check_type_conf;

        if (cf->need_pool) {
            peer[i].pool = ngx_create_pool(ngx_pagesize, cycle->log);
            if (peer[i].pool == NULL) {
                return NGX_ERROR;
            }
        }

        peer[i].send_handler = cf->send_handler;
        peer[i].recv_handler = cf->recv_handler;

        peer[i].init = cf->init;
        peer[i].parse = cf->parse;
        peer[i].reinit = cf->reinit;

        /* 
         * Default delay interval is 1 second. 
         * I don't want to trigger the check event too close.
         * */
        delay = ucscf->check_interval > 1000 ? ucscf->check_interval : 1000;
        t = ngx_random() % delay;

        ngx_add_timer(&peer[i].check_ev, t);
    }

    return NGX_OK;
}


ngx_int_t 
ngx_http_upstream_check_status_handler(ngx_http_request_t *r) 
{
    ngx_int_t                       rc;
    ngx_buf_t                      *b;
    ngx_str_t                       shm_name;
    ngx_uint_t                      i;
    ngx_chain_t                     out;
    ngx_shm_zone_t                 *shm_zone;
    ngx_http_check_peer_t          *peer;
    ngx_http_check_peers_t         *peers;
    ngx_http_check_peer_shm_t      *peer_shm;
    ngx_http_check_peers_shm_t     *peers_shm;


    if (r->method != NGX_HTTP_GET && r->method != NGX_HTTP_HEAD) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    rc = ngx_http_discard_request_body(r);

    if (rc != NGX_OK) {
        return rc;
    }

    r->headers_out.content_type.len = sizeof("text/html; charset=utf-8") - 1;
    r->headers_out.content_type.data = (u_char *) "text/html; charset=utf-8";

    if (r->method == NGX_HTTP_HEAD) {
        r->headers_out.status = NGX_HTTP_OK;

        rc = ngx_http_send_header(r);

        if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
            return rc;
        }
    }

    if (ngx_http_check_get_shm_name(&shm_name, r->pool) == NGX_ERROR) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    shm_zone = ngx_shared_memory_find((ngx_cycle_t *)ngx_cycle, &shm_name, 
            &ngx_http_upstream_check_module);

    if (shm_zone == NULL || shm_zone->data == NULL) {

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "[http upstream check] can not find the shared memory zone \"%V\" ", &shm_name);

        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    peers = shm_zone->data;
    peers_shm = peers->peers_shm;

    peer = peers->peers.elts;
    peer_shm = peers_shm->peers;

    b = ngx_create_temp_buf(r->pool, ngx_pagesize);
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    out.buf = b;
    out.next = NULL;

    b->last = ngx_sprintf(b->last, 
            "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\n"
            "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n"
            "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n"
            "<head>\n"
            "  <title>Nginx http upstream check status</title>\n"
            "</head>\n"
            "<body>\n"
            "<h1>Nginx http upstream check status</h1>\n"
            "<h2>Check upstream server number: %ui, shm_name: %V</h2>\n"
            "<table style=\"background-color:white\" cellspacing=\"0\" cellpadding=\"3\" border=\"1\">\n"
            "  <tr bgcolor=\"#C0C0C0\">\n"
            "    <th>Index</th>\n"
            "    <th>Name</th>\n"
            "    <th>Status</th>\n"
            "    <th>Business</th>\n"
            "    <th>Rise counts</th>\n"
            "    <th>Fall counts</th>\n"
            "    <th>Access counts</th>\n"
            "    <th>Check type</th>\n"
            "  </tr>\n",
        peers->peers.nelts, &shm_name);

    for (i = 0; i < peers->peers.nelts; i++) {
        b->last = ngx_sprintf(b->last, 
                "  <tr%s>\n"
                "    <td>%ui</td>\n" 
                "    <td>%V</td>\n" 
                "    <td>%s</td>\n" 
                "    <td>%ui</td>\n" 
                "    <td>%ui</td>\n" 
                "    <td>%ui</td>\n" 
                "    <td>%ui</td>\n" 
                "    <td>%s</td>\n" 
                "  </tr>\n",
                peer_shm[i].down ? " bgcolor=\"#FF0000\"" : "",
                i, 
                &peer[i].peer_addr->name, 
                peer_shm[i].down ? "down" : "up",
                peer_shm[i].business,
                peer_shm[i].rise_count, 
                peer_shm[i].fall_count, 
                peer_shm[i].access_count, 
                peer[i].conf->check_type_conf->name);
    }

    b->last = ngx_sprintf(b->last, 
            "</table>\n"
            "</body>\n"
            "</html>\n");

    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = b->last - b->pos;

    b->last_buf = 1;

    rc = ngx_http_send_header(r);

    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    return ngx_http_output_filter(r, &out);
}
