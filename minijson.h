#ifndef MINIJSON_H
#define MINIJSON_H

#include "common.h"

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;

typedef struct {
    char *key;
    JsonValue *value;
} JsonMember;

struct JsonValue {
    JsonType type;
    union {
        bool boolean;
        double number;
        char *string;
        struct {
            JsonValue **items;
            size_t count;
            size_t cap;
        } array;
        struct {
            JsonMember *members;
            size_t count;
            size_t cap;
        } object;
    } u;
};

JsonValue *json_parse(const char *src);
void json_free(JsonValue *v);

JsonValue *json_obj_get(const JsonValue *obj, const char *key);
const char *json_obj_get_str(const JsonValue *obj, const char *key);
double json_obj_get_num(const JsonValue *obj, const char *key, double def);
bool json_obj_get_bool(const JsonValue *obj, const char *key, bool def);

JsonValue *json_create_object(void);
JsonValue *json_create_array(void);
JsonValue *json_create_string(const char *val);
JsonValue *json_create_number(double val);
JsonValue *json_create_bool(bool val);
JsonValue *json_create_null(void);

void json_obj_add(JsonValue *obj, const char *key, JsonValue *val);
void json_arr_add(JsonValue *arr, JsonValue *val);
char *json_serialize(const JsonValue *val);

#endif
