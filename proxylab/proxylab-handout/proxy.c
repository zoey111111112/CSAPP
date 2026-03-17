#include <stdio.h>
#include <ctype.h>
#include <strings.h> // strcasecmp 在这里
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define SBUFZISE 16
#define NTHREADS 9

typedef struct {
    int *buf;
    int n;
    int front;
    int rear;
    sem_t mutex;
    sem_t slots;
    sem_t items;
} sbuf_t;

typedef struct cache_item{
    int n;
    char *key;
    char *buf;
    struct cache_item *prev;
    struct cache_item *next;
} cache_item_t;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

/* 声明函数原型 */
void *thread(void *vargp);
void doit(int fd);
void read_requesthdrs(rio_t *rp,int serverfd,char *hostname);
int parse_url(const char *url, char *hostname, char *port, char *uri);
void clienterror(int fd, char *cause, char *errnum,char *shortmsg, char *longmsg);
void sbuf_init(sbuf_t *sp,int n);
void sbuf_insert(sbuf_t *sp,int item);
int sbuf_remove(sbuf_t *sp);
void free_sbuf(sbuf_t *sp);
int find_cache(char *key, int fd);
void insert_to_head(cache_item_t *node);

sbuf_t sbuf;
int cache_size = 0;
cache_item_t *head;
cache_item_t *tail;
pthread_rwlock_t cache_lock;
pthread_rwlockattr_t attr;

int main(int argc,char **argv)
{
    /* 不知道为什么要加这个 */
    Signal(SIGPIPE, SIG_IGN); // 忽略 SIGPIPE 信号
    int listenfd,connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;


    /* Check commond line args */
    if(argc != 2){
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    /* 建立监听套接字 */
    listenfd = Open_listenfd(argv[1]);
    /* 初始化缓冲区 */
    sbuf_init(&sbuf,SBUFZISE);
    // 初始化时设置为写者优先
    pthread_rwlockattr_init(&attr);
    pthread_rwlockattr_setkind_np(&attr,PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
    pthread_rwlock_init(&cache_lock, &attr);

    /* 初始化链表头尾节点 */
    head = NULL;
    tail = NULL;

    /* 创建线程 */
    for(int i=0;i<NTHREADS;i++){
        Pthread_create(&tid,NULL,thread,NULL);
    }

    while(1){
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd,(SA *)&clientaddr,&clientlen);
        sbuf_insert(&sbuf,connfd);
    }


}
/*
* 处理一个完整的Http请求
*/
void doit(int fd){
    rio_t rio;
    int serverfd;
    char buf[MAXLINE],method[MAXLINE],url[MAXLINE],version[MAXLINE];
    char hostname[MAXLINE],port[MAXLINE],uri[MAXLINE],key[MAXLINE];

    /* 绑定客户端套接字和用户缓冲区，用于读取套接字内容 */
    Rio_readinitb(&rio,fd);
    if(!Rio_readlineb(&rio,buf,MAXLINE)){
        return;
    }
    /* 解析报文首行,url是关键 */
    if(sscanf(buf,"%s %s %s",method,url,version) != 3){
        return;
    }
    
    // 判断是否是GET请求
    if (strcasecmp(method, "GET")) {
    
    clienterror(fd, method, "501", "Not Implemented",
    "Proxy does not implement this method");
    return;
    }
    
    /* 从url中解析出Host([www.cmu.edu](http://www.cmu.edu/)),port,uri(home/index.html) */
    if(parse_url(url,hostname,port,uri) == -1){
        clienterror(fd, url, "400", "Bad Request","Invalid request format");
        return;
    }

    /* 构造缓存key */
    strcpy(key,method);
    strcat(key,hostname);
    strcat(key,port);
    strcat(key,uri);
    /* 查找缓存 */
    if(find_cache(key,fd) == 1){
        return;
    }

    /* 和目标服务器建立连接 */
    serverfd = Open_clientfd(hostname,port);
    if(serverfd < 0){
        char addr[MAXLINE];
        sprintf(addr, "%s:%s", hostname, port);
        clienterror(fd, addr, "502", "Bad Gateway","Failed to connect to");
        return;
    }
    
    /* 向目标服务器转发请求报文 */
    /* 转发报文首行：GET /home/index.html HTTP/1.0 */
    char request_line[MAXLINE];
    sprintf(request_line,"GET %s HTTP/1.0\r\n",uri);
    Rio_writen(serverfd,request_line,strlen(request_line));
    /* 构造并转发报文剩余部分 */
    read_requesthdrs(&rio,serverfd,hostname);
    
    /* 从目标服务器读取请求报文并转发给客户端 */
    rio_t server_rio;
    Rio_readinitb(&server_rio,serverfd);
    int n;
    /* 为缓存项申请内存 */
    char *cache_buf = Calloc(1,MAX_OBJECT_SIZE);
    int cache_len = 0;
    int success = 1;  // 标记是否成功处理
    while ((n = Rio_readnb(&server_rio, buf, MAXLINE)) > 0) {
        Rio_writen(fd, buf, n);
        // 复制到缓存（只要还没超过大小）
        if (cache_len + n <= MAX_OBJECT_SIZE) {
            memcpy(cache_buf + cache_len, buf, n);
            cache_len += n;
        } else {
            // 超过缓存大小，标记为不缓存，但继续转发
            success = 0;
        }
    }
    if (success && cache_len > 0) {  // 有内容且未超过大小
        cache_item_t *item = Calloc(1, sizeof(cache_item_t));
        if (item) {
            item->key = strdup(key);
            item->buf = cache_buf;  // 缓存接管缓冲区
            item->n = cache_len;
            insert_to_head(item);
            // cache_buf 现在由缓存管理，不要释放
        } else {
            free(cache_buf);  // 内存分配失败，释放缓冲区
        }
    } else {
        free(cache_buf);  // 不缓存，释放缓冲区
    }
    
    /* 关闭和目标服务器的连接 */
    Close(serverfd);
}
    

void read_requesthdrs(rio_t *rp,int serverfd,char *hostname) {
    char buf[MAXLINE];

    /* Host头部 */
    sprintf(buf,"Host: %s\r\n",hostname);
    Rio_writen(serverfd,buf,strlen(buf));

    /* 发送固定的头部:User-Agent,Connection,Proxy-Connection */
    Rio_writen(serverfd,user_agent_hdr,strlen(user_agent_hdr));
    Rio_writen(serverfd,connection_hdr,strlen(connection_hdr));
    Rio_writen(serverfd,proxy_connection_hdr,strlen(proxy_connection_hdr));

    /* 转发客户端请求头中的剩余行 */
    Rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n")) {
       if (strncasecmp(buf, "Host:", 5) && 
        strncasecmp(buf, "User-Agent:", 11) &&
        strncasecmp(buf, "Connection:", 11) &&
        strncasecmp(buf, "Proxy-Connection:", 17)) 
        {
            Rio_writen(serverfd, buf, strlen(buf));
        }
        Rio_readlineb(rp, buf, MAXLINE);
    }

    /* 空行结束请求报文 */
    Rio_writen(serverfd,"\r\n",2);

}

int parse_url(const char *url, char *hostname, char *port, char *uri){
    // 验证URL是否以http://开头
    if(strncmp(url,"http://",7) != 0){
        return -1;
    }
    char *ptr = url + 7;
    char *host_start = ptr;


    //默认端口80
    strcpy(port,"80");

    // 查找第一个：或 /,确定hostname结束的位置
    while(*ptr && *ptr != ':' && *ptr != '/'){
        ptr++;
    }

    // 有端口号
    if(*ptr == ':'){
        int host_len = ptr - host_start;
        strncpy(hostname,host_start,host_len);
        hostname[host_len] = '\0';

        // 提取端口号
        ptr++;
        char *port_start = ptr;

        while(*ptr && isdigit(*ptr)){
            ptr++;
        }
        int port_len = ptr - port_start;
        if(port_len > 0){
            strncpy(port,port_start,port_len);
            port[port_len] = '\0';
        }
        // 此时ptr指向/或者字符串末尾

    } else {
        // 无端口号,直接提取hostname
        int host_len = ptr - host_start;
        strncpy(hostname, host_start, host_len);
        hostname[host_len] = '\0';
    }
    // 处理URI
    if (*ptr == '/') {
        strcpy(uri, ptr);
    } else {
        // 没有路径，使用根路径
        strcpy(uri, "/");
    }
    return 0;

}

/*

- clienterror - returns an error message to the client
*/
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum,char *shortmsg, char *longmsg)
{
    char buf[MAXLINE];
    
    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));
    
    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
    /* $end clienterror */
void sbuf_init(sbuf_t *sp,int n){
    sp->buf = Calloc(n,sizeof(int));
    sp->n = n;
    sp->front = sp->rear = 0;
    Sem_init(&sp->mutex,0,1);
    Sem_init(&sp->slots,0,n);
    Sem_init(&sp->items,0,0);
}
/* 插入缓冲区 */
void sbuf_insert(sbuf_t *sp,int item){
    P(&sp->slots);
    P(&sp->mutex);
    sp->buf[sp->rear % sp->n] = item;
    sp->rear++;
    V(&sp->mutex);
    V(&sp->items);
}
int sbuf_remove(sbuf_t *sp){
    int item;
    P(&sp->items);
    P(&sp->mutex);
    item = sp->buf[sp->front % sp->n];
    sp->front++;
    V(&sp->mutex);
    V(&sp->slots);
    return item;
}
/* 只有缓冲数组buf通过calloc分配,在堆上,需要手动释放 */
void free_sbuf(sbuf_t *sp){
    free(sp->buf);
}
void *thread(void *vargp){
    /* 转化为可分离线程，终止后内核回收其资源 */
    Pthread_detach(pthread_self());
    int connfd;
    while(1){
        connfd = sbuf_remove(&sbuf);
        doit(connfd);
        Close(connfd);
    }
}
/* 
 * 缓存命中返回 1 并发送数据，否则返回 0 
 * 直接传入 fd，避免额外的内存拷贝
 */
int find_cache(char *key, int fd) {

    pthread_rwlock_rdlock(&cache_lock);  // 加读锁

    int found = 0;
    cache_item_t *p = head;

    while(p) {
        if(strcmp(p->key, key) == 0) {
            /* 命中：直接发送数据 */
            // 使用 Rio_writen 确保完整发送 p->n 字节（处理二进制数据）
            Rio_writen(fd, p->buf, p->n); 
            found = 1;
            break; // 找到后跳出循环，去统一释放锁
        }
        p = p->next;
    }
    pthread_rwlock_unlock(&cache_lock);  // 解除读锁
    return found;
}

void insert_to_head(cache_item_t *node){
    pthread_rwlock_wrlock(&cache_lock);  // 加写锁

    /* 不断删除尾节点 */
    while((cache_size + node->n) > MAX_CACHE_SIZE){
        cache_item_t *victim = tail;  // 保存要删除的节点
        tail = tail->prev;
        tail->next = NULL;
        // 更新缓存大小
        cache_size -= victim->n;
        // 释放victim的资源
        free(victim->buf);
        free(victim->key);
        free(victim);
    }
    node->next = head;
    node->prev = NULL;
    if(head) {
        head->prev = node;
    } else {
        tail = node;
    }
    head = node;
    cache_size += node->n;

    pthread_rwlock_unlock(&cache_lock);  // 解除写锁
}