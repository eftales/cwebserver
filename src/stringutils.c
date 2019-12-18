#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "stringutils.h"

#define STRING_SIZE_INC 64      //字符串分配内存标准

string* string_init() {
    string *s;
    //申请内存并初始化
    s = malloc(sizeof(*s));
    s->ptr = NULL;
    s->size = s->len = 0;
    return s;
}

string* string_init_str(const char *str) {
    string *s = string_init();
    string_copy(s, str);

    return s;
}

void string_free(string *s) {
    //字符串为空直接返回
    if (!s) return;
    free(s->ptr);
    free(s);
}

void string_reset(string *s) {
    //确定字符串不为空
    assert(s != NULL);
    //重置字符串，起始为空
    if (s->size > 0) {
        s->ptr[0] = '\0';
    }
    s->len = 0;
}

void string_extend(string *s, size_t new_len) {
    //确定字符串不为空
    assert(s != NULL);

    //新长度大于原本长度，则申请空间
    if (new_len >= s->size) {
        s->size += new_len - s->size;
        s->size += STRING_SIZE_INC - (s->size % STRING_SIZE_INC);
        s->ptr = realloc(s->ptr, s->size);
    }
}

int string_copy_len(string *s, const char *str, size_t str_len) {
    //确定参数不为空
    assert(s != NULL);
    assert(str != NULL);

    if (str_len <= 0) return 0;
    //为s申请空间
    string_extend(s, str_len + 1);
    //将str拷贝到s
    strncpy(s->ptr, str, str_len);
    s->len = str_len;
    //添加终止符
    s->ptr[s->len] = '\0';

    return str_len;
}

int string_copy(string *s, const char *str) {
    return string_copy_len(s, str, strlen(str));
}

int string_append_string(string *s, string *s2) {
    //确定参数不为空
    assert(s != NULL);
    assert(s2 != NULL);

    return string_append_len(s, s2->ptr, s2->len);
}

int string_append_int(string *s, int i) {
    //确定s不为空
    assert(s != NULL);
    char buf[30];
    //替换表
    char digits[] = "0123456789";
    int len = 0;
    //符号标记
    int minus = 0;
    
    //将负数变正
    if (i < 0) {
        minus = 1;
        i *= -1;
    } else if (i == 0) {
        string_append_ch(s, '0');
        return 1;
    }
    
    //整数大于9处理
    while (i) {
        buf[len++] = digits[i % 10];
        i = i / 10;
    }

    if (minus)
        buf[len++] = '-';

    //添加到字符串，反向添加
    for (int i = len - 1; i >= 0; i--) {
        string_append_ch(s, buf[i]);
    }

    return len;
    
}

int string_append_len(string *s, const char *str, size_t str_len) {
    //确定参数不为空
    assert(s != NULL);
    assert(str != NULL);

    if (str_len <= 0) return 0;
    //为s申请内存
    string_extend(s, s->len + str_len + 1);
    //拷贝到s
    memcpy(s->ptr + s->len, str, str_len);
    s->len += str_len;
    s->ptr[s->len] = '\0';

    return str_len;
}

int string_append(string *s, const char *str) {
    return string_append_len(s, str, strlen(str));
}

int string_append_ch(string *s, char ch) {
    //确定s不为空
    assert(s != NULL);
    //申请内存
    string_extend(s, s->len + 2);
    //添加字符
    s->ptr[s->len++] = ch;
    //添加终止符
    s->ptr[s->len] = '\0';

    return 1;
}