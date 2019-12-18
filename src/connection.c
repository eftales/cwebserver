#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "log.h"
#include "connection.h"
#include "request.h"
#include "response.h"
#include "stringutils.h"

void connection_close(connection *con) {
    if (!con) return;

    // 释放连接对应的请求和相应
    http_request_free(con->request);
    http_response_free(con->response);
    
    // 释放客户端连接中的缓存
    string_free(con->recv_buf);
    
    // 关闭连接socket
    if (con->sockfd > -1)
        close(con->sockfd);

    free(con);
}

connection* connection_accept(server *serv) {
    //新地址
    struct sockaddr_in addr;
    connection *con;
    int sockfd;
    socklen_t addr_len = sizeof(addr);

    // accept() 接受新的连接
    sockfd = accept(serv->sockfd, (struct sockaddr *) &addr, &addr_len);
    
    if (sockfd < 0) {
        log_error(serv, "accept: %s", strerror(errno));
        perror("accept");
        return NULL;
    }

    // 创建连接结构实例
    con = malloc(sizeof(*con));

    // 初始化连接结构
    con->status_code = 0;
    con->request_len = 0;
    con->sockfd = sockfd;
    con->real_path[0] = '\0';

    //接受信息
    con->recv_state = HTTP_RECV_STATE_WORD1;
    con->request = http_request_init();
    con->response = http_response_init();
    con->recv_buf = string_init();
    memcpy(&con->addr, &addr, addr_len);

    return con;
}

int connection_handler(server *serv, connection *con) {
    char buf[512];
    int nbytes;
    int ret;
    //socket id
    printf("socket: %d\n", con->sockfd);

    //缓存接受字符
    while ((nbytes = recv(con->sockfd, buf, sizeof(buf), 0)) > 0) {
        string_append_len(con->recv_buf, buf, nbytes);

        if (http_request_complete(con) != 0)
            break;
    }

    if (nbytes <= 0) {
        ret = -1;
        //接收字节为0，说明套接字已关闭
        if (nbytes == 0) {
            printf("socket %d closed\n", con->sockfd);
            log_info(serv, "socket %d closed", con->sockfd);
        
        } 
        //否则，错误
        else if (nbytes < 0) {
            perror("read");
            log_error(serv, "read: %s", strerror(errno));
        }
    } else {
        ret = 0;
    }

    //请求响应
    http_request_parse(serv, con); 
    http_response_send(serv, con);
    log_request(serv, con);

    return ret;
}