#include <limits.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "log.h"
#include "http_header.h"
#include "response.h"

// 文件扩展名与MimeType数据结构
typedef struct {
    // 文件扩展名
    const char *ext;
    // Mime类型名
    const char *mime;
} mime;

// 目前支持的MimeType
static mime mime_types[] = {
    {".html", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".jpg", "image/jpg"},
    {".png", "image/png"}
};

http_response* http_response_init() {
    //初始化响应，分配内存
    http_response *resp;
    resp = malloc(sizeof(*resp));
    memset(resp, 0, sizeof(*resp));

    resp->headers = http_headers_init();
    resp->entity_body = string_init();
    resp->content_length = -1;

    return resp;
}

void http_response_free(http_response *resp) {
    if (!resp) return;

    http_headers_free(resp->headers);
    string_free(resp->entity_body);

    free(resp);
}

// 出错页面
static char err_file[PATH_MAX];
static const char *default_err_msg = "<HTML><HEAD><TITLE>Error</TITLE></HEAD>"
                                      "<BODY><H1>Something went wrong</H1>"
                                      "</BODY></HTML>";

//根据状态码构建响应结构中的状态消息
static const char* reason_phrase(int status_code){
    switch (status_code) {
        case 200:
            return "OK";
        case 400:
            return "Bad Request";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 500:
            return "Internal Server Error";
        case 501:
            return "Not Implemented";

    }

    return "";
}

static int send_all(connection *con, string *buf){
    int bytes_sent = 0;
    int bytes_left = buf->len;
    int nbytes = 0;

    //把buf内容全部发送到客户端
    while (bytes_sent < bytes_left) {
        nbytes = send(con->sockfd, buf->ptr + bytes_sent, bytes_left, 0);
        
        if (nbytes == -1)
            break;

        bytes_sent += nbytes;
        bytes_left -= nbytes;
        
    }

    return nbytes != -1 ? bytes_sent : -1;
}

static const char* get_mime_type(const char *path, const char *default_mime){
    //路径长度
    size_t path_len = strlen(path);

    //逐个比较
    for (size_t i = 0; i < sizeof(mime_types); i++) {
        size_t ext_len = strlen(mime_types[i].ext);
        const char *path_ext = path + path_len - ext_len;
        //如果存在MimeType则返回
        if (ext_len <= path_len && strcmp(path_ext, mime_types[i].ext) == 0)
            return mime_types[i].mime;
    
    }
    //其他情况返回默认mime
    return default_mime;
}

//检查文件权限是否可以访问
static int check_file_attrs(connection *con, const char *path){
    struct stat s;
    con->response->content_length = -1;
    //stat检查路径
    if (stat(path, &s) == -1) {
        con->status_code = 404;
        return -1;
    }
    //S_ISREG检查是否常规文件
    if (!S_ISREG(s.st_mode)) {
        con->status_code = 403;
        return -1;
    }

    con->response->content_length = s.st_size;

    return 0;
}

//读取文件
static int read_file(string *buf, const char *path){
    FILE *fp;
    int fsize;
    //只读方式打开文件
    fp = fopen(path, "r");

    if (!fp) {
        return -1;
    }
    //定位到文件末尾
    fseek(fp, 0, SEEK_END);
    //获取文件长度 
    fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    //申请内存
    string_extend(buf, fsize + 1);
    //读入缓存
    if (fread(buf->ptr, fsize, 1, fp) > 0) {
        buf->len += fsize;
        buf->ptr[buf->len] = '\0';
    }
    
    fclose(fp);

    return fsize;
}

//读取标准错误页面
static int read_err_file(server *serv, connection *con, string *buf){
    //打印错误文件
    snprintf(err_file, sizeof(err_file), "%s/%d.html", serv->conf->doc_root, con->status_code);

    int len = read_file(buf, err_file);

    //如果文件不存在则使用默认的出错信息字符串替代
    if (len <= 0) {
        string_append(buf, default_err_msg);
        len = buf->len;
    }

    return len;
}

static void build_and_send_response(connection *con){
    //初始化发送的字符串
    string *buf = string_init();
    http_response *resp = con->response;
    //添加版本协议并换行
    string_append(buf, "HTTP/1.0 ");
    string_append_int(buf, con->status_code);
    string_append_ch(buf, ' ');
    string_append(buf, reason_phrase(con->status_code));
    string_append(buf, "\r\n");

    //HTTP 头部
    for (size_t i = 0; i < resp->headers->len; i++) {
        string_append_string(buf, resp->headers->ptr[i].key); 
        string_append(buf, ": ");
        string_append_string(buf, resp->headers->ptr[i].value);
        string_append(buf, "\r\n");
    }

    string_append(buf, "\r\n");
    
    //HTTP 身体
    if (resp->content_length > 0 && con->request->method != HTTP_METHOD_HEAD) {
        string_append_string(buf, resp->entity_body);
    }

    // 将字符串缓存发送到客户端
    send_all(con, buf);
    string_free(buf);
}

static void send_err_response(server *serv, connection *con){
    http_response *resp = con->response;
    snprintf(err_file, sizeof(err_file), "%s/%d.html", serv->conf->doc_root, con->status_code);

    // 检查错误页面
    if (check_file_attrs(con, err_file) == -1) {
        resp->content_length = strlen(default_err_msg);
        log_error(serv, "failed to open file %s", err_file);
    }

    // 构建消息头部
    http_headers_add(resp->headers, "Content-Type", "text/html");
    http_headers_add_int(resp->headers, "Content-Length", resp->content_length);

    if (con->request->method != HTTP_METHOD_HEAD) {
       read_err_file(serv, con, resp->entity_body); 
    }

    build_and_send_response(con);
}

static void send_response(server *serv, connection *con){
    http_response *resp = con->response;
    http_request *req = con->request;
    
    http_headers_add(resp->headers, "Server", "cserver");

    if (con->status_code != 200) {
        send_err_response(serv, con);
        return;
    }

    if (check_file_attrs(con, con->real_path) == -1) {
        send_err_response(serv, con);
        return;
    }

    if (req->method != HTTP_METHOD_HEAD) {
        read_file(resp->entity_body, con->real_path);
    }

    // 构建消息头部
    const char *mime = get_mime_type(con->real_path, "text/plain");
    http_headers_add(resp->headers, "Content-Type", mime);
    http_headers_add_int(resp->headers, "Content-Length", resp->content_length);

    build_and_send_response(con);
}

static void send_http09_response(server *serv, connection *con){
    http_response *resp = con->response;

    if (con->status_code == 200 && check_file_attrs(con, con->real_path) == 0) {
        read_file(resp->entity_body, con->real_path);
    } else {
        read_err_file(serv, con, resp->entity_body);
    }

    send_all(con, resp->entity_body);
}

void http_response_send(server *serv, connection *con) {
    if (con->request->version == HTTP_VERSION_09) {
        send_http09_response(serv, con);
    } else {
        send_response(serv, con);
    }
}
