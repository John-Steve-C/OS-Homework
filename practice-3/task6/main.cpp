#include <net/if.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
 
 
using namespace std;
 
int main()
{	
	//得到套接字描述符
	int sockfd;		

    /* code */
	// ioctl(sockfd, _IO('c', 0));
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	//AF_INET表示ipv4，SOCK_STREAM表示TCP协议，0表示默认协议

	struct ifconf ifc;
	caddr_t buf;
	int len = 100;
	
	//初始化ifconf结构
	ifc.ifc_len = 1024;
	if ((buf = (caddr_t)malloc(1024)) == NULL)
	{
		cout << "malloc error" << endl;
		exit(-1);
	}
	ifc.ifc_buf = buf; 
	
	//获取所有接口信息
	
    /* code */
	ioctl(sockfd, SIOCGIFCONF, &ifc);	// 获取网络接口列表
	
	//遍历每一个ifreq结构
	struct ifreq *ifr;
	struct ifreq ifrcopy;
	ifr = (struct ifreq*)buf;
	for(int i = (ifc.ifc_len/sizeof(struct ifreq)); i>0; i--)
	{
		//接口名
		cout << "interface name: "<< ifr->ifr_name << endl;
		//ipv4地址
		cout << "inet addr: " 
			 << inet_ntoa(((struct sockaddr_in*)&(ifr->ifr_addr))->sin_addr)
			 << endl;
		
		//获取广播地址
		ifrcopy = *ifr;

		/* code */
		ioctl(sockfd, SIOCGIFBRDADDR, &ifrcopy);

		cout << "broad addr: "
			 << inet_ntoa(((struct sockaddr_in*)&(ifrcopy.ifr_addr))->sin_addr)
			 << endl;
		//获取mtu：Maximum Transmission Unit 最大传输单元
		ifrcopy = *ifr;
		
        /* code */
		ioctl(sockfd, SIOCGIFMTU, &ifrcopy);

		cout << "mtu: " << ifrcopy.ifr_mtu << endl;
		cout << endl;
		ifr++;
	}
	
	return 0;
}