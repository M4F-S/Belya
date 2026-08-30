#include "minijson.h"
#include <ctype.h>

static const char *skip_ws(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static JsonValue *parse_value(const char **src);

static char *parse_string_raw(const char **src) {
    if (**src != '\"') return NULL;
    (*src)++;
    DynString ds = dyn_str_new();
    while (**src) {
        char c = **src;
        if (c == '\"') {
            (*src)++;
            return ds.data;
        }
        if (c == '\\') {
            (*src)++;
            c = **src;
            if (!c) break;
            switch (c) {
                case '\"': dyn_str_append(&ds, "\""); break;
                case '\\': dyn_str_append(&ds, "\\"); break;
                case '/':  dyn_str_append(&ds, "/");  break;
                case 'b':  dyn_str_append(&ds, "\b"); break;
                case 'f':  dyn_str_append(&ds, "\f"); break;
                case 'n':  dyn_str_append(&ds, "\n"); break;
                case 'r':  dyn_str_append(&ds, "\r"); break;
                case 't':  dyn_str_append(&ds, "\t"); break;
                case 'u': {
                    unsigned int hex = 0;
                    int valid = 1;
                    for (int h = 0; h < 4; h++) {
                        char hc = (*src)[1 + h];
                        if (!hc) { valid = 0; break; }
                        hex <<= 4;
                        if (hc >= '0' && hc <= '9') hex |= (hc - '0');
                        else if (hc >= 'a' && hc <= 'f') hex |= (hc - 'a' + 10);
                        else if (hc >= 'A' && hc <= 'F') hex |= (hc - 'A' + 10);
                        else { valid = 0; break; }
                    }
                    if (valid) {
                        *src += 4;
                        if (hex <= 0x7F) {
                            char buf[2] = {(char)hex, '\0'};
                            dyn_str_append(&ds, buf);
                        } else if (hex <= 0x7FF) {
                            char buf[3] = {
                                (char)(0xC0 | ((hex >> 6) & 0x1F)),
                                (char)(0x80 | (hex & 0x3F)),
                                '\0'
                            };
                            dyn_str_append(&ds, buf);
                        } else {
                            char buf[4] = {
                                (char)(0xE0 | ((hex >> 12) & 0x0F)),
                                (char)(0x80 | ((hex >> 6) & 0x3F)),
                                (char)(0x80 | (hex & 0x3F)),
                                '\0'
                            };
                            dyn_str_append(&ds, buf);
                        }
                    } else {
                        dyn_str_append(&ds, "\\u");
                    }
                    break;
                }
                default: {
                    char tmp[2] = {c, '\0'};
                    dyn_str_append(&ds, tmp);
                    break;
                }
            }
        } else {
            char tmp[2] = {c, '\0'};
            dyn_str_append(&ds, tmp);
        }
        (*src)++;
    }
    dyn_str_free(&ds);
    return NULL;
}

static JsonValue *parse_object(const char **src) {
    JsonValue *obj = json_create_object();
    (*src)++; // Skip '{'
    *src = skip_ws(*src);
    if (**src == '}') {
        (*src)++;
        return obj;
    }
    while (**src) {
        *src = skip_ws(*src);
        if (**src != '\"') goto error;
        char *key = parse_string_raw(src);
        if (!key) goto error;
        *src = skip_ws(*src);
        if (**src != ':') { free(key); goto error; }
        (*src)++; // Skip ':'
        *src = skip_ws(*src);
        JsonValue *val = parse_value(src);
        if (!val) { free(key); goto error; }
        json_obj_add(obj, key, val);
        free(key);
        *src = skip_ws(*src);
        if (**src == ',') {
            (*src)++;
            continue;
        }
        if (**src == '}') {
            (*src)++;
            return obj;
        }
        break;
    }
error:
    json_free(obj);
    return NULL;
}

static JsonValue *parse_array(const char **src) {
    JsonValue *arr = json_create_array();
    (*src)++; // Skip '['
    *src = skip_ws(*src);
    if (**src == ']') {
        (*src)++;
        return arr;
    }
    while (**src) {
        *src = skip_ws(*src);
        JsonValue *val = parse_value(src);
        if (!val) goto error;
        json_arr_add(arr, val);
        *src = skip_ws(*src);
        if (**src == ',') {
            (*src)++;
            continue;
        }
        if (**src == ']') {
            (*src)++;
            return arr;
        }
        break;
    }
error:
    json_free(arr);
    return NULL;
}

static JsonValue *parse_number(const char **src) {
    char *end;
    double val = strtod(*src, &end);
    if (end == *src) return NULL;
    *src = end;
    return json_create_number(val);
}

static JsonValue *parse_value(const char **src) {
    *src = skip_ws(*src);
    if (!**src) return NULL;
    if (**src == '{') return parse_object(src);
    if (**src == '[') return parse_array(src);
    if (**src == '\"') {
        char *s = parse_string_raw(src);
        if (!s) return NULL;
        JsonValue *val = json_create_string(s);
        free(s);
        return val;
    }
    if (strncmp(*src, "true", 4) == 0) { *src += 4; return json_create_bool(true); }
    if (strncmp(*src, "false", 5) == 0) { *src += 5; return json_create_bool(false); }
    if (strncmp(*src, "null", 4) == 0) { *src += 4; return json_create_null(); }
    return parse_number(src);
}

JsonValue *json_parse(const char *src) {
    if (!src) return NULL;
    const char *p = src;
    return parse_value(&p);
}

JsonValue *json_create_object(void) {
    JsonValue *v = calloc(1, sizeof(JsonValue));
    v->type = JSON_OBJECT;
    return v;
}

JsonValue *json_create_array(void) {
    JsonValue *v = calloc(1, sizeof(JsonValue));
    v->type = JSON_ARRAY;
    return v;
}

JsonValue *json_create_string(const char *val) {
    JsonValue *v = calloc(1, sizeof(JsonValue));
    v->type = JSON_STRING;
    v->u.string = strdup(val ? val : "");
    return v;
}

JsonValue *json_create_number(double val) {
    JsonValue *v = calloc(1, sizeof(JsonValue));
    v->type = JSON_NUMBER;
    v->u.number = val;
    return v;
}

JsonValue *json_create_bool(bool val) {
    JsonValue *v = calloc(1, sizeof(JsonValue));
    v->type = JSON_BOOL;
    v->u.boolean = val;
    return v;
}

JsonValue *json_create_null(void) {
    JsonValue *v = calloc(1, sizeof(JsonValue));
    v->type = JSON_NULL;
    return v;
}

void json_obj_add(JsonValue *obj, const char *key, JsonValue *val) {
    if (!obj || obj->type != JSON_OBJECT || !key || !val) return;
    if (obj->u.object.count >= obj->u.object.cap) {
        size_t new_cap = obj->u.object.cap == 0 ? 8 : obj->u.object.cap * 2;
        JsonMember *new_members = realloc(obj->u.object.members, sizeof(JsonMember) * new_cap);
        if (!new_members) {
            fprintf(stderr, "[Fatal] Out of memory in json_obj_add\n");
            abort();
        }
        obj->u.object.members = new_members;
        obj->u.object.cap = new_cap;
    }
    obj->u.object.members[obj->u.object.count].key = strdup(key);
    obj->u.object.members[obj->u.object.count].value = val;
    obj->u.object.count++;
}

void json_arr_add(JsonValue *arr, JsonValue *val) {
    if (!arr || arr->type != JSON_ARRAY || !val) return;
    if (arr->u.array.count >= arr->u.array.cap) {
        size_t new_cap = arr->u.array.cap == 0 ? 8 : arr->u.array.cap * 2;
        JsonValue **new_items = realloc(arr->u.array.items, sizeof(JsonValue*) * new_cap);
        if (!new_items) {
            fprintf(stderr, "[Fatal] Out of memory in json_arr_add\n");
            abort();
        }
        arr->u.array.items = new_items;
        arr->u.array.cap = new_cap;
    }
    arr->u.array.items[arr->u.array.count++] = val;
}

JsonValue *json_obj_get(const JsonValue *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT || !key) return NULL;
    for (size_t i = 0; i < obj->u.object.count; i++) {
        if (strcmp(obj->u.object.members[i].key, key) == 0) {
            return obj->u.object.members[i].value;
        }
    }
    return NULL;
}

const char *json_obj_get_str(const JsonValue *obj, const char *key) {
    JsonValue *v = json_obj_get(obj, key);
    if (v && v->type == JSON_STRING) return v->u.string;
    return NULL;
}

double json_obj_get_num(const JsonValue *obj, const char *key, double def) {
    JsonValue *v = json_obj_get(obj, key);
    if (v && v->type == JSON_NUMBER) return v->u.number;
    return def;
}

bool json_obj_get_bool(const JsonValue *obj, const char *key, bool def) {
    JsonValue *v = json_obj_get(obj, key);
    if (v && v->type == JSON_BOOL) return v->u.boolean;
    return def;
}

static void serialize_internal(const JsonValue *val, DynString *ds) {
    if (!val) { dyn_str_append(ds, "null"); return; }
    switch (val->type) {
        case JSON_NULL: dyn_str_append(ds, "null"); break;
        case JSON_BOOL: dyn_str_append(ds, val->u.boolean ? "true" : "false"); break;
        case JSON_NUMBER: {
            char buf[64];
            snprintf(buf, sizeof(buf), "%f", val->u.number);
            char *p = buf + strlen(buf) - 1;
            while (*p == '0' && p > buf) *p-- = '\0';
            if (*p == '.') *p = '\0';
            dyn_str_append(ds, buf);
            break;
        }
        case JSON_STRING:
            dyn_str_append(ds, "\"");
            dyn_str_append_escaped(ds, val->u.string);
            dyn_str_append(ds, "\"");
            break;
        case JSON_ARRAY:
            dyn_str_append(ds, "[");
            for (size_t i = 0; i < val->u.array.count; i++) {
                if (i > 0) dyn_str_append(ds, ",");
                serialize_internal(val->u.array.items[i], ds);
            }
            dyn_str_append(ds, "]");
            break;
        case JSON_OBJECT:
            dyn_str_append(ds, "{");
            for (size_t i = 0; i < val->u.object.count; i++) {
                if (i > 0) dyn_str_append(ds, ",");
                dyn_str_append(ds, "\"");
                dyn_str_append_escaped(ds, val->u.object.members[i].key);
                dyn_str_append(ds, "\":");
                serialize_internal(val->u.object.members[i].value, ds);
            }
            dyn_str_append(ds, "}");
            break;
    }
}

char *json_serialize(const JsonValue *val) {
    DynString ds = dyn_str_new();
    serialize_internal(val, &ds);
    return ds.data;
}

void json_free(JsonValue *v) {
    if (!v) return;
    switch (v->type) {
        case JSON_STRING: free(v->u.string); break;
        case JSON_ARRAY:
            for (size_t i = 0; i < v->u.array.count; i++) json_free(v->u.array.items[i]);
            free(v->u.array.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < v->u.object.count; i++) {
                free(v->u.object.members[i].key);
                json_free(v->u.object.members[i].value);
            }
            free(v->u.object.members);
            break;
        default: break;
    }
    free(v);
}
