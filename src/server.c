#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netinet/in.h>

#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "server.h"
#include "log.h"
#include "connection.h"
#include "config.h"

// 默认端口号
#define DEFAULT_PORT 8080
#define BACKLOG 10

static server* server_init(){
    server *serv;
    //分配内存并初始化
    serv = malloc(sizeof(*serv));
    memset(serv, 0, sizeof(*serv));
    return serv;
}

static void jail_server(server *serv, char *logfile, const char *chroot_path){
    //获取目录路径长度
    size_t root_len = strlen(chroot_path);
    size_t doc_len = strlen(serv->conf->doc_root);
    size_t log_len = strlen(logfile);

    // 检查web文件目录是否在根目录下
    if (root_len < doc_len && strncmp(chroot_path, serv->conf->doc_root, root_len) == 0) {
        // 更新web文件目录为根目录的相对路径
        strncpy(serv->conf->doc_root, &serv->conf->doc_root[0] + root_len, doc_len - root_len + 1);
    } else {
        fprintf(stderr, "document root %s is not a sub-directory in chroot %s\n", serv->conf->doc_root, chroot_path);
        exit(1);
    }

    // 检查日志文件是否在根目录下
    if (serv->use_logfile) {
        if (logfile[0] != '/')
            fprintf(stderr, "warning: log file is not an absolute path, opening it will fail if it's not in chroot\n");
        else if (root_len < log_len && strncmp(chroot_path, logfile, root_len) == 0) {
            // 更新日志文件为chroot_path的相对路径
            strncpy(logfile, logfile + root_len, log_len - root_len + 1);
        } else {
            fprintf(stderr, "log file %s is not in chroot\n", logfile);
            exit(1);
        }
    }

    // 改变根目录位置
    if (chroot(chroot_path) != 0) {
        perror("chroot");
        exit(1);
    }

    //定位到根目录
    chdir("/");
}

static void daemonize(server *serv, int null_fd){
    struct sigaction sa;
    int fd0, fd1, fd2;

    //赋予进程最大权限
    umask(0);

    // fork()新的进程，0为则正确
    switch(fork()) {
        case 0:
            break;
        case -1:
            log_error(serv, "daemon fork 1: %s", strerror(errno));
            exit(1);
        default:
            exit(0);
    }
    //返回新进程id
    setsid();

    //初始化信号集
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    //终止进程
    if (sigaction(SIGHUP,  &sa, NULL) < 0) {
        log_error(serv, "SIGHUP: %s", strerror(errno));
        exit(1);
    }

    switch(fork()) {
        case 0:
            break;
        case -1:
            log_error(serv, "daemon fork 2: %s", strerror(errno));
            exit(1);
        default:
            exit(0);
    }

    //定位根目录
    chdir("/");

    // 关闭标准输入，输出，错误
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    //复制描述
    fd0 = dup(null_fd);
    fd1 = dup(null_fd);
    fd2 = dup(null_fd);

    if (null_fd != -1)
        close(null_fd);

    if (fd0 != 0 || fd1 != 1 || fd2 != 2) {
        log_error(serv, "unexpected fds: %d %d %d", fd0, fd1, fd2);
        exit(1);
    }

    log_info(serv, "pid: %d", getpid());
}

static void bind_and_listen(server *serv){
    struct sockaddr_in serv_addr;
    // 创建socket，ipv4,tcp
    serv->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    //创建失败，写入日志
    if (serv->sockfd < 0) {
        perror("socket");
        log_error(serv, "socket: %s", strerror(errno));
        exit(1);
    }

    int yes = 0;
    //设置套接口SO_REUSEADDR许套接口和一个已在使用中的地址捆绑
    if ((setsockopt(serv->sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) == -1) {
        perror("setsockopt");
        log_error(serv, "socket: %s", strerror(errno));
        exit(1);
    }

    //初始化服务器地址
    memset(&serv_addr, 0, sizeof serv_addr);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(serv->port);

    // bind() 绑定
    if (bind(serv->sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        log_error(serv, "bind: %s", strerror(errno));
        exit(1);
    }

    // listen() 监听，等待客户端连接
    if (listen(serv->sockfd, BACKLOG) < 0) {
        perror("listen");
        log_error(serv, "listen: %s", strerror(errno));
        exit(1);
    }
}


static void server_free(server *serv) {
    config_free(serv->conf);
    free(serv);
}

static void start_server(server *serv, const char *config, const char *chroot_path, char *logfile) {
    int null_fd = -1;
    
    // 1. 加载配置文件
    serv->conf = config_init();
    config_load(serv->conf, config);

    // 2. 设置端口号
    if (serv->port == 0 && serv->conf->port != 0) {
        serv->port = serv->conf->port;
    }
    else if (serv->port == 0) {
        serv->port = DEFAULT_PORT;
    }

    printf("port: %d\n", serv->port);
    
    if (serv->is_daemon) {
        null_fd = open("/dev/null", O_RDWR);
    }

    // 3. 判断是否根目录
    if (serv->do_chroot) {
        jail_server(serv, logfile, chroot_path);
    }

    // 4. 打开日志文件
    log_open(serv, logfile);
    
    // 5. 判断是否以守护进程方式启动
    if (serv->is_daemon) {
        daemonize(serv, null_fd);
    }

    // 6. 绑定并监听
    bind_and_listen(serv);
    // 当有新的连接时创建客户端结构数据并fork()新的进程处理HTTP请求
    // 此处可以调用客户端管理模块的接口
}

static void sigchld_handler(int s) {
    pid_t pid;
    while((pid = waitpid(-1, NULL, WNOHANG)) > 0);
}

static void do_fork_strategy(server *serv){
    //新进程
    pid_t pid;
    struct sigaction sa;
    //客户端
    connection *con;
    
    //子进程处理
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    //信号处理
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    //循环接收
    while (1) {
        if ((con = connection_accept(serv)) == NULL) {
            continue;
        }

        if ((pid = fork()) == 0) {
            
            // 子进程中处理HTTP请求
            close(serv->sockfd);

            connection_handler(serv, con);
            connection_close(con);

            exit(0);
        }

        printf("child process: %d\n", pid);
        connection_close(con);
    }
}

// 主函数
int main(int argc, char** argv) {
    
    // 初始化服务器
    server *serv;
    char logfile[PATH_MAX];
    char chroot_path[PATH_MAX];
    int opt;
    
    serv = server_init();

    // 解析命令行参数
    while((opt = getopt(argc, argv, "p:l:r:d")) != -1) {
        switch(opt) {
            // 设置端口号
            case 'p':
                serv->port = atoi(optarg);
                if (serv->port == 0) {
                    fprintf(stderr, "error: port must be an integer\n");
                    exit(1); 
                }
                break;
            // 在后台启动
            case 'd':
                serv->is_daemon = 1;
                break;
            // 使用日志文件
            case 'l':
                strcpy(logfile, optarg);
                serv->use_logfile = 1;
                break;
            // 使用chroot
            case 'r':
                if (realpath(optarg, chroot_path) == NULL) {
                    perror("chroot");
                    exit(1);
                }
                serv->do_chroot = 1;
                break;
        }
    }

    // 启动服务
    start_server(serv, "web.conf", chroot_path, logfile);
    
    // 进入主循环等待并处理客户端连接
    do_fork_strategy(serv);
    
    // 关闭日志文件
    log_close(serv);
    
    // 释放服务器结构体
    server_free(serv);

    return 0;
}
