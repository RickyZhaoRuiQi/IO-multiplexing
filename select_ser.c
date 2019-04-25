/*
 
int select(int nfds,fd_set* readfds,fd_set* writefds,fd_set* exceptfds,struct timeval* timeout);

(1):nfds参数指定被监听文件描述符总数。它通常被设置为select监听的所有文件描述符中的最大值加1，因为
文件描述符从0开始计数，这样就可以囊括所有文件描述符
(2):readfds、writefds和exceptfds参数分别指向可读、可写和异常事件对应的文件描述符集合。
(3):timeout参数用来设置select函数的超时时间。
(4):返回值大于0代表就绪文件描述符数量，等于0代表超时，小于0代表出错

FD_ZERO(fd_set *fdset);				清除fdset的所有位
FD_SET(int fd,fd_set *fdset);			设置fdset的位fd
FD_CLR(int fd,fd_set *fdser);			清除fdset的位fd
int FD_ISSET(int fd,fd_set *fdset);		测试fdset的位fd是否被设置

理解select模型：

理解select模型的关键在于理解fd_set,为说明方便，取fd_set长度为1字节，fd_set中的每一bit可以对应一个文件描
述符fd。则1字节长的fd_set最大可以对应8个fd。

（1）执行fd_set set; FD_ZERO(&set);则set用位表示是0000,0000。

（2）若fd＝5,执行FD_SET(fd,&set);后set变为0010,0000(第6位置为1)

（3）若再加入fd＝2，fd=1,则set变为0010,0110

（4）执行select(6,&set,0,0,0)阻塞等待

（5）若fd=1,fd=2上都发生可读事件，则select返回，此时set变为0000,0110。注意：没有事件发生的fd=5被清空。

select模型的特点：

（1)可监控的文件描述符个数取决于sizeof(fd_set)的值。我这虚拟机上sizeof(fd_set)＝128，每bit表示一个文
件描述符，则我虚拟机上支持的最大文件描述符是128*8=1024。据说可调，另有说虽然可调，但调整上限受于编译内
核时的变量值。

（2）将fd加入select监控集的同时，还要再使用一个数据结构array保存放到select监控集中的fd，一是用于在
select返回后，array作为源数据和fd_set进行FD_ISSET判断。二是select返回后会把以前加入的但并无事件发生的fd
清空，则每次开始select前都要重新从array取得fd逐一加入（FD_ZERO最先），扫描array的同时取得fd最大值maxfd
，用于select的第一个参数。

（3）可见select模型必须在select前循环array（加fd，取maxfd），select返回后循环array（FD_ISSET判断是
否有事件发生）。


select缺点

（1）每次调用select，都需要把fd集合从用户态拷贝到内核态，这个开销在fd很多时会很大

（2）同时每次调用select都需要在内核中遍历传递进来的所有fd，这个开销在fd很多时也很大

（3）select支持的文件描述符数量有限，默认是1024

*/




//tcp回射程序，程序的功能是：客户端向服务器发送信息，服务器接收并原样发送给客户端，客户端显示出接
//收到的信息，来源http://www.cnblogs.com/Anker/archive/2013/08/14/3258674.html作为学习之用

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <assert.h>

#define IPADDR		"127.0.0.1"
#define PORT		6000
#define MAXLINE		1024
#define LISTENQ		5
#define SIZE		10

typedef struct server_context_st
{
	int cli_cnt;		//客户端个数
	int clifds[SIZE];	//客户端的个数
	fd_set allfds;		//句柄个数
	int maxfd;			//句柄最大值
}server_context_st;

static server_context_st *s_srv_ctx = NULL;

static int create_server_proc(const char* ip,int port)
{
	struct sockaddr_in servaddr;
	int fd = socket(AF_INET,SOCK_STREAM,0);
	if(-1 == fd)
	{
		printf("create socket fail,errno:%d,reason:%s\n",errno,strerror(errno));
		return -1;
	}

	/*一个端口释放后会等待两分钟之后才能再被使用，SO_REUSEADDR是让端口释放后立即就可以被再次使用。*/
	int reuse = 1;
	if(setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse)) == -1)
	  return -1;

	bzero(&servaddr,sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	inet_pton(AF_INET,ip,&servaddr.sin_addr);
	servaddr.sin_port = htons(port);

	if(bind(fd,(struct sockaddr*)&servaddr,sizeof(servaddr)) == -1)
	{
		perror("bind error: ");
		return -1;
	}

	listen(fd,LISTENQ);

	return fd;
}

static int accept_client_proc(int sevfd)
{
	struct sockaddr_in cliaddr;
	socklen_t cliaddrlen = sizeof(cliaddr);
	int clifd = -1;

	printf("accept client proc is called.\n");

ACCEPT:
	clifd = accept(sevfd,(struct sockaddr*)&cliaddr,&cliaddrlen);
	if(-1 == clifd)
	{
		if(errno == EINTR)
		  goto ACCEPT;
		else
		{
			printf("accept fail,errno:%s\n",strerror(errno));
		}
	}

	printf("accept a new client:%s:%d\n",inet_ntoa(cliaddr.sin_addr),cliaddr.sin_port);

	//将新的连接描述符添加到数组中
	int i = 0;
	for(;i < SIZE;++i)
	{
		if(s_srv_ctx->clifds[i] < 0)
		{
			s_srv_ctx->clifds[i] = clifd;
			s_srv_ctx->cli_cnt++;
			break;
		}
	}

	if(i == SIZE)
	{
		printf("too many clients.\n");
		return -1;
	}
}

static int handle_client_msg(int fd,char *buf)
{
	assert(buf);
	printf("recv buf is :%s\n",buf);
	write(fd,buf,strlen(buf) + 1);
	return 0;
}

static void recv_client_msg(fd_set *readfds)
{
	int i = 0,n = 0;
	int clifd;
	char buf[MAXLINE] = {0};
	for(i = 0;i <= s_srv_ctx->cli_cnt;++i)
	{
		clifd = s_srv_ctx->clifds[i];
		if(clifd < 0)
		  continue;
		if(FD_ISSET(clifd,readfds))
		{
			n = read(clifd,buf,MAXLINE);
			if(n <= 0)
			{
				FD_CLR(clifd,&s_srv_ctx->allfds);
				close(clifd);
				s_srv_ctx->clifds[i] = -1;
				continue;
			}
			handle_client_msg(clifd,buf);
		}
	}
}

static void handle_client_proc(int srvfd)
{
	int clifd = -1;
	int retval = 0;
	fd_set *readfds = &s_srv_ctx->allfds;
	struct timeval tv;
	int i = 0;

	while(1)
	{
		//每次调用select前都要重新设置文件描述符和时间，因为时间发生后，文件描述符和时间都被内核修改了
		FD_ZERO(readfds);
		//添加监听套接字
		FD_SET(srvfd,readfds);
		s_srv_ctx->maxfd = srvfd;

		tv.tv_sec = 30;
		tv.tv_usec = 0;

		//添加客户端套接字
		for(i = 0;i < s_srv_ctx->cli_cnt;++i)
		{
			clifd = s_srv_ctx->clifds[i];
			//去除无效的客户端句柄
			if(-1 != clifd)
			  FD_SET(clifd,readfds);
			s_srv_ctx->maxfd = (clifd > s_srv_ctx->maxfd ? clifd : s_srv_ctx->maxfd);
		}

		//开始轮询接收处理服务端和客户端套接字

		retval = select(s_srv_ctx->maxfd + 1,readfds,NULL,NULL,&tv);
		if(-1 == retval)
		{
			printf("select error:%s.\n",strerror(errno));
			continue;
		}
		if(FD_ISSET(srvfd,readfds))
		  accept_client_proc(srvfd);
		else
		  recv_client_msg(readfds);
	}
}

static void server_uninit()
{
	if(s_srv_ctx)
	{
		free(s_srv_ctx);
		s_srv_ctx = NULL;
	}
}

static int server_init()
{
	s_srv_ctx = (server_context_st *)malloc(sizeof(server_context_st));
	if(NULL == s_srv_ctx)
	  return -1;

	memset(s_srv_ctx,0,sizeof(server_context_st));

	int i = 0;
	for(;i < SIZE;++i)
	  s_srv_ctx->clifds[i] = -1;

	return 0;
}

int main()
{
	int srvfd;
	if(server_init() < 0)
	  return -1;

	srvfd = create_server_proc(IPADDR,PORT);
	if(srvfd < 0)
	{
		printf("socket create or bind fail.\n");
		goto err;
	}
	handle_client_proc(srvfd);
	server_uninit();
	return 0;
err:
	server_uninit();
	return 0;
}
