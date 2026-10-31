#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "forge_json.h"
#include "forge_runtime.h"

typedef struct {
    char *p;
    size_t n;
    size_t cap;
} sb;

typedef struct { volatile PVOID request; volatile LONG *cancel; volatile LONG done; } cancel_watch;
static DWORD WINAPI watch_request_cancel(void *context)
{
    cancel_watch *watch = (cancel_watch *)context;
    for (;;) {
        if (InterlockedCompareExchange(watch->cancel, 0, 0)) {
            HINTERNET request = (HINTERNET)InterlockedExchangePointer(&watch->request, NULL);
            if (request) WinHttpCloseHandle(request);
            break;
        }
        if (InterlockedCompareExchange(&watch->done, 0, 0)) break;
        Sleep(5);
    }
    return 0;
}

static int reserve(sb *b, size_t add)
{
    if (add > SIZE_MAX - b->n - 1) return -1;
    size_t need = b->n + add + 1;
    if (need <= b->cap) return 0;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) return -1;
        cap *= 2;
    }
    char *p = (char *)realloc(b->p, cap);
    if (!p) return -1;
    b->p = p;
    b->cap = cap;
    return 0;
}

static int putn(sb *b, const char *s, size_t n)
{
    if (reserve(b, n) != 0) return -1;
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = '\0';
    return 0;
}

static int putsb(sb *b, const char *s)
{
    return putn(b, s, strlen(s));
}

static int json_string_n(sb *b, const char *s, size_t n)
{
    if (!s) return putsb(b, "null");
    if (putsb(b, "\"") != 0) return -1;
    for (size_t i = 0; i < n; i++) {
        const unsigned char *p = (const unsigned char *)s + i;
        char escaped[7];
        switch (*p) {
        case '"': if (putsb(b, "\\\"") != 0) return -1; break;
        case '\\': if (putsb(b, "\\\\") != 0) return -1; break;
        case '\b': if (putsb(b, "\\b") != 0) return -1; break;
        case '\f': if (putsb(b, "\\f") != 0) return -1; break;
        case '\n': if (putsb(b, "\\n") != 0) return -1; break;
        case '\r': if (putsb(b, "\\r") != 0) return -1; break;
        case '\t': if (putsb(b, "\\t") != 0) return -1; break;
        default:
            if (*p < 0x20) {
                snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
                if (putsb(b, escaped) != 0) return -1;
            } else if (putn(b, (const char *)p, 1) != 0) {
                return -1;
            }
        }
    }
    return putsb(b, "\"");
}

static int json_string(sb *b, const char *s)
{
    return s ? json_string_n(b, s, strlen(s)) : putsb(b, "null");
}

static void set_error(char *out, unsigned cap, const char *message)
{
    if (out && cap) snprintf(out, cap, "%s", message ? message : "error");
}

static void free_tool_calls(fr_reply *reply)
{
    if (!reply) return;
    for (size_t i = 0; i < reply->tool_call_count; i++) {
        free(reply->tool_calls[i].id);
        free(reply->tool_calls[i].name);
        free(reply->tool_calls[i].arguments);
    }
    free(reply->tool_calls);
    reply->tool_calls = NULL;
    reply->tool_call_count = 0;
}

void fr_reply_free(fr_reply *reply)
{
    if (!reply) return;
    free(reply->text);
    free(reply->raw_json);
    free(reply->finish_reason);
    free_tool_calls(reply);
    memset(reply, 0, sizeof(*reply));
}

typedef struct {
    const char *s;
    size_t pos;
    size_t len;
    unsigned depth;
} json_scan;

static void json_ws(json_scan *j)
{
    while (j->pos < j->len && (j->s[j->pos] == ' ' || j->s[j->pos] == '\t' ||
           j->s[j->pos] == '\r' || j->s[j->pos] == '\n')) j->pos++;
}

static int json_string_scan(json_scan *j)
{
    if (j->pos >= j->len || j->s[j->pos++] != '"') return -1;
    while (j->pos < j->len) {
        unsigned char c = (unsigned char)j->s[j->pos++];
        if (c == '"') return 0;
        if (c < 0x20) return -1;
        if (c >= 0x80) {
            unsigned need;
            unsigned cp;
            if (c >= 0xc2 && c <= 0xdf) { need = 1; cp = c & 0x1f; }
            else if (c >= 0xe0 && c <= 0xef) { need = 2; cp = c & 0x0f; }
            else if (c >= 0xf0 && c <= 0xf4) { need = 3; cp = c & 0x07; }
            else return -1;
            if (j->len - j->pos < need) return -1;
            for (unsigned i = 0; i < need; i++) {
                unsigned char tail = (unsigned char)j->s[j->pos++];
                if ((tail & 0xc0) != 0x80) return -1;
                cp = (cp << 6) | (tail & 0x3f);
            }
            if ((need == 2 && cp < 0x800) || (need == 3 && cp < 0x10000) ||
                (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) return -1;
            continue;
        }
        if (c != '\\') continue;
        if (j->pos >= j->len) return -1;
        c = (unsigned char)j->s[j->pos++];
        if (strchr("\"\\/bfnrt", c)) continue;
        if (c != 'u' || j->len - j->pos < 4) return -1;
        for (int i = 0; i < 4; i++) {
            if (!isxdigit((unsigned char)j->s[j->pos++])) return -1;
        }
    }
    return -1;
}

static int json_value_scan(json_scan *j);

static int json_number_scan(json_scan *j)
{
    if (j->s[j->pos] == '-') j->pos++;
    if (j->pos >= j->len) return -1;
    if (j->s[j->pos] == '0') {
        j->pos++;
        if (j->pos < j->len && isdigit((unsigned char)j->s[j->pos])) return -1;
    } else {
        if (j->s[j->pos] < '1' || j->s[j->pos] > '9') return -1;
        while (j->pos < j->len && isdigit((unsigned char)j->s[j->pos])) j->pos++;
    }
    if (j->pos < j->len && j->s[j->pos] == '.') {
        j->pos++;
        if (j->pos >= j->len || !isdigit((unsigned char)j->s[j->pos])) return -1;
        while (j->pos < j->len && isdigit((unsigned char)j->s[j->pos])) j->pos++;
    }
    if (j->pos < j->len && (j->s[j->pos] == 'e' || j->s[j->pos] == 'E')) {
        j->pos++;
        if (j->pos < j->len && (j->s[j->pos] == '+' || j->s[j->pos] == '-')) j->pos++;
        if (j->pos >= j->len || !isdigit((unsigned char)j->s[j->pos])) return -1;
        while (j->pos < j->len && isdigit((unsigned char)j->s[j->pos])) j->pos++;
    }
    return 0;
}

static int json_container_scan(json_scan *j, char open, char close)
{
    if (++j->depth > 128) return -1;
    j->pos++;
    json_ws(j);
    if (j->pos < j->len && j->s[j->pos] == close) {
        j->pos++;
        j->depth--;
        return 0;
    }
    for (;;) {
        if (open == '{') {
            if (json_string_scan(j) != 0) return -1;
            json_ws(j);
            if (j->pos >= j->len || j->s[j->pos++] != ':') return -1;
            json_ws(j);
        }
        if (json_value_scan(j) != 0) return -1;
        json_ws(j);
        if (j->pos >= j->len) return -1;
        if (j->s[j->pos] == close) {
            j->pos++;
            j->depth--;
            return 0;
        }
        if (j->s[j->pos++] != ',') return -1;
        json_ws(j);
    }
}

static int json_value_scan(json_scan *j)
{
    json_ws(j);
    if (j->pos >= j->len) return -1;
    char c = j->s[j->pos];
    if (c == '"') return json_string_scan(j);
    if (c == '{') return json_container_scan(j, '{', '}');
    if (c == '[') return json_container_scan(j, '[', ']');
    if (c == '-' || isdigit((unsigned char)c)) return json_number_scan(j);
    const char *literal = c == 't' ? "true" : c == 'f' ? "false" : c == 'n' ? "null" : NULL;
    if (!literal) return -1;
    size_t n = strlen(literal);
    if (j->len - j->pos < n || memcmp(j->s + j->pos, literal, n) != 0) return -1;
    j->pos += n;
    return 0;
}

static int json_strict_n(const char *s, size_t len)
{
    json_scan j = {s, 0, len, 0};
    if (json_value_scan(&j) != 0) return 0;
    json_ws(&j);
    return j.pos == j.len;
}

static int json_strict(const char *s)
{
    return s && json_strict_n(s, strlen(s));
}

static int no_duplicate_keys(const fj_value *v)
{
    if (!v) return 0;
    if (v->type == FJ_OBJECT) {
        for (const fj_value *a = v->head; a; a = a->next) {
            for (const fj_value *b = a->next; b; b = b->next)
                if (a->key && b->key && strcmp(a->key, b->key) == 0) return 0;
            if (!no_duplicate_keys(a)) return 0;
        }
    } else if (v->type == FJ_ARRAY) {
        for (const fj_value *a = v->head; a; a = a->next)
            if (!no_duplicate_keys(a)) return 0;
    }
    return 1;
}

static int copy_string_field(const fj_value *object, const char *key, char **out)
{
    const fj_value *value = fjson_get(object, key);
    if (!value || value->type != FJ_STRING || !value->str[0]) return -1;
    *out = _strdup(value->str);
    return *out ? 0 : -2;
}

static int validate_arguments(const char *arguments, size_t length,
                              char *error, unsigned error_capacity)
{
    if (!arguments || !length || !json_strict_n(arguments, length)) {
        set_error(error, error_capacity, "tool arguments are not complete strict JSON");
        return -1;
    }
    char *copy = fstrndup(arguments, length);
    if (!copy) {
        set_error(error, error_capacity, "out of memory validating tool arguments");
        return -1;
    }
    fj_value *tree = fjson_parse(copy);
    free(copy);
    if (!tree || tree->type != FJ_OBJECT || !no_duplicate_keys(tree)) {
        fjson_free(tree);
        set_error(error, error_capacity, "tool arguments must be a duplicate-free JSON object");
        return -1;
    }
    fjson_free(tree);
    return 0;
}

static int valid_tool_call(const fr_tool_call *call, char *error, unsigned error_capacity)
{
    if (!call || !call->id || !call->id[0] || !call->name || !call->name[0]) {
        set_error(error, error_capacity, "assistant tool call requires non-empty id and name");
        return -1;
    }
    return validate_arguments(call->arguments, call->arguments_len, error, error_capacity);
}

static int append_tool_call(sb *body, const fr_tool_call *call)
{
    return putsb(body, "{\"id\":") || json_string(body, call->id) ||
           putsb(body, ",\"type\":\"function\",\"function\":{\"name\":") ||
           json_string(body, call->name) || putsb(body, ",\"arguments\":") ||
           json_string_n(body, call->arguments, call->arguments_len) ||
           putsb(body, "}}");
}

static int outstanding_add(char ***ids, size_t *count, size_t *capacity, const char *id)
{
    for (size_t i = 0; i < *count; i++)
        if ((*ids)[i] && strcmp((*ids)[i], id) == 0) return -1;
    if (*count == *capacity) {
        size_t cap = *capacity ? *capacity * 2 : 4;
        if (cap > SIZE_MAX / sizeof(char *)) return -1;
        char **grown = (char **)realloc(*ids, cap * sizeof(char *));
        if (!grown) return -1;
        for (size_t i = *capacity; i < cap; i++) grown[i] = NULL;
        *ids = grown;
        *capacity = cap;
    }
    (*ids)[*count] = _strdup(id);
    if (!(*ids)[*count]) return -1;
    (*count)++;
    return 0;
}

static int outstanding_remove(char **ids, size_t count, const char *id)
{
    for (size_t i = 0; i < count; i++) {
        if (ids[i] && strcmp(ids[i], id) == 0) {
            free(ids[i]);
            ids[i] = NULL;
            return 0;
        }
    }
    return -1;
}

int fr_encode_messages(const fr_message *messages, size_t count,
                       char **json, size_t *json_len,
                       char *error, unsigned error_capacity)
{
    if (json) *json = NULL;
    if (json_len) *json_len = 0;
    if (!messages || !count || !json || !json_len) {
        set_error(error, error_capacity, "messages, count, and output pointers are required");
        return -1;
    }

    sb body = {0};
    char **outstanding = NULL;
    size_t outstanding_count = 0, outstanding_capacity = 0;
    size_t outstanding_pending = 0;
    int rc = -1;
    if (putsb(&body, "[") != 0) goto oom;
    for (size_t i = 0; i < count; i++) {
        const fr_message *message = &messages[i];
        int is_assistant = message->role && strcmp(message->role, "assistant") == 0;
        int is_tool = message->role && strcmp(message->role, "tool") == 0;
        int is_user = message->role && strcmp(message->role, "user") == 0;
        int is_system = message->role && strcmp(message->role, "system") == 0;
        if (!is_assistant && !is_tool && !is_user && !is_system) {
            set_error(error, error_capacity, "unsupported or missing message role");
            goto done;
        }
        if (outstanding_pending && !is_tool) {
            set_error(error, error_capacity, "all assistant tool calls need tool responses before the next message");
            goto done;
        }
        if (is_tool) {
            if (!message->tool_call_id || !message->tool_call_id[0] || !message->content ||
                message->tool_calls || message->tool_call_count ||
                outstanding_remove(outstanding, outstanding_count, message->tool_call_id) != 0) {
                set_error(error, error_capacity, "tool message requires matching tool_call_id and string content");
                goto done;
            }
            outstanding_pending--;
        } else if (message->tool_call_id || (message->tool_call_count && !is_assistant) ||
                   (message->tool_call_count && !message->tool_calls)) {
            set_error(error, error_capacity, "tool_call_id is tool-only; tool_calls are assistant-only");
            goto done;
        }
        if (message->tool_call_count && !is_assistant) {
            set_error(error, error_capacity, "only assistant messages may contain tool calls");
            goto done;
        }
        if (i && putsb(&body, ",") != 0) goto oom;
        if (putsb(&body, "{\"role\":") != 0 || json_string(&body, message->role) != 0 ||
            putsb(&body, ",\"content\":") != 0 || json_string(&body, message->content) != 0)
            goto oom;
        if (is_tool) {
            if (putsb(&body, ",\"tool_call_id\":") != 0 ||
                json_string(&body, message->tool_call_id) != 0) goto oom;
        }
        if (message->tool_call_count) {
            if (outstanding_pending) {
                set_error(error, error_capacity, "nested assistant tool call sequence is invalid");
                goto done;
            }
            for (size_t j = 0; j < outstanding_count; j++) free(outstanding[j]);
            outstanding_count = 0;
            if (putsb(&body, ",\"tool_calls\":[") != 0) goto oom;
            for (size_t j = 0; j < message->tool_call_count; j++) {
                const fr_tool_call *call = &message->tool_calls[j];
                if (valid_tool_call(call, error, error_capacity) != 0) goto done;
                for (size_t k = 0; k < j; k++) {
                    if (strcmp(message->tool_calls[k].id, call->id) == 0) {
                        set_error(error, error_capacity, "duplicate assistant tool call id");
                        goto done;
                    }
                }
                if (j && putsb(&body, ",") != 0) goto oom;
                if (append_tool_call(&body, call) != 0) goto oom;
                if (outstanding_add(&outstanding, &outstanding_count,
                                    &outstanding_capacity, call->id) != 0) goto oom;
                outstanding_pending++;
            }
            if (putsb(&body, "]") != 0) goto oom;
        }
        if (putsb(&body, "}") != 0) goto oom;
    }
    if (outstanding_pending) {
        set_error(error, error_capacity, "conversation ends before all tool calls have responses");
        goto done;
    }
    if (putsb(&body, "]") != 0) goto oom;
    *json = body.p;
    *json_len = body.n;
    body.p = NULL;
    rc = 0;
    goto done;

oom:
    set_error(error, error_capacity, "out of memory serializing messages");
done:
    for (size_t i = 0; i < outstanding_capacity; i++) free(outstanding[i]);
    free(outstanding);
    free(body.p);
    return rc;
}

static int decode_tool_calls(const fj_value *message, fr_reply *reply,
                             char *error, unsigned error_capacity)
{
    const fj_value *calls = fjson_get(message, "tool_calls");
    if (!calls || calls->type != FJ_ARRAY || !fjson_count(calls)) {
        set_error(error, error_capacity, "finish_reason=tool_calls but tool_calls is missing or empty");
        return -1;
    }
    size_t count = fjson_count(calls);
    reply->tool_calls = (fr_tool_call *)calloc(count, sizeof(*reply->tool_calls));
    if (!reply->tool_calls) {
        set_error(error, error_capacity, "out of memory allocating tool calls");
        return -1;
    }
    reply->tool_call_count = count;
    size_t i = 0;
    for (const fj_value *item = calls->head; item; item = item->next, i++) {
        const fj_value *function = fjson_get(item, "function");
        const fj_value *type = fjson_get(item, "type");
        if (item->type != FJ_OBJECT || !type || type->type != FJ_STRING ||
            strcmp(type->str, "function") != 0 || !function || function->type != FJ_OBJECT) {
            set_error(error, error_capacity, "malformed tool call: expected type=function and function object");
            return -1;
        }
        fr_tool_call *call = &reply->tool_calls[i];
        int rc = copy_string_field(item, "id", &call->id);
        if (rc == 0) rc = copy_string_field(function, "name", &call->name);
        if (rc != 0) {
            set_error(error, error_capacity, rc == -2 ? "out of memory copying tool call" :
                      "malformed tool call: id and function.name must be non-empty strings");
            return -1;
        }
        const fj_value *args = fjson_get(function, "arguments");
        if (!args || args->type != FJ_STRING || !args->str[0] || !json_strict(args->str)) {
            set_error(error, error_capacity, "malformed tool call: function.arguments must contain complete JSON");
            return -1;
        }
        fj_value *arg_tree = fjson_parse(args->str);
        if (!arg_tree || arg_tree->type != FJ_OBJECT || !no_duplicate_keys(arg_tree)) {
            fjson_free(arg_tree);
            set_error(error, error_capacity, "malformed tool call: arguments must be a duplicate-free JSON object");
            return -1;
        }
        fjson_free(arg_tree);
        call->arguments = _strdup(args->str);
        if (!call->arguments) {
            set_error(error, error_capacity, "out of memory copying tool arguments");
            return -1;
        }
        call->arguments_len = strlen(call->arguments);
        for (size_t j = 0; j < i; j++) {
            if (strcmp(reply->tool_calls[j].id, call->id) == 0) {
                set_error(error, error_capacity, "duplicate tool call id");
                return -1;
            }
        }
    }
    return 0;
}

int fr_decode_response(const char *json, fr_reply *reply,
                       char *error, unsigned error_capacity)
{
    if (!json || !reply) {
        set_error(error, error_capacity, "null response");
        return -1;
    }
    fr_reply_free(reply);
    if (!json_strict(json)) {
        reply->stop = FR_STOP_ERROR;
        set_error(error, error_capacity, "malformed or partial JSON response");
        return -1;
    }
    fj_value *root = fjson_parse(json);
    if (!root || root->type != FJ_OBJECT || !no_duplicate_keys(root)) {
        fjson_free(root);
        reply->stop = FR_STOP_ERROR;
        set_error(error, error_capacity, "response JSON is not a duplicate-free object");
        return -1;
    }
    reply->raw_json = _strdup(json);
    if (!reply->raw_json) {
        fjson_free(root);
        reply->stop = FR_STOP_ERROR;
        set_error(error, error_capacity, "out of memory");
        return -1;
    }
    const fj_value *choices = fjson_get(root, "choices");
    const fj_value *choice = fjson_at(choices, 0);
    const fj_value *message = fjson_get(choice, "message");
    const fj_value *content = fjson_get(message, "content");
    const fj_value *finish = fjson_get(choice, "finish_reason");
    if (!choices || choices->type != FJ_ARRAY || !choice || choice->type != FJ_OBJECT ||
        !message || message->type != FJ_OBJECT || !finish || finish->type != FJ_STRING ||
        !finish->str[0] || (content && content->type != FJ_STRING && content->type != FJ_NULL)) {
        fjson_free(root);
        reply->stop = FR_STOP_ERROR;
        set_error(error, error_capacity, "response is missing valid choices[0].message or finish_reason");
        return -1;
    }
    reply->text = _strdup(content && content->type == FJ_STRING ? content->str : "");
    reply->finish_reason = _strdup(finish->str);
    if (!reply->text || !reply->finish_reason) {
        fjson_free(root);
        reply->stop = FR_STOP_ERROR;
        set_error(error, error_capacity, "out of memory copying response fields");
        return -1;
    }
    const fj_value *calls = fjson_get(message, "tool_calls");
    if (strcmp(finish->str, "tool_calls") == 0) {
        if (decode_tool_calls(message, reply, error, error_capacity) != 0) {
            fjson_free(root);
            free_tool_calls(reply);
            reply->stop = FR_STOP_ERROR;
            return -1;
        }
        reply->stop = FR_STOP_TOOL_CALL;
    } else if (calls && calls->type == FJ_ARRAY && fjson_count(calls) != 0) {
        fjson_free(root);
        free_tool_calls(reply);
        reply->stop = FR_STOP_ERROR;
        set_error(error, error_capacity, "tool_calls present but finish_reason is not tool_calls");
        return -1;
    } else if (calls && calls->type != FJ_ARRAY) {
        fjson_free(root);
        reply->stop = FR_STOP_ERROR;
        set_error(error, error_capacity, "message.tool_calls must be an array");
        return -1;
    }
    else if (strcmp(finish->str, "stop") == 0) reply->stop = FR_STOP_TURN;
    else if (strcmp(finish->str, "length") == 0) reply->stop = FR_STOP_LENGTH;
    else reply->stop = FR_STOP_OTHER;
    fjson_free(root);
    return 0;
}

static int is_loopback_url(const char *url)
{
    return url && strncmp(url, "http://127.0.0.1", 16) == 0 &&
           (url[16] == '\0' || url[16] == ':' || url[16] == '/');
}

static int build_body(const fr_config *config, const fr_message *messages,
                      unsigned count, sb *body, char *error, unsigned error_capacity)
{
    if (!isfinite(config->temperature)) {
        set_error(error, error_capacity, "temperature must be a finite JSON number");
        return -1;
    }
    if (config->tools_json && *config->tools_json) {
        if (!json_strict(config->tools_json)) {
            set_error(error, error_capacity, "tools_json is malformed or partial JSON");
            return -1;
        }
        fj_value *schemas = fjson_parse(config->tools_json);
        if (!schemas || schemas->type != FJ_ARRAY || !no_duplicate_keys(schemas)) {
            fjson_free(schemas);
            set_error(error, error_capacity, "tools_json must be a duplicate-free JSON array");
            return -1;
        }
        for (const fj_value *schema = schemas->head; schema; schema = schema->next) {
            const fj_value *type = fjson_get(schema, "type");
            const fj_value *function = fjson_get(schema, "function");
            if (schema->type != FJ_OBJECT || !type || type->type != FJ_STRING ||
                strcmp(type->str, "function") != 0 || !function || function->type != FJ_OBJECT) {
                fjson_free(schemas);
                set_error(error, error_capacity, "each tool schema must have type=function and a function object");
                return -1;
            }
        }
        fjson_free(schemas);
    }
    char *messages_json = NULL;
    size_t messages_len = 0;
    if (fr_encode_messages(messages, count, &messages_json, &messages_len,
                           error, error_capacity) != 0) return -1;
    if (putsb(body, "{\"model\":\"local\",\"messages\":") != 0 ||
        putn(body, messages_json, messages_len) != 0) {
        free(messages_json);
        return -1;
    }
    free(messages_json);
    if (putsb(body, ",\"temperature\":") != 0) return -1;
    char number[64];
    snprintf(number, sizeof(number), "%.6g", config->temperature);
    if (putsb(body, number) != 0) return -1;
    snprintf(number, sizeof(number), "%d", config->max_tokens ? config->max_tokens : -1);
    if (putsb(body, ",\"max_tokens\":") != 0 || putsb(body, number) != 0 ||
        putsb(body, ",\"stream\":false,\"cache_prompt\":false,"
                    "\"chat_template_kwargs\":{\"enable_thinking\":") != 0 ||
        putsb(body, config->thinking ? "true" : "false") != 0 || putsb(body, "}") != 0)
        return -1;
    if (config->tools_json && *config->tools_json) {
        if (putsb(body, ",\"tools\":") != 0 || putsb(body, config->tools_json) != 0 ||
            putsb(body, ",\"tool_choice\":\"auto\"") != 0) return -1;
    }
    if (putsb(body, "}") != 0) return -1;
    if (!json_strict(body->p)) {
        set_error(error, error_capacity, "serialized request body is not strict JSON");
        return -1;
    }
    return 0;
}

int fr_generate(const fr_config *config, const fr_message *messages,
                unsigned count, fr_reply *reply,
                char *error, unsigned error_capacity)
{
    if (!config || !reply || !messages || !count || !is_loopback_url(config->base_url)) {
        set_error(error, error_capacity, "endpoint must be http://127.0.0.1[:port]");
        return -1;
    }
    sb body = {0};
    if (build_body(config, messages, count, &body, error, error_capacity) != 0) {
        free(body.p);
        fr_reply_free(reply);
        reply->stop = FR_STOP_ERROR;
        if (!error || !error_capacity || !error[0])
            set_error(error, error_capacity, "request serialization failed");
        return -1;
    }
    fr_reply_free(reply);
    if (config->cancel && *config->cancel) {
        free(body.p);
        reply->stop = FR_STOP_CANCELLED;
        set_error(error, error_capacity, "cancelled before request");
        return -1;
    }
    INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT;
    const char *colon = strchr(config->base_url + 16, ':');
    if (colon) {
        char *end = NULL;
        unsigned long value = strtoul(colon + 1, &end, 10);
        if (value < 1 || value > 65535 || (end && *end && *end != '/')) {
            free(body.p);
            reply->stop = FR_STOP_ERROR;
            set_error(error, error_capacity, "invalid loopback port");
            return -1;
        }
        port = (INTERNET_PORT)value;
    }

    HINTERNET session = WinHttpOpen(L"FORGE-runtime-adapter/1",
        WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        free(body.p);
        reply->stop = FR_STOP_ERROR;
        set_error(error, error_capacity, "WinHttpOpen failed");
        return -1;
    }
    DWORD timeout = config->timeout_ms ? config->timeout_ms : 120000;
    WinHttpSetTimeouts(session, (int)timeout, (int)timeout, (int)timeout, (int)timeout);
    HINTERNET connection = WinHttpConnect(session, L"127.0.0.1", port, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"POST",
        L"/v1/chat/completions", NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, 0) : NULL;
    if (!request) {
        DWORD code = GetLastError();
        if (connection) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        free(body.p);
        reply->stop = FR_STOP_ERROR;
        snprintf(error, error_capacity, "WinHTTP request initialization failed (%lu)",
                 (unsigned long)code);
        return -1;
    }

    cancel_watch watch = { request, (volatile LONG *)config->cancel, 0 };
    HANDLE cancel_thread = NULL;
    if (config->cancel) {
        cancel_thread = CreateThread(NULL, 0, watch_request_cancel, &watch, 0, NULL);
        if (!cancel_thread) {
            WinHttpCloseHandle(request); WinHttpCloseHandle(connection); WinHttpCloseHandle(session);
            free(body.p); reply->stop = FR_STOP_ERROR;
            set_error(error, error_capacity, "could not start cancellation watcher"); return -1;
        }
    }

    LPCWSTR headers = L"Content-Type: application/json\r\nAccept: application/json\r\n";
    BOOL ok = WinHttpSendRequest(request, headers, (DWORD)-1, body.p,
                                 (DWORD)body.n, (DWORD)body.n, 0);
    free(body.p);
    if (ok && !(config->cancel && *config->cancel))
        ok = WinHttpReceiveResponse(request, NULL);
    if (!ok) {
        DWORD code = GetLastError();
        reply->stop = config->cancel && *config->cancel ? FR_STOP_CANCELLED : FR_STOP_ERROR;
        snprintf(error, error_capacity, "WinHTTP send/receive failed (%lu)",
                 (unsigned long)code);
        goto done;
    }

    DWORD status = 0, status_size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
    reply->http_status = status;
    sb response = {0};
    for (;;) {
        if (config->cancel && *config->cancel) {
            reply->stop = FR_STOP_CANCELLED;
            free(response.p);
            set_error(error, error_capacity, "cancelled during response read");
            goto done;
        }
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            free(response.p);
            reply->stop = FR_STOP_ERROR;
            set_error(error, error_capacity, "response availability query failed");
            goto done;
        }
        if (!available) break;
        if (available > 64u * 1024u * 1024u - response.n ||
            reserve(&response, available) != 0 ||
            !WinHttpReadData(request, response.p + response.n, available, &available)) {
            free(response.p);
            reply->stop = FR_STOP_ERROR;
            set_error(error, error_capacity, "response read failed or exceeded 64 MiB limit");
            goto done;
        }
        response.n += available;
        response.p[response.n] = '\0';
    }
    if (!response.p) response.p = _strdup("");
    if (status < 200 || status >= 300) {
        reply->raw_json = response.p;
        reply->stop = FR_STOP_ERROR;
        set_error(error, error_capacity, "HTTP error response (raw_json retained)");
        goto done;
    }
    if (!response.p || fr_decode_response(response.p, reply, error, error_capacity) != 0) {
        reply->stop = FR_STOP_ERROR;
        free(response.p);
        goto done;
    }
    free(response.p);

done:
    InterlockedExchange(&watch.done, 1);
    { HINTERNET owned = (HINTERNET)InterlockedExchangePointer(&watch.request, NULL);
      if (owned) WinHttpCloseHandle(owned); }
    if (cancel_thread) { WaitForSingleObject(cancel_thread, INFINITE); CloseHandle(cancel_thread); }
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return reply->stop == FR_STOP_ERROR || reply->stop == FR_STOP_CANCELLED ? -1 : 0;
}
