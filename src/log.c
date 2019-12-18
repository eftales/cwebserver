#include <arpa/inet.h>
#include <syslog.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <stdarg.h>
#include "stringutils.h"
#include "log.h"

void log_open(server *serv, const char *logfile) {
    //判断服务器是否启动日志
    if (serv->use_logfile) {
        //打开日志文件
        serv->logfp = fopen(logfile, "a");

        if (!serv->logfp) {
            perror(logfile);
            exit(1);
        }

        return;
    }
    //webserver固定在每条日志前，LOG_NDELAY立即打开连接，LOG_PID包括每个消息的PID，LOG_DAEMON守护进程
    openlog("webserver", LOG_NDELAY | LOG_PID, LOG_DAEMON);
}

void log_close(server *serv) {
    if (serv->logfp)
        fclose(serv->logfp);
    closelog();
}

static void date_str(string *s){
    //定义一个时间结构
    struct tm *ti;
    //标准计时点到当前秒数
    time_t rawtime;
    char local_date[100];
    char zone_str[20];
    int zone;
    char zone_sign;

    //返回当前时间
    time(&rawtime);
    //转化为本地时间
    ti = localtime(&rawtime);
    //将时间转换成真实世界使用的日期表示方法
    zone = ti->tm_gmtoff / 60;

    if (ti->tm_zone < 0) {
        zone_sign = '-';
        zone = -zone;
    } else
        zone_sign = '+';
    
    zone = (zone / 60) * 100 + zone % 60;
    
    //格式化本地时间和日期
    strftime(local_date, sizeof(local_date), "%d/%b/%Y:%X", ti);
    snprintf(zone_str, sizeof(zone_str), " %c%.4d", zone_sign, zone);

    string_append(s, local_date);
    string_append(s, zone_str);
}

void log_request(server *serv, connection *con) {
    //初始化请求和响应
    http_request *req = con->request;
    http_response *resp = con->response;
    char host_ip[INET_ADDRSTRLEN];
    char content_len[20];
    string *date = string_init();

    //判断服务器或客户端是否启动
    if (!serv || !con)
        return;

    if (resp->content_length > -1 && req->method != HTTP_METHOD_HEAD) {
        snprintf(content_len, sizeof(content_len), "%d", resp->content_length); 
    } else {
        strcpy(content_len, "-");
    }

    //将二进制网络地址转换为ascall码
    inet_ntop(con->addr.sin_family, &con->addr.sin_addr, host_ip, INET_ADDRSTRLEN);
    date_str(date);

    // 日志中需要记录的项目：IP，时间，访问方法，URI，版本，状态，内容长度
    if (serv->use_logfile) {
        fprintf(serv->logfp, "%s - - [%s] \"%s %s %s\" %d %s\n",
                host_ip, date->ptr, req->method_raw, req->uri,
                req->version_raw, con->status_code, content_len);
        fflush(serv->logfp);
    } else {
        syslog(LOG_ERR, "%s - - [%s] \"%s %s %s\" %d %s",
                host_ip, date->ptr, req->method_raw, req->uri,
                req->version_raw, con->status_code, content_len);
    }

    string_free(date);
}

static void log_write(server *serv, const char *type, const char *format, va_list ap){
    string *output = string_init();

    //如果服务器使用日志，写入时间
    if (serv->use_logfile) {
        string_append_ch(output, '[');
        date_str(output);
        string_append(output, "] ");
    }
    
    //写入日志类型
    string_append(output, "[");
    string_append(output, type);
    string_append(output, "] ");
    
    string_append(output, format);
    
    //如果服务器使用日志，将所有参数写入日志
    if (serv->use_logfile) {
        string_append_ch(output, '\n');
        vfprintf(serv->logfp, output->ptr, ap);
        fflush(serv->logfp);
    } else {
        vsyslog(LOG_ERR, output->ptr, ap);
    }

    string_free(output);
}

void log_error(server *serv, const char *format, ...) {
    va_list ap;
    //将所有参数写入日志
    va_start(ap, format);
    //所有信息添加错误标记
    log_write(serv, "error", format, ap);
    va_end(ap);
}

void log_info(server *serv, const char *format, ...) {
    va_list ap;
    //记录所有信息
    va_start(ap, format);
    log_write(serv, "info", format, ap);
    va_end(ap);
}