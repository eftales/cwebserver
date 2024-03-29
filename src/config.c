#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>

#include "stringutils.h"
#include "config.h"

config* config_init() {
    config *conf;
    //申请内存并初始化
    conf = malloc(sizeof(*conf));
    memset(conf, 0, sizeof(*conf));

    return conf;
}

void config_free(config *conf) {
    if (!conf) return;
    free(conf);
}

void config_load(config *conf, const char *fn) {
    char *errormsg;
    struct stat st;
    string *line;
    string *buf;
    string *key;
    string *value;
    FILE *fp;
    int lineno = 0;
    int is_str = 0;
    char ch;

    // 打开文件
    fp = fopen(fn, "r");
    if (!fp) {
        fprintf(stderr, "%s: failed to open config file\n", fn);
        exit(1);
    }

    // 初始化字符串
    line = string_init();
    key = string_init();
    value = string_init();
    buf = key;
    lineno = 1;

    // 遍历读取文件中的每个字符
    while ((ch = fgetc(fp)) != EOF) {
        if (ch != '\n')
            string_append_ch(line, ch);

        if (ch == '\\')
            continue;

        if (!is_str && (ch == ' ' || ch == '\t'))
            continue;

        if (ch == '"') {
            is_str = (is_str + 1) % 2;
            continue;
        }

        if (ch == '=') {
            buf = value;
            continue;
        }
        
        // 遇到换行时需要判断是否key和value可以构成一个配置项
        if (ch == '\n') {
            if ((key->len == 0 && value->len > 0) ||
                (value->len == 0 && key->len > 0)) {
                errormsg = "bad syntax"; goto configerr;
            } 
            //key和value都不为空
            if (value->len != 0 && key->len != 0) {
                //如果key为port，则获取端口
                if (strcasecmp(key->ptr, "port") == 0) {
                    conf->port = atoi(value->ptr);
                    //端口为0，错误
                    if (conf->port == 0) {
                        errormsg = "invalid port"; goto configerr;
                    }        
                } 
                //如果key为document-dir,则获取文件状态
                else if (strcasecmp(key->ptr, "document-dir") == 0) {
                    if (stat(value->ptr, &st) == 0) {
                        if (!S_ISDIR(st.st_mode)) {
                            errormsg = "invalid directory"; goto configerr;
                        }
                    } else {
                        errormsg = strerror(errno); goto configerr;
                    }

                    realpath(value->ptr, conf->doc_root);
                } else {
                    errormsg = "unsupported config setting"; goto configerr;
                }
            }

            // 重置字符串
            string_reset(line);
            string_reset(key);
            string_reset(value);
        
            buf = key;
            lineno++;

            continue;
        }

        string_append_ch(buf, ch);
    }

    // 释放文件描述符和字符串
    fclose(fp);
    string_free(key);
    string_free(value);
    string_free(line);

    return;

configerr:
    // 配置文件读取失败时打印出错信息并退出
    fprintf(stderr, "\n*** FAILED TO LOAD CONFIG FILE ***\n");
    fprintf(stderr, "at line: %d\n", lineno);
    fprintf(stderr, ">> '%s'\n", line->ptr);
    fprintf(stderr, "%s\n", errormsg);
    exit(1);
}