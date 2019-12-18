#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <stdio.h>

#include "request.h"
#include "http_header.h"

http_request* http_request_init() {
    http_request *req;
    //分配内存
    req = malloc(sizeof(*req));
    req->content_length = 0;
    req->version = HTTP_VERSION_UNKNOWN;
    req->content_length = -1;

    req->headers = http_headers_init();
    return req; 
}

void http_request_free(http_request *req) {
    if (!req) return;

    http_headers_free(req->headers);
    free(req);
}

static char* match_until(char **buf, const char *delims){
    //初始化match
    char *match = *buf;
    //match添加buf中第一次出现delims之前的长度
    char *end_match = match + strcspn(*buf, delims);
    //end_match后添加end_match中delims出现次数
    char *end_delims = end_match + strspn(end_match, delims);

    for (char *p = end_match; p < end_delims; p++) {
        *p = '\0';
    }

    *buf = end_delims;

    return (end_match != end_delims) ? match : NULL;
}

static http_method get_method(const char *method){
    //strcasecmp比较会忽略大小写
    if (strcasecmp(method, "GET") == 0)
        return HTTP_METHOD_GET;
    else if (strcasecmp(method, "HEAD") == 0)
        return HTTP_METHOD_HEAD;
    else if(strcasecmp(method, "POST") == 0 || strcasecmp(method, "PUT") == 0)
        return HTTP_METHOD_NOT_SUPPORTED;
    //其他情况返回未知方法
    return HTTP_METHOD_UNKNOWN;
}

static int resolve_uri(char *resolved_path, char *root, char *uri){
    int ret = 0;
    //初始化完整路径
    string *path = string_init_str(root);
    string_append(path, uri);

    //绝对路径
    char *res = realpath(path->ptr, resolved_path);
    
    if (!res) {
        ret = -1;
        goto cleanup;
    }

    size_t resolved_path_len = strlen(resolved_path);
    size_t root_len = strlen(root);

    //比较路径
    if (resolved_path_len < root_len) {
        ret = -1;
    } else if (strncmp(resolved_path, root, root_len) != 0) {
        ret = -1;
    } else if(uri[0] == '/' && uri[1] == '\0') {
        strcat(resolved_path, "/index.html");
    }

cleanup:
    string_free(path);
    return ret;
}

static void try_set_status(connection *con, int status_code){
    if (con->status_code == 0)
        con->status_code = status_code;
}

void http_request_parse(server *serv, connection *con) {
    //初始化请求
    http_request *req = con->request;
    char *buf = con->recv_buf->ptr;
    //解析HTTP方法
    req->method_raw = match_until(&buf, " ");
    //方法为空，错误
    if (!req->method_raw) {
        con->status_code = 400;
        return;
    }

    // 获得HTTP方法
    req->method = get_method(req->method_raw);

    //处理方法
    if (req->method == HTTP_METHOD_NOT_SUPPORTED) {
        try_set_status(con, 501);
    } else if(req->method == HTTP_METHOD_UNKNOWN) {
        con->status_code = 400;
        return;
    }

    // 获得URI
    req->uri = match_until(&buf, " \r\n");

    if (!req->uri) {
        con->status_code = 400;
        return;
    }

    /*
     * 判断访问的资源是否在服务器上
     *
     */
    if (resolve_uri(con->real_path, serv->conf->doc_root, req->uri) == -1) {
        try_set_status(con, 404);
    } 
    
    // 如果版本为HTTP_VERSION_09立刻退出
    if (req->version == HTTP_VERSION_09) {
        try_set_status(con, 200);
        req->version_raw = "";
        return;
    }

    // 获得HTTP版本
    req->version_raw = match_until(&buf, "\r\n");

    if (!req->version_raw) {
        con->status_code = 400;
        return;
    }

    // 支持HTTP/1.0或HTTP/1.1
    if (strcasecmp(req->version_raw, "HTTP/1.0") == 0) {
        req->version = HTTP_VERSION_10;
    } else if (strcasecmp(req->version_raw, "HTTP/1.1") == 0) {
        req->version = HTTP_VERSION_11;
    } else {
        try_set_status(con, 400);
    }

    if (con->status_code > 0)
        return;

    // 解析HTTP请求头部

    char *p = buf;
    char *endp = con->recv_buf->ptr + con->request_len;

    while (p < endp) {
        const char *key = match_until(&p, ": ");
        const char *value = match_until(&p, "\r\n");

        if (!key || !value) {
            con->status_code = 400;
            return;
        }

        http_headers_add(req->headers, key, value);
    }

    con->status_code = 200;
}

int http_request_complete(connection *con) {
    char c;
    for (; con->request_len < con->recv_buf->len; con->request_len++) {
        c = con->recv_buf->ptr[con->request_len];
        //判断接收状态
        switch (con->recv_state) {
            case HTTP_RECV_STATE_WORD1:
                if (c == ' ')
                    con->recv_state = HTTP_RECV_STATE_SP1;
                else if (!isalpha(c))
                    return -1;
            break;

            case HTTP_RECV_STATE_SP1:
                if (c == ' ')
                    continue;
                if (c == '\r' || c == '\n' || c == '\t')
                    return -1;
                con->recv_state = HTTP_RECV_STATE_WORD2;
            break;

            case HTTP_RECV_STATE_WORD2:
                if (c == '\n') {
                    con->request_len++;
                    con->request->version = HTTP_VERSION_09;
                    return 1;
                } else if (c == ' ')
                    con->recv_state = HTTP_RECV_STATE_SP2;
                else if (c == '\t')
                    return -1;
            break;

            case HTTP_RECV_STATE_SP2:
                if (c == ' ')
                    continue;    
                if (c == '\r' || c == '\n' || c == '\t')
                    return -1;
                con->recv_state = HTTP_RECV_STATE_WORD3;
            break;

            case HTTP_RECV_STATE_WORD3:
                if (c == '\n')
                    con->recv_state = HTTP_RECV_STATE_LF;
                else if (c == ' ' || c == '\t')
                    return -1;
            break;

            case HTTP_RECV_STATE_LF:
                if (c == '\n') {
                    con->request_len++;
                    return 1;
                } else if (c != '\r')
                    con->recv_state = HTTP_RECV_STATE_LINE;
            break;

            case HTTP_RECV_STATE_LINE:
                if (c == '\n')
                    con->recv_state = HTTP_RECV_STATE_LF;
            break;
        }
    }

    return 0;
}