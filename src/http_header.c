#include <assert.h>
#include <string.h>

#include "http_header.h"

#define HEADER_SIZE_INC 20

http_headers* http_headers_init() {
    http_headers *h;
    h = malloc(sizeof(*h));
    memset(h, 0, sizeof(*h));
    return h;
}

void http_headers_free(http_headers *h) {
    if (!h) return;
    //逐个释放
    for (size_t i = 0; i < h->len; i++) {
        string_free(h->ptr[i].key);
        string_free(h->ptr[i].value);
    }

    free(h->ptr);
    free(h);
}

static void extend(http_headers *h){
    //如果新头部长度大于原长度，需要申请内存
    if (h->len >= h->size) {
        h->size += HEADER_SIZE_INC;
        h->ptr = realloc(h->ptr, h->size * sizeof(keyvalue));
    }
}

void http_headers_add(http_headers *h, const char *key, const char *value) {
    //确定头部不为空
    assert(h != NULL);
    //扩展头部
    extend(h);
    //添加
    h->ptr[h->len].key = string_init_str(key); 
    //值是一个字符串
    h->ptr[h->len].value = string_init_str(value);
    h->len++;
}

void http_headers_add_int(http_headers *h, const char *key, int value) {
    //确定头部不为空
    assert(h != NULL);
     //扩展头部
    extend(h);

    //将整型value转换为字符型
    string *value_str = string_init();
    string_append_int(value_str, value);

    h->ptr[h->len].key = string_init_str(key); 
    h->ptr[h->len].value = value_str;
    h->len++;
}

