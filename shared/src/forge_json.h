/* forge_json.h -- minimal JSON (parse + build + write), C11, libc only.
 *
 * Scope: FORGE only needs JSON for config files, CLI flags and audit dumps.
 * That is: parse a document, walk it (objects, arrays, strings, numbers,
 * booleans, null), and serialize trees back out. No incremental streaming,
 * no NaN/Infinity, no bignum encoding beyond double.
 */
#ifndef FORGE_JSON_H
#define FORGE_JSON_H

#include "forge_util.h"

typedef enum fj_type {
    FJ_NULL = 0,
    FJ_BOOL,
    FJ_NUMBER,
    FJ_STRING,
    FJ_ARRAY,
    FJ_OBJECT
} fj_type;

typedef struct fj_value {
    fj_type type;
    double  num;          /* FJ_NUMBER */
    int     bval;         /* FJ_BOOL   */
    char   *str;          /* FJ_STRING */
    char   *key;          /* object member name (obj children only) */
    struct fj_value *head;   /* first child of array/object */
    struct fj_value *next;   /* sibling                     */
} fj_value;

/* Parsing. NULL on syntax error. Caller frees with fjson_free(). */
fj_value *fjson_parse(const char *text);

/* Read access. All return a safe default on type mismatch. */
const fj_value *fjson_get(const fj_value *obj, const char *key);
const fj_value *fjson_at(const fj_value *arr, size_t i);
size_t          fjson_count(const fj_value *arr_or_obj);
double          fjson_num(const fj_value *v);
const char     *fjson_str(const fj_value *v);
int             fjson_bool(const fj_value *v);

/* Building. fjson_object_set/fjson_array_push take ownership of 'val'. */
fj_value *fjson_object_new(void);
fj_value *fjson_array_new(void);
fj_value *fjson_string_new(const char *s);
fj_value *fjson_number_new(double n);
fj_value *fjson_bool_new(int b);
fj_value *fjson_null_new(void);
int       fjson_object_set(fj_value *obj, const char *key, fj_value *val);
int       fjson_array_push(fj_value *arr, fj_value *val);

/* Writer. Serializes the tree into 'out'. 0 on success, -1 on OOM.
 * Strings are escaped per RFC 8259 (\n \t \" \\ \uXXXX ...). */
int fjson_write(const fj_value *v, fbuf *out);

void fjson_free(fj_value *v);

#endif /* FORGE_JSON_H */
