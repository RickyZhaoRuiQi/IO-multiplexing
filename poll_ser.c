/*
int poll(struct pollfd* fds,nfds_t nfds,int timeout);

(1)struct pollfd{
		int fd;				//文件描述符
		short events;		//注册的事件
		short revents;		//实际发生的事件，由内核填充
		}
	每一个pollfd结构体指定一个被监视的文件描述符，可以传递多个结构体，指示poll()监视多个文件描述符。
	每个结构体的events域是监视该文件描述符的事件掩码，由用户来设置这个域。revents域是文件描述符操作
	结果事件掩码，内核在调用返回时设置这个域。events域中请求的任何事件都可能在revents域中返回。

	主要的poll事件类型
	POLLIN		数据可读
	POLLOUT		数据可写
	POLLRDHUP	TCP连接被对方关闭，或者对方关闭了写操作，又GNU引入

(2)nfds参数指定被监听事件集合fds的大小
	typedef unsigned long int nfds_t

(3)timeout参数指定poll超时时间，单位是毫秒。当其值为-1时，poll调用将永远阻塞，直到某个事件发生。

(4)返回值意义与select相同，为就绪文件描述符总数

 */


//tcp回射程序，程序的功能是：客户端向服务器发送信息，服务器接收并原样发送给客户端，客户端显示出接
//收到的信息，来源http://www.cnblogs.com/Anker/archive/2013/08/15/3261006.html作为学习之用


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <sys/types.h>

#define IPADDR	   "127.0.0.1"
#define PORT		6000
#define MAXLINE     1024
#define LISTENQ     5
#define OPEN_MAX    1000

//函数声明
//创建套接字并进行绑定
static int socket_bind(const char* ip,int port);
//IO多路复用poll
static void do_poll(int listenfd);
//处理多个连接
static void handle_connection(struct pollfd *connfds,int num);

int main()
{
	int listenfd = socket_bind(IPADDR,PORT);
	do_poll(listenfd);
	return 0;
}

static int socket_bind(const char* ip,int port)
{
	struct sockaddr_in servaddr;
	int listenfd = socket(AF_INET,SOCK_STREAM,0);
	if(listenfd == -1)
	{
		perror("socket error:");
		exit(1);
	}
	bzero(&servaddr,sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	inet_pton(AF_INET,ip,&servaddr.sin_addr);
	servaddr.sin_port = htons(port);
	if(-1 == bind(listenfd,(struct sockaddr*)&servaddr,sizeof(servaddr)))
	{
		perror("bind error:");
		exit(1);
	}
	listen(listenfd,LISTENQ);
	return listenfd;
}

static void do_poll(int listenfd)
{
	int connfd,sockfd;
	struct sockaddr_in cliaddr;
	socklen_t cliaddrlen;
	struct pollfd clientfds[OPEN_MAX];
	int maxi,i,nready;
	//添加监听描述符
	clientfds[0].fd = listenfd;
	clientfds[0].events = POLLIN;
	//初始化客户连接描述符
	for(i = 1;i < OPEN_MAX;++i)
	  clientfds[i].fd = -1;

	maxi = 0;
	for(;;)
	{
		//获取可用描述符的个数
		nready = poll(clientfds,maxi + 1,-1);
		if(-1 == nready)
		{
			perror("poll error:");
			exit(1);
		}
		//测试监听描述符是否准备好
		if(clientfds[0].revents & POLLIN)
		{
			cliaddrlen = sizeof(cliaddr);
			//接受新的连接
			if((connfd = accept(listenfd,(struct sockaddr*)&cliaddr,&cliaddrlen)) == -1)
			{
				if(errno == EINTR)
				  continue;
				else
				{
					perror("accept errno:");
					exit(1);
				}
			}
			printf("accept a new client:%s:%d\n",inet_ntoa(cliaddr.sin_addr),cliaddr.sin_port);
			//将新的连接添加到数组中
			for(i = 1;i < OPEN_MAX;++i)
			{
				if(clientfds[i].fd < 0)
				{
					clientfds[i].fd = connfd;
					break;
				}
			}
			if(i == OPEN_MAX)
			{
				printf("too many clients.\n");
				exit(1);
			}
			//将新的描述符添加到描述符集合中
			clientfds[i].events = POLLIN;
			maxi = (i > maxi ? i : maxi);
		}
		else//客户端发来数据
		{
			for(i = 1;i < OPEN_MAX;++i)
			{
				if(clientfds[i].fd == -1)
				  continue;
				else if(clientfds[i].revents & POLLIN)
				{
					char buf[MAXLINE] = {0};
					nready = recv(clientfds[i].fd,buf,MAXLINE,0);
					if(0 == nready)
					{
						close(clientfds[i].fd);
						clientfds[i].fd = -1;
						continue;
					}
					printf("%s\n",buf);
					send(clientfds[i].fd,buf,MAXLINE,0);
				}
			}
		}
	}
}
