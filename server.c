#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>

#define BUF_LEN 1028
#define SERVER_PORT 8080
#define MAX_EVENTS 50
#define MAX_DATA_SIZE 4096

//定义好的html页面，web server多从本地文件系统读取html文件
const static char http_error_hdr[] = "HTTP/1.1 404 Not Found\r\nContent-type: text/html\r\n\r\n";
const static char http_html_hdr[] = "HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n";
const static char http_index_html[] =
"<html><head><title>Congrats!</title></head>"
"<body><h1>please login HTTP server demo!</h1>"
"<form>name:    <input type=\"text\" name=\"UName\">"
"<br/>password:<input type=\"password\" name=\"Passwd\">"
"<input type=\"submit\" name=\"tlogin\" id=\"button\" value=\"login\" />"
"</form>";

static char user1[64] = {0};
static char user1_p[64] = {0};

int session_st = 1;  //1:logout 0:online
int session_id = 0;

// 断开连接的函数
void disconnect(int cfd, int epfd){
    int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
    if(ret == -1){
        perror("epoll_ctl del cfd error");
        exit(1);
    }
    close(cfd);
}

void send_page(int sockfd, char *user, char *passwd, int session_id)
{
    char http_user_html[512] = {0};

    sprintf(http_user_html, "<html><head><title>Congrats!</title></head>");
    printf(http_user_html+strlen(http_user_html), "<body><h1>HTTP server demo! online</h1>");
    sprintf(http_user_html+strlen(http_user_html), "<form>name:%s", user);
    sprintf(http_user_html+strlen(http_user_html), "<br/><form>passwd:%s", passwd);
    sprintf(http_user_html+strlen(http_user_html), "</form></body>");

    write(sockfd, http_html_hdr, strlen(http_html_hdr));
    write(sockfd, http_user_html, strlen(http_user_html));
}

int user_login(int sockfd, char *strpath)
{
    //TODO:内存分配小容易缓冲区溢出
    char str_tmp[1024] = {0};
    char user_name[64] = {0};
    char passwd[64] = {0};
    char *tokenPtr;
    char *stepPtr;

    //memset(str_tmp, 0, sizeof(str_tmp));
    //str_tmp = malloc(strlen(strpath));
    strncpy(str_tmp, strpath, strlen(strpath));

    tokenPtr = strtok(str_tmp, "&");
    stepPtr = strchr(tokenPtr, '=');
    stepPtr++;
    strncpy(user_name, stepPtr, strlen(stepPtr));

    tokenPtr = strtok(NULL, "&");
    stepPtr = strchr(tokenPtr, '=');
    stepPtr++;
    strncpy(passwd, stepPtr, strlen(stepPtr));

    //TODO: check user_name and passwd
    //TODO: check session & aging session
    session_st = 0;
    session_id = 10;
    strncpy(user1, user_name, strlen(user_name));
    strncpy(user1_p, passwd, strlen(passwd));

    send_page(sockfd, user1, user1_p, session_id);

    return 0;
}

//解析GET的具体内容:主页，登录
int http_send_file(char *filename, int sockfd)
{
    //TODO: cookie check
    if (session_st == 0) {
        send_page(sockfd, user1, user1_p, session_id);
        return 0;
    }

      if (!strcmp(filename, "/")) {
        //通过write函数发送http响应报文；报文包括HTTP响应头和响应内容--HTML文件
        write(sockfd, http_html_hdr, strlen(http_html_hdr));
        write(sockfd, http_index_html, strlen(http_index_html));
      } else if (NULL != strstr(filename, "tlogin=login")) {
        user_login(sockfd, filename);
    } else {
        // 文件未找到情况下发送404error响应
        printf("%s:file not find!\n",filename);
        write(sockfd, http_error_hdr, strlen(http_error_hdr));
      }
  return 0;
}

//HTTP请求解析
void handle_read(int sockfd, int epollfd){
    char buf[BUF_LEN];
    read(sockfd, buf, BUF_LEN);
    printf("buf:%s\n", buf);
    if(!strncmp(buf, "GET", 3)){
        char *file = buf + 4;
        char *space = strchr(file, ' ');
        *space = '\0';
        http_send_file(file, sockfd);
        disconnect(sockfd, epollfd);
    }else{
        //其他HTTP请求处理，如POST，HEAD等 。这里我们只处理GET
        //TODO：POST etc.
        printf("unsupported request!\n");
        return;
    }
}

void do_accept(int sockfd,int epollfd)
{
    struct sockaddr_in sin;
    socklen_t len=sizeof(struct sockaddr_in);
    bzero(&sin,len);

    int confd=accept(sockfd,(struct sockaddr *)&sin,&len);

    if(confd<0)
    {
        perror("connect error\n");
        exit(-1);
    }

    //把客户端新建立的连接添加到EPOLL的监听中
    struct epoll_event event;
    event.data.fd=confd;
    event.events=EPOLLIN|EPOLLET;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,confd,&event);

    return ;
}

void Handle(int clientfd)
{
    int recvLen=0;
    char recvBuf[MAX_DATA_SIZE];
    memset(recvBuf,0,sizeof(recvBuf));
    recvLen=recv(clientfd,(char *)recvBuf,MAX_DATA_SIZE,0);
    if(recvLen==0)
        return ;
    else if(recvLen<0)
    {
        perror("recv Error");
        exit(-1);
    }
    //各种处理
    printf("接收到的数据:%s \n",recvBuf);
    return ;
}

void main(){
    int i=0;
    int sockfd,err,newfd;
    struct sockaddr_in addr;
    //建立TCP套接字
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0){
        perror("socket creation failed!\n");
        return;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    //这里要注意，端口号一定要使用htons先转化为网络字节序，否则绑定的实际端口
    //可能和你需要的不同
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if(bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in))){
        perror("socket binding failed!\n");
        return;
    }
    if(listen(sockfd, 128)){
        perror("监听失败");
        exit(-1);
    }

    //epoll初始化
    int epollfd;//epoll描述符
    struct epoll_event eventList[MAX_EVENTS];
    epollfd=epoll_create(MAX_EVENTS);
    struct epoll_event event;
    event.events=EPOLLIN|EPOLLET;
    event.data.fd=sockfd;//把server socket fd封装进events里面

    //epoll_ctl设置属性,注册事件
    if(epoll_ctl(epollfd,EPOLL_CTL_ADD,sockfd,&event)<0){
        printf("epoll 加入失败 fd:%d\n",sockfd);
        exit(-1);
    }

    while(1){
        int timeout=300;//设置超时;在select中使用的是timeval结构体
        //epoll_wait epoll处理
        //ret会返回在规定的时间内获取到IO数据的个数，并把获取到的event保存在eventList中，注意在每次执行该函数时eventList都会清空，由epoll_wait函数填写。
        //而不清除已经EPOLL_CTL_ADD到epollfd描述符的其他加入的文件描述符。这一点与select不同，select每次都要进行FD_SET，具体可看我的select讲解。
        //epoll里面的文件描述符要手动通过EPOLL_CTL_DEL进行删除。
        int ret=epoll_wait(epollfd,eventList,MAX_EVENTS,timeout);

        if(ret<0){
            perror("epoll error\n");
            break;
        }
        else if(ret==0){
            //超时
            //printf("epoll_wait timeout.\n");
            continue;
        }

        //直接获取了事件数量，给出了活动的流，这里就是跟selec，poll区别的关键 //select要用遍历整个数组才知道是那个文件描述符有事件。而epoll直接就把有事件的文件描述符按顺序保存在eventList中
        for(i=0;i<ret;i++){
            //错误输出
            if((eventList[i].events & EPOLLERR)
              || (eventList[i].events & EPOLLHUP)
              || !(eventList[i].events & EPOLLIN)){
                printf("epoll error\n");
                close(eventList[i].data.fd);
                exit(-1);
            }

            if(eventList[i].data.fd==sockfd){
                //printf("sockfd=%d.\n", sockfd);
                //这个是判断sockfd的，主要是用于接收客户端的连接accept
                do_accept(sockfd, epollfd);
            }else{ //里面可以通过判断eventList[i].events&EPOLLIN 或者 eventList[i].events&EPOLLOUT 来区分当前描述符的连接是对应recv还是send
                //printf("sockfd=%d.event[%d]=%d.\n", sockfd, i, eventList[i].data.fd);
                //其他所有与客户端连接的clientfd文件描述符
                //获取数据等操作
                //如需不接收客户端发来的数据，但是不关闭连接。
                //epoll_ctl(epollfd, EPOLL_CTL_DEL,eventList[i].data.fd,eventList[i]);
                //Handle对各个客户端发送的数据进行处理
                //Handle(eventList[i].data.fd);
                handle_read(eventList[i].data.fd, epollfd);
            }
        }
    }

    close(epollfd);
    close(sockfd);
    return;
}

//参考:https://blog.csdn.net/wzx19840423/article/details/47811571

