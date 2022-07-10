#include "xscjson.h"
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#ifndef JSON_PARSE_STACK_INIT_SIZE
#define JSON_PARSE_STACK_INIT_SIZE 256
#endif

#ifndef JSON_STRINGIFY_STACK_INIT_SIZE
#define JSON_STRINGIFY_STACK_INIT_SIZE 256
#endif

#define EXPECT(c, ch) do { assert(*c->json == (ch)); c->json ++;} while(0)
#define ISDIGIT(ch) ((ch) >= '0' && (ch) <= '9')
#define ISDIGIT_1TO9(ch) ((ch) >= '1' && (ch) <= '9')
#define PUTC(c, ch) do { *(char*)json_context_push(c, sizeof(char)) = (ch); } while(0)
#define PUTS(c, s, len) memcpy(json_context_push(c, len), s, len)

typedef struct {
    const char* json;
    char* stack;
    size_t size, top;
} json_context;

static void* json_context_push(json_context* c, size_t size) {
    assert(size > 0);
    if (c->top + size >= c->size) {
        if (c->size == 0) {
            c->size = JSON_PARSE_STACK_INIT_SIZE;
        }
        while (c->top + size >= c->size) {
            c->size += c->size >> 1;
        }
        c->stack = (char*)realloc(c->stack, c->size);
    }
    void* ret = c->stack + c->top;
    c->top += size;
    return ret;
}
static void* json_context_pop(json_context* c, size_t size) {
    assert(c->top >= size);
    return c->stack + (c->top -= size);
}

static void json_parse_whitespace(json_context* c) {
    const char* p = c->json;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        ++ p;
    }
    c->json = p;
}
static int json_parse_literal(json_context* c, json_value* v, const char* literal, json_type type) {
    EXPECT(c, literal[0]);
    int i = 1;
    while (literal[i] != '\0') {
        if (literal[i] != *c->json) {
            return JSON_PARSE_INVALID_VALUE;
        } else {
            c->json ++;
            i ++;
        }
    }
    v->type = type;
    return JSON_PARSE_OK;
}
static int json_parse_number(json_context* c, json_value* v) {
    const char* p = c->json;
    if (*p == '-') {
        p ++;
    }
    if (*p == '0') {
        p ++;
    } else {
        if (!ISDIGIT_1TO9(*p)) {
            return JSON_PARSE_INVALID_VALUE;
        }
        for (p ++; ISDIGIT(*p); p ++);
    }
    if (*p == '.') {
        p ++;
        if (!ISDIGIT(*p)) {
            return JSON_PARSE_INVALID_VALUE;
        }
        for (p ++; ISDIGIT(*p); p ++);
    }
    if (*p == 'e' || *p == 'E') {
        p ++;
        if (*p == '+' || *p == '-') {
            p ++;
        }
        if (!ISDIGIT(*p)) {
            return JSON_PARSE_INVALID_VALUE;
        }
        for (p ++; ISDIGIT(*p); p ++);
    }
    errno = 0;
    v->u.n = strtod(c->json, NULL);
    if (errno == ERANGE && (v->u.n == HUGE_VAL || v->u.n == -HUGE_VAL)) {
        return JSON_PARSE_NUMBER_TOO_BIG;
    }
    v->type = JSON_NUMBER;
    c->json = p;
    return JSON_PARSE_OK;
}
static const char* json_parse_hex4(const char* p, unsigned* u) {
    *u = 0;
    for (int i = 0; i < 4; i++) {
        char ch = *p++;
        *u <<= 4;
        if      (ch >= '0' && ch <= '9')  *u |= ch - '0';
        else if (ch >= 'A' && ch <= 'F')  *u |= ch - ('A' - 10);
        else if (ch >= 'a' && ch <= 'f')  *u |= ch - ('a' - 10);
        else return NULL;
    }
    return p;
}
static void json_encode_utf8(json_context* c, unsigned u) {
    if (u < 0x0080) {
        PUTC(c, 0x00 | (u & 0x7f));
    } else if (u >= 0x0080 && u < 0x0800) {
        PUTC(c, 0xc0 | ((u >> 6 ) & 0x1f));
        PUTC(c, 0x80 | ((u      ) & 0x3f));
    } else if (u >= 0x0800 && u < 0x10000) {
        PUTC(c, 0xe0 | ((u >> 12) & 0xff));
        PUTC(c, 0x80 | ((u >> 6 ) & 0x3f));
        PUTC(c, 0x80 | ((u      ) & 0x3f));
    } else if (u >= 0x10000 && u <= 0x10ffff) {
        PUTC(c, 0xf0 | ((u >> 18) & 0x07));
        PUTC(c, 0x80 | ((u >> 12) & 0x3f));
        PUTC(c, 0x80 | ((u >> 6 ) & 0x3f));
        PUTC(c, 0x80 | ((u      ) & 0x3f));
    }
}
#define STRING_ERROR(ret) do { c->top = head; return ret; } while(0)
static int json_parse_string_raw(json_context* c, char** str, size_t* len) {
    size_t head = c->top;
    EXPECT(c, '\"');
    const char* p = c->json;
    for (;;) {
        char ch = *p ++;
        switch (ch) {
            case '\"': {
                *len = c->top - head; 
                *str = json_context_pop(c, *len);
                c->json = p;
                return JSON_PARSE_OK;
            }
            case '\0': STRING_ERROR(JSON_PARSE_MISS_QUOTATION_MARK);
            case '\\': {
                switch (*p ++) {
                    case '\"': PUTC(c, '\"'); break;
                    case '\\': PUTC(c, '\\'); break;
                    case '/':  PUTC(c, '/');  break;
                    case 'b':  PUTC(c, '\b'); break;
                    case 'f':  PUTC(c, '\f'); break;
                    case 'n':  PUTC(c, '\n'); break;
                    case 'r':  PUTC(c, '\r'); break;
                    case 't':  PUTC(c, '\t'); break;
                    case 'u': {
                        unsigned u;
                        if (!(p = json_parse_hex4(p, &u))) {
                            STRING_ERROR(JSON_PARSE_INVALID_UNICODE_HEX);
                        }                       
                        if (u >= 0xd800 && u <= 0xdbff) {
                            if (*p ++ != '\\' || *p ++ != 'u') {
                                STRING_ERROR(JSON_PARSE_INVALID_UNICODE_SURROGATE);
                            }
                            unsigned lowu;
                            if (!(p = json_parse_hex4(p, &lowu))) {                              
                                STRING_ERROR(JSON_PARSE_INVALID_UNICODE_HEX);
                            }
                            if (!(lowu >= 0xdc00 && lowu <= 0xdfff)) {                                
                                STRING_ERROR(JSON_PARSE_INVALID_UNICODE_SURROGATE);
                            }
                            u = 0x10000 + (u - 0xd800) * 0x400 + (lowu - 0xdc00);
                        }
                        json_encode_utf8(c, u);
                        break;
                    }
                    default: STRING_ERROR(JSON_PARSE_INVALID_STRING_ESCAPE);
                }
                break;
            }
            default: { 
                if ((unsigned char)ch < 0x20) {
                    STRING_ERROR(JSON_PARSE_INVALID_STRING_CHAR);
                }
                PUTC(c, ch);
            }
        }
    }
}
static int json_parse_string(json_context* c, json_value* v) {
    char* s;
    size_t len;
    int ret = json_parse_string_raw(c, &s, &len);
    if (ret == JSON_PARSE_OK) {
        json_set_string(v, s, len);
    }
    return ret;
}
static int json_parse_value(json_context* c, json_value* v);
static int json_parse_array(json_context* c, json_value* v) {
    EXPECT(c, '[');
    json_parse_whitespace(c);
    if (*c->json == ']') {
        c->json ++;
        json_set_array(v, 0);
        return JSON_PARSE_OK;
    }
    int ret;
    size_t size = 0;
    for (;;) {
        json_value e;
        json_init(&e);
        ret = json_parse_value(c, &e);
        if (ret != JSON_PARSE_OK) {
            break;
        }
        memcpy(json_context_push(c, sizeof(json_value)), &e, sizeof(json_value));
        size ++;
        json_parse_whitespace(c);
        if (*c->json == ',') {
            c->json ++;
            json_parse_whitespace(c);
        } else if (*c->json == ']') {
            c->json ++;
            json_set_array(v, size);
            memcpy(v->u.a.e, json_context_pop(c, size * sizeof(json_value)), size * sizeof(json_value));
            v->u.a.size = size;
            return JSON_PARSE_OK;
        } else {
            ret = JSON_PARSE_MISS_COMMA_OR_SQUARE_BRACKET;
            break;
        }
    }
    for (int i = 0; i < size; i ++) {
        json_free((json_value*)json_context_pop(c, sizeof(json_value)));
    }
    return ret;
}
static int json_parse_object(json_context* c, json_value* v) {
    EXPECT(c, '{');
    json_parse_whitespace(c);
    if (*c->json == '}') {
        c->json ++;
        json_set_object(v, 0);
        return JSON_PARSE_OK;
    }
    int ret;
    size_t size = 0;
    json_member m;
    m.k = NULL;
    for (;;) {
        json_init(&m.v);
        char* str;
        if (*c->json != '\"') {
            ret = JSON_PARSE_MISS_KEY;
            break;
        }
        ret = json_parse_string_raw(c, &str, &m.klen);
        if (ret != JSON_PARSE_OK) {
            break;
        }
        memcpy(m.k = (char*)malloc(m.klen + 1), str, m.klen);
        m.k[m.klen] = '\0';
        json_parse_whitespace(c);
        if (*c->json != ':') {
            ret = JSON_PARSE_MISS_COLON;
            break;
        }
        c->json ++;
        json_parse_whitespace(c);
        ret = json_parse_value(c, &m.v);
        if (ret != JSON_PARSE_OK) {
            break;
        }
        size ++;
        memcpy(json_context_push(c, sizeof(json_member)), &m, sizeof(json_member));
        m.k = NULL;

        json_parse_whitespace(c);
        if (*c->json == ',') {
            c->json ++;
            json_parse_whitespace(c);
        } else if (*c->json == '}') {
            c->json ++;
            json_set_object(v, size);
            memcpy(v->u.o.m, json_context_pop(c, size * sizeof(json_member)), size * sizeof(json_member));
            v->u.o.size = size;
            return JSON_PARSE_OK;
        } else {
            ret = JSON_PARSE_MISS_COMMA_OR_CURLY_BRACKET;
            break;
        }
    }
    free(m.k);
    for (int i = 0; i < size; i ++) {
        json_member* m = (json_member*)json_context_pop(c, sizeof(json_member));
        free(m->k);
        json_free(&m->v);
    }
    v->type = JSON_NULL;
    return ret;
}
static int json_parse_value(json_context* c, json_value* v) {
    switch (*c->json) {
        case 'n': return json_parse_literal(c, v, "null", JSON_NULL);
        case 't': return json_parse_literal(c, v, "true", JSON_TRUE);
        case 'f': return json_parse_literal(c, v, "false", JSON_FALSE);
        case '\0': return JSON_PARSE_EXPECT_VALUE;
        case '\"': return json_parse_string(c, v);
        case '[': return json_parse_array(c, v);
        case '{': return json_parse_object(c, v);
        default: return json_parse_number(c, v);
    }
}
int json_parse(json_value* v, const char* json) {
    assert(v != NULL);
    json_context c;
    c.json = json;
    c.stack = NULL;
    c.size = c.top = 0;
    json_init(v);
    json_parse_whitespace(&c);
    int ret = json_parse_value(&c, v);
    if (ret == JSON_PARSE_OK) {
        json_parse_whitespace(&c);
        if (*c.json != '\0') {
            v->type = JSON_NULL;
            return JSON_PARSE_ROOT_NOT_SINGULAR;
        }
    }
    assert(c.top == 0);
    free(c.stack);
    return ret;
}

void json_free(json_value* v) {
    assert(v != NULL);
    switch (v->type) {
        case JSON_STRING: {
            free(v->u.s.s);
            break;
        }
        case JSON_ARRAY: {
            for (int i = 0; i < v->u.a.size; i ++) {
                json_free(&v->u.a.e[i]);
            }
            free(v->u.a.e);
            break;
        }
        case JSON_OBJECT: {
            for (int i = 0; i < v->u.o.size; i ++) {
                free(v->u.o.m[i].k);
                json_free(&v->u.o.m[i].v);
            }
            free(v->u.o.m);
            break;
        }
        default: {
            break;
        }
    }
    v->type = JSON_NULL;
}



static void json_stringify_string(json_context* c, const char* s, size_t len) {
    static const char hex_digits[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
    assert(s != NULL);
    size_t size;
    char* head, * p;
    p = head = json_context_push(c, size = len * 6 + 2);
    *p ++ = '\"';
    for (int i = 0; i < len; i ++) {
        unsigned char ch = (unsigned char)s[i];
        switch (ch) {
            case '\"': *p ++ = '\\'; *p ++ = '\"'; break;
            case '\\': *p ++ = '\\'; *p ++ = '\\'; break;
            case '\b': *p ++ = '\\'; *p ++ = 'b';  break;
            case '\f': *p ++ = '\\'; *p ++ = 'f';  break;
            case '\n': *p ++ = '\\'; *p ++ = 'n';  break;
            case '\r': *p ++ = '\\'; *p ++ = 'r';  break;
            case '\t': *p ++ = '\\'; *p ++ = 't';  break;
            default: {
                if (ch < 0x20) {
                    *p++ = '\\'; *p++ = 'u'; *p++ = '0'; *p++ = '0';
                    *p++ = hex_digits[ch >> 4];
                    *p++ = hex_digits[ch & 15];
                } else {
                    *p++ = s[i];
                }
            }
        }
    }
    *p ++ = '\"';
    c->top -= size - (p - head);
}
static void json_stringify_value(json_context* c, const json_value* v) {
    switch (v->type) {
        case JSON_NULL: PUTS(c, "null", 4); break;
        case JSON_TRUE: PUTS(c, "true", 4); break;
        case JSON_FALSE: PUTS(c, "false", 5); break;
        case JSON_NUMBER: c->top -= 32 - sprintf(json_context_push(c, 32), "%.17g", v->u.n); break; // sprintf返回字节数
        case JSON_STRING: json_stringify_string(c, v->u.s.s, v->u.s.len); break;
        case JSON_ARRAY: {
            PUTC(c, '[');
            for (int i = 0; i < v->u.a.size; i ++) {
                if (i > 0) {
                    PUTC(c, ',');
                }
                json_stringify_value(c, &v->u.a.e[i]);
            }
            PUTC(c, ']');
            break;
        }
        case JSON_OBJECT: {
            PUTC(c, '{');
            for (int i = 0; i < v->u.o.size; i ++) {
                if (i > 0) {
                    PUTC(c, ',');
                }
                json_stringify_string(c, v->u.o.m[i].k, v->u.o.m[i].klen);
                PUTC(c, ':');
                json_stringify_value(c, &v->u.o.m[i].v);
            }
            PUTC(c, '}');
            break;
        }
        default: assert(0 && "invalid type");
    }
}
char* json_stringify(const json_value* v, size_t* length) {
    assert(v != NULL);
    json_context c;
    c.stack = (char*)malloc(c.size = JSON_STRINGIFY_STACK_INIT_SIZE);
    c.top = 0;
    json_stringify_value(&c, v);
    if (length) {
        *length = c.top;
    }
    PUTC(&c, '\0');
    return c.stack;
}

void json_copy(json_value* dst, const json_value* src) {
    assert(src != NULL && dst != NULL && dst != src);
    switch (src->type) {
        case JSON_STRING: { // 深度拷贝
            json_set_string(dst, src->u.s.s, src->u.s.len);
            break;
        }
        case JSON_ARRAY: { // 深度拷贝
            json_set_array(dst, src->u.a.size);
            for (size_t i = 0; i < src->u.a.size; i ++) {
                json_copy(&dst->u.a.e[i], &src->u.a.e[i]);
            }
            dst->u.a.size =  src->u.a.size;
            break;
        }
        case JSON_OBJECT: { // 深度拷贝
            json_set_object(dst, src->u.o.size);
            for (size_t i = 0; i < src->u.o.size; i ++) {
                json_value* v = json_set_object_value(dst, src->u.o.m[i].k, src->u.o.m[i].klen);
                json_copy(v, &src->u.o.m[i].v);
            }
            dst->u.o.size = src->u.o.size;
            break;
        }
        default: {
            json_free(dst);
            memcpy(dst, src, sizeof(json_value));
            break;
        }
    }
}
void json_move(json_value* dst, json_value* src) {
    assert(src != NULL && dst != NULL && dst != src);
    json_free(dst);
    memcpy(dst, src, sizeof(json_value));
    json_init(src);
}
void json_swap(json_value* lhs, json_value* rhs) {
    assert(lhs != NULL && rhs != NULL);
    if (lhs != rhs) {
        json_value temp;
        memcpy(&temp, lhs, sizeof(json_value));
        memcpy(lhs, rhs, sizeof(json_value));
        memcpy(rhs, &temp, sizeof(json_value));
    }
}


json_type json_get_type(const json_value* v) {
    assert(v != NULL);
    return v->type;
}


int json_is_equal(const json_value* lhs, const json_value* rhs) {
    assert(lhs != NULL && rhs != NULL);
    if (lhs->type != rhs->type) {
        return 0;
    }
    switch (lhs->type) {
        case JSON_STRING: {
            return lhs->u.s.len == rhs->u.s.len && memcmp(lhs->u.s.s, rhs->u.s.s, lhs->u.s.len) == 0;
        }
        case JSON_NUMBER: {
            return lhs->u.n == rhs->u.n;
        }
        case JSON_ARRAY: {
            if (lhs->u.a.size != rhs->u.a.size) {
                return 0;
            }
            for (size_t i = 0; i < lhs->u.a.size; i ++ ) {
                if (!json_is_equal(&lhs->u.a.e[i], &rhs->u.a.e[i])) {
                    return 0;
                }
            }
            return 1;
        }
        case JSON_OBJECT: { // 对象成员顺序不同不影响比较结果
            if (lhs->u.o.size != rhs->u.o.size) {
                return 0;
            }
            size_t index;
            for (size_t i = 0; i < lhs->u.o.size; i ++) {
                index = json_find_object_index(rhs, lhs->u.o.m[i].k, lhs->u.o.m[i].klen);
                if (index == JSON_KEY_NOT_EXIST) {
                    return 0;
                }
                if (!json_is_equal(&lhs->u.o.m[i].v, &rhs->u.o.m[index].v)) {
                    return 0;
                }
            }
            return 1;
        }
        default: {
            return 1;
        }
    }
}




int json_get_boolean(const json_value* v) {
    assert(v != NULL && (v->type == JSON_TRUE || v->type == JSON_FALSE));
    return v->type == JSON_TRUE;
}
void json_set_boolean(json_value* v, int b) {
    assert(v != NULL);
    json_free(v);
    v->type = b == 0 ? JSON_FALSE : JSON_TRUE;
}

double json_get_number(const json_value* v) {
    assert(v != NULL && v->type == JSON_NUMBER);
    return v->u.n;
}
void json_set_number(json_value* v, double n) {
    json_free(v); // assert(v != NULL);
    v->type = JSON_NUMBER;
    v->u.n = n;
}

const char* json_get_string(const json_value* v) {
    assert(v != NULL && v->type == JSON_STRING);
    return v->u.s.s;
}
size_t json_get_string_length(const json_value* v) {
    assert(v != NULL && v->type == JSON_STRING);
    return v->u.s.len;
}
void json_set_string(json_value* v, const char* s, size_t len) {
    assert(v != NULL && (s != NULL || len == 0));
    json_free(v);
    v->u.s.s = (char*)malloc(len + 1);
    memcpy(v->u.s.s, s, len);
    v->u.s.s[len] = '\0';
    v->u.s.len = len;
    v->type = JSON_STRING;
}


void json_set_array(json_value* v, size_t capacity) {
    assert(v != NULL);
    json_free(v);
    v->type = JSON_ARRAY;
    v->u.a.size = 0;
    v->u.a.capacity = capacity;
    v->u.a.e = capacity > 0 ? (json_value*)malloc(capacity * sizeof(json_value)) : NULL;
}
size_t json_get_array_size(const json_value* v) {
    assert(v != NULL && v->type == JSON_ARRAY);
    return v->u.a.size;
}
size_t json_get_array_capacity(const json_value* v) {
    assert(v != NULL && v->type == JSON_ARRAY);
    return v->u.a.capacity;
}
void json_reserve_array(json_value* v, size_t capacity) {
    assert(v != NULL && v->type == JSON_ARRAY);
    if (v->u.a.capacity < capacity) {
        v->u.a.capacity = capacity;
        v->u.a.e = (json_value*)realloc(v->u.a.e, capacity * sizeof(json_value));
    }
}
void json_shrink_array(json_value* v) {
    assert(v != NULL && v->type == JSON_ARRAY);
    if (v->u.a.capacity > v->u.a.size) {
        v->u.a.capacity = v->u.a.size;
        v->u.a.e = (json_value*)realloc(v->u.a.e, v->u.a.capacity * sizeof(json_value));
    }
}
void json_clear_array(json_value* v) {
    assert(v != NULL && v->type == JSON_ARRAY);
    json_erase_array_element(v, 0, v->u.a.size);
}
json_value* json_get_array_element(const json_value* v, size_t index) {
    assert(v != NULL && v->type == JSON_ARRAY);
    assert(v->u.a.size > index);
    return &v->u.a.e[index];
}
json_value* json_pushback_array_element(json_value* v) {
    assert(v != NULL && v->type == JSON_ARRAY);
    if (v->u.a.size == v->u.a.capacity) {
        json_reserve_array(v, v->u.a.capacity == 0 ? 1 : v->u.a.capacity * 2);
    }
    json_init(&v->u.a.e[v->u.a.size]);
    return &v->u.a.e[v->u.a.size ++];
}
void json_popback_array_element(json_value* v) {
    assert(v != NULL && v->type == JSON_ARRAY && v->u.a.size > 0);
    json_free(&v->u.a.e[-- v->u.a.size]);
}
json_value* json_insert_array_element(json_value* v, size_t index) {
    assert(v != NULL && v->type == JSON_ARRAY && index <= v->u.a.size);
    if (v->u.a.size == v->u.a.capacity) {
        json_reserve_array(v, v->u.a.capacity == 0 ? 1 : v->u.a.capacity * 2);
    }
    memcpy(&v->u.a.e[index + 1], &v->u.a.e[index], (v->u.a.size - index) * sizeof(json_value));
    json_init(&v->u.a.e[index]);
    v->u.a.size ++;
    return &v->u.a.e[index];
}
void json_erase_array_element(json_value* v, size_t index, size_t count) {
    assert(v != NULL && v->type == JSON_ARRAY && index + count <= v->u.a.size);
    for (size_t i = index; i < index + count; i ++) {
        json_free(&v->u.a.e[i]);
    }
    memcpy(&v->u.a.e[index], &v->u.a.e[index + count], (v->u.a.size - index - count) * sizeof(json_value));
    for (size_t i = v->u.a.size - count; i < v->u.a.size; i ++) {
        json_init(&v->u.a.e[i]);
    }
    v->u.a.size -= count;
}



void json_set_object(json_value* v, size_t capacity) {
    assert(v != NULL);
    json_free(v);
    v->type = JSON_OBJECT;
    v->u.o.size = 0;
    v->u.o.capacity = capacity;
    v->u.o.m = capacity > 0 ? (json_member*)malloc(capacity * sizeof(json_member)) : NULL;
}
size_t json_get_object_size(const json_value* v) {
    assert(v != NULL && v->type == JSON_OBJECT);
    return v->u.o.size;
}
size_t json_get_object_capacity(const json_value* v) {
    assert(v != NULL && v->type == JSON_OBJECT);
    return v->u.o.capacity;
}
void json_reserve_object(json_value* v, size_t capacity) {
    assert(v != NULL && v->type == JSON_OBJECT);
    if (v->u.o.capacity < capacity) {
        v->u.o.capacity = capacity;
        v->u.o.m = (json_member*)realloc(v->u.o.m, capacity * sizeof(json_member));
    }
}
void json_shrink_object(json_value* v) {
    assert(v != NULL && v->type == JSON_OBJECT);
    if (v->u.o.capacity > v->u.o.size) {
        v->u.o.capacity = v->u.o.size;
        v->u.o.m = (json_member*)realloc(v->u.o.m, v->u.o.capacity * sizeof(json_member));
    }
}
void json_clear_object(json_value* v) {
    assert(v != NULL && v->type == JSON_OBJECT);
    for (size_t i = 0; i < v->u.o.size; i ++) {
        free(v->u.o.m[i].k);
        json_free(&v->u.o.m[i].v);
    }
    v->u.o.size = 0;
}
const char* json_get_object_key(const json_value* v, size_t index) {
    assert(v != NULL && v->type == JSON_OBJECT);
    assert(index < v->u.o.size);
    return v->u.o.m[index].k;
}
size_t json_get_object_key_length(const json_value* v, size_t index) {
    assert(v != NULL && v->type == JSON_OBJECT);
    assert(index < v->u.o.size);
    return v->u.o.m[index].klen;
}
json_value* json_get_object_value(const json_value* v, size_t index) {
    assert(v != NULL && v->type == JSON_OBJECT);
    assert(index < v->u.o.size);
    return &v->u.o.m[index].v;
}
size_t json_find_object_index(const json_value* v, const char* key, size_t klen) {
    assert(v != NULL && v->type == JSON_OBJECT && key != NULL);
    for (size_t i = 0; i < v->u.o.size; i ++) {
        if (v->u.o.m[i].klen == klen && memcmp(v->u.o.m[i].k, key, klen) == 0) {
            return i;
        }
    }
    return JSON_KEY_NOT_EXIST;
}
json_value* json_find_object_value(json_value* v, const char* key, size_t klen) {
    size_t index = json_find_object_index(v, key, klen);
    return index != JSON_KEY_NOT_EXIST ? &v->u.o.m[index].v : NULL;
}
json_value* json_set_object_value(json_value* v, const char* key, size_t klen) {
    assert(v != NULL && v->type == JSON_OBJECT && key != NULL);
    size_t index = json_find_object_index(v, key, klen);
    if (index != JSON_KEY_NOT_EXIST) {
        return &v->u.o.m[index].v;
    }
    if (v->u.o.size == v->u.o.capacity) {
        json_reserve_object(v, v->u.o.capacity == 0 ? 1 : v->u.o.capacity * 2);
    }
    index = v->u.o.size ++;
    memcpy(v->u.o.m[index].k = (char*)malloc(klen + 1), key, klen);
    v->u.o.m[index].k[klen] = '\0';
    v->u.o.m[index].klen = klen;
    json_init(&v->u.o.m[index].v);
    return &v->u.o.m[index].v;
}
void json_remove_object_value(json_value* v, size_t index) {
    assert(v != NULL && v->type == JSON_OBJECT && index < v->u.o.size);
    free(v->u.o.m[index].k);
    json_free(&v->u.o.m[index].v);
    memcpy(&v->u.o.m[index], &v->u.o.m[index + 1], (v->u.o.size - index - 1) * sizeof(json_member));
    v->u.o.size --;
}