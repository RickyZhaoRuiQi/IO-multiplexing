/*

1.int epoll_create(int size);
2.int epoll_ctl(int epfd,int op,int fd,struct epoll_event *event);
3.int epoll_wait(int epfd,struct epoll_event* events,int maxevents,int time);


1.size参数现在并不起作用，只是个内核的一个提示，告诉它事件表需要多大。该函数返回的文件描述符将作用于其他所有
epoll系统调用的第一个参数，以指定要访问的内核事件表

2.epoll的事件注册函数，它不同于select是在监听事件时告诉内核要监听什么类型的事件，而是在这里先注册要监听的事件
类型。第一个参数是epoll_create返回值。第二个参数表示动作
EPOLL_CTL_ADD:注册新的fd到epfd中
EPOLL_CTL_MOD:修改已经注册的fd监听事件
EPOLL_CTL_DEL:从epfd中删除一个fd
第三个参数是需要作用的对象，第四个参数告诉内核事件类型（在移除时不需要使用）

3.等待事件产生，参数events用来从内核得到就绪事件集合，maxevents告诉内核这个events有多大，该函数返回需要处理事
件的数目。

主要支持的事件同poll，比poll多几个事件

工作模式

LT：当epoll_wait检测到描述符事件发生并将此事件通知应用程序，应用程序可以不立即处理该事件。下次调用epoll_wait时，
会再次响应应用程序并通知此事件。

ET：当epoll_wait检测到描述符事件发生并将此事件通知应用程序，应用程序必须立即处理该事件。如果不处理，下次调用
epoll_wait时，不会再次响应应用程序并通知此事件。

 */

//服务器回射程序echo，练习epoll过程——来自http://www.cnblogs.com/Anker/archive/2013/08/17/3263780.html


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/types.h>

#define IPADDR	   "127.0.0.1"
#define PORT        6000
#define MAXSIZE     1024
#define LISTENQ     5
#define FDSIZE      1000
#define EPOLLEVENTS 100

//函数声明
//创建套接字并进行绑定
static int socket_bind(const char* ip,int port);
//IO多路复用epoll
static void do_epoll(int listenfd);
//事件处理函数
static void handle_events(int epollfd,struct epoll_event *events,int num,int listenfd,char *buf);
//处理接收的连接
static void handle_accept(int epollfd,int listenfd);
//读处理
static void do_read(int epollfd,int fd,char* buf);
//写处理
static void do_write(int epollfd,int fd,char* buf);
//添加事件
static void add_event(int epollfd,int fd,int stat);
//修改事件
static void modify_event(int epollfd,int fd,int state);
//删除事件
static void delete_event(int epollfd,int fd,int state);

int main()
{
	int listenfd = socket_bind(IPADDR,PORT);
	listen(listenfd,LISTENQ);
	do_epoll(listenfd);
	return 0;
}

static int socket_bind(const char* ip,int port)
{
	struct sockaddr_in servaddr;
	int listenfd = socket(AF_INET,SOCK_STREAM,0);
	if(-1 == listenfd)
	{
		perror("socket error:");
		exit(1);
	}
	bzero(&servaddr,sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	inet_pton(AF_INET,ip,&servaddr.sin_addr);
	servaddr.sin_port = htons(port);
	if(-1 == (bind(listenfd,(struct sockaddr*)&servaddr,sizeof(servaddr))))
	{
		perror("bind error:");
		exit(1);
	}
	return listenfd;
}

static void do_epoll(int listenfd)
{
	struct epoll_event events[EPOLLEVENTS];
	int epollfd,ret;
	char buf[MAXSIZE];
	memset(buf,0,MAXSIZE);

	epollfd = epoll_create(FDSIZE);
	add_event(epollfd,listenfd,EPOLLIN);
	for(;;)
	{
		ret = epoll_wait(epollfd,events,EPOLLEVENTS,-1);
		handle_events(epollfd,events,ret,listenfd,buf);
	}
	close(epollfd);
}

static void handle_events(int epollfd,struct epoll_event * events,int num,int listenfd,char *buf)
{
	int i,fd;
	for(i = 0;i < num;++i)
	{
		fd = events[i].data.fd;
		if((fd == listenfd) && (events[i].events & EPOLLIN))
		  handle_accept(epollfd,listenfd);
		else if(events[i].events & EPOLLIN)
		  do_read(epollfd,fd,buf);
		else if(events[i].events & EPOLLOUT)
		  do_write(epollfd,fd,buf);
	}
}

static void handle_accept(int epollfd,int listenfd)
{
	struct sockaddr_in cliaddr;
	socklen_t cliaddrlen = sizeof(cliaddr);
	int clifd = accept(listenfd,(struct sockaddr*)&cliaddr,&cliaddrlen);
	if(-1 == clifd)
	  perror("accept error:");
	else
	{
		printf("accept a new client:%s:%d\n",inet_ntoa(cliaddr.sin_addr),cliaddr.sin_port);
		add_event(epollfd,clifd,EPOLLIN);
	}
}

static void do_read(int epollfd,int fd,char * buf)
{
	int nread = read(fd,buf,MAXSIZE);
	if(-1 == nread)
	{
		perror("read error:");
		close(fd);
		delete_event(epollfd,fd,EPOLLIN);
	}
	else if(0 == nread)
	{
		fprintf(stderr,"client close.\n");
		close(fd);
		delete_event(epollfd,fd,EPOLLIN);
	}
	else
	{
		printf("read message is : %s",buf);
		modify_event(epollfd,fd,EPOLLOUT);
	}
}

static void do_write(int epollfd,int fd,char * buf)
{
	int nwrite = write(fd,buf,strlen(buf));
	if(-1 == nwrite)
	{
		perror("write error:");
		close(fd);
		delete_event(epollfd,fd,EPOLLOUT);
	}
	else
	  modify_event(epollfd,fd,EPOLLIN);
	memset(buf,0,MAXSIZE);
}

static void add_event(int epollfd,int fd,int state)
{
	struct epoll_event ev;
	ev.events = state;
	ev.data.fd = fd;
	epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&ev);

}
static void delete_event(int epollfd,int fd,int state)
{
	struct epoll_event ev;
	ev.events = state;
	ev.data.fd = fd;
	epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,&ev);
}


static void modify_event(int epollfd,int fd,int state)
{
	struct epoll_event ev;
	ev.events = state;
	ev.data.fd = fd;
	epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&ev);
}
