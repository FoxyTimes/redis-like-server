#include "protocol_parser.h"
#include "logger.h"
#include "utilities.h"
#include <string.h>


static RespObject *new_resp() {
    RespObject *o = xcalloc(1, sizeof(*o));
    return o;
}


ssize_t reader_read(RespReader *r, void *dst, size_t n) {
    if (r->pos >= r->len) return 0;
    size_t avail = r->len - r->pos;
    if (n > avail) n = avail;
    memcpy(dst, r->data + r->pos, n);
    r->pos += n;
    LOG(LOG_DEBUG, "reader_read %zu bytes, new pos=%zu/%zu\n", n, r->pos, r->len);
    return n;
}

ssize_t reader_read_line(RespReader *r, char *buf, size_t maxlen) {
    size_t i = 0;
    while (i < maxlen - 1 && r->pos < r->len) {
        char c = r->data[r->pos++];
        if (c == '\r') {
            if (r->pos < r->len && r->data[r->pos] == '\n') r->pos++;
            break;
        }
        if (c == '\n') break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    LOG(LOG_DEBUG, "reader_read_line: '%s', pos=%zu/%zu\n", buf, r->pos, r->len);
    return i;
}


RespObject *parse_resp_object(RespReader *r) {
    char prefix;

    if (reader_read(r, &prefix, 1) != 1) {
        LOG(LOG_DEBUG, "EOF reached while expecting prefix\n");
        return NULL;
    }

    LOG(LOG_DEBUG, "parse_resp_object: prefix='%c', pos=%zu/%zu\n", prefix, r->pos, r->len);

    switch (prefix) {

        case '+': {
            char buf[1024];
            reader_read_line(r, buf, sizeof(buf));

            RespObject *o = new_resp();
            o->type = RESP_SIMPLE_STRING;
            o->str = strdup(buf);

            LOG(LOG_DEBUG, "parsed simple string: '%s'\n", buf);
            return o;
        }

        case '-': {
            char buf[1024];
            reader_read_line(r, buf, sizeof(buf));

            RespObject *o = new_resp();
            o->type = RESP_ERROR;
            o->str = strdup(buf);

            LOG(LOG_DEBUG, "parsed error: '%s'\n", buf);
            return o;
        }

        case ':': {
            char buf[64];
            reader_read_line(r, buf, sizeof(buf));

            RespObject *o = new_resp();
            o->type = RESP_INTEGER;
            o->integer = atoll(buf);

            LOG(LOG_DEBUG, "parsed integer: %lld\n", o->integer);
            return o;
        }

        case '$': {
            char lenbuf[64];
            reader_read_line(r, lenbuf, sizeof(lenbuf));
            int len = atoi(lenbuf);

            RespObject *o = new_resp();

            if (len == -1) {
                o->type = RESP_NULL;
                LOG(LOG_DEBUG, "parsed null bulk string\n");
                return o;
            }

            o->type = RESP_BULK_STRING;
            o->str = xcalloc(len + 1, 1);

            if (reader_read(r, o->str, len) != len) {
                LOG(LOG_DEBUG, "truncated bulk string!\n");
            }

            char crlf[2];
            reader_read(r, crlf, 2);

            LOG(LOG_DEBUG, "parsed bulk string: '%s'\n", o->str);
            return o;
        }

        case '*': {
            char lenbuf[64];
            reader_read_line(r, lenbuf, sizeof(lenbuf));
            int n = atoi(lenbuf);

            RespObject *o = new_resp();

            if (n == -1) {
                o->type = RESP_NULL;
                LOG(LOG_DEBUG, "parsed null array\n");
                return o;
            }

            o->type = RESP_ARRAY;
            o->elements = n;
            o->items = xcalloc(n, sizeof(RespObject*));

            LOG(LOG_DEBUG, "parsing array with %d items\n", n);

            for (int i = 0; i < n; i++) {
                o->items[i] = parse_resp_object(r);
                if (!o->items[i])
                    LOG(LOG_DEBUG, "warning: array item %d is NULL\n", i);
            }

            return o;
        }

        default:
            LOG(LOG_DEBUG, "Unknown RESP prefix: %c\n", prefix);
            return NULL;
    }
}


void print_resp(RespObject *o, int indent) {
    if (!o) return;

    for (int i = 0; i < indent; i++) printf("  ");

    switch (o->type) {
        case RESP_SIMPLE_STRING:
            printf("(simple) \"%s\"\n", o->str);
            break;
        case RESP_ERROR:
            printf("(error) \"%s\"\n", o->str);
            break;
        case RESP_INTEGER:
            printf("(int) %lld\n", o->integer);
            break;
        case RESP_BULK_STRING:
            printf("(bulk) \"%s\"\n", o->str);
            break;
        case RESP_NULL:
            printf("(null)\n");
            break;
    }
}




void add_callback(RespObject* command, void (*on_complete)(void* arg)) {
    LOG(LOG_INFO, "ADD CALLBACK");
    command->on_complete = on_complete;
}


ParseResult parse_resp_stream(char *data, size_t len) {
    RespReader r = { data, 0, len };

    size_t capacity = 10;
    size_t count = 0;

    RespObject **cmds = xcalloc(capacity, sizeof(*cmds));

    while (r.pos < r.len) {
        size_t start_pos = r.pos;

        RespObject *cmd = parse_resp_object(&r);

        if (cmd == NULL) {
            r.pos = start_pos;
            break;
        }

        if (count == capacity) {
            capacity *= 2;
            cmds = xrealloc(cmds, capacity * sizeof(*cmds));
        }

        cmds[count++] = cmd;
    }

    ParseResult res = { cmds, count, r.pos };
    return res;
}

void free_parse_result(ParseResult *r) {
    if (!r) return;

    for (size_t i = 0; i < r->count; i++) {
        free_resp_object(r->cmds[i]);
    }

    free(r->cmds);

}