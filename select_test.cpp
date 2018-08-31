#include <sys/time.h>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
using namespace std;

int main ()
{

    int keyboard,ret;
    char c = 0;
    fd_set readfd;
    keyboard = open("/dev/tty",O_RDONLY);//当前终端

    if(-1 == keyboard)
	{
		cout<<"open fail:"<<strerror(errno)<<endl;
	}

	bool flag = true;
    while(flag)
    {
        FD_ZERO(&readfd);
        FD_SET(keyboard,&readfd);
 
        ///监控函数
        ret = select(keyboard + 1,&readfd,NULL,NULL,NULL);
        if(ret == -1)   //错误情况
		{
            cout<<"error"<<endl ;
		}
        else if(ret)    //返回值大于0 有数据到来
		{
            if(FD_ISSET(keyboard,&readfd))
            {
                while(read(keyboard,&c,1) > 0)
				{
					if('\n' == c)
						break;
					if ('q' == c)
					{
						while(read(keyboard,&c,1) && c != '\n');
						flag = false;
						break;
					}
					printf("the input is %c\n",c);
				}
            }
		}
    }
	return 0;
}

