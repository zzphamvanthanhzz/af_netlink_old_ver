/* 
 * File:   main.cpp
 * Author: thanhpv
 *
 * Created on October 20, 2015, 9:58 PM
 */

#include <cstdlib>
#include <unistd.h>
#include <string.h>//for memset
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
#include <linux/socket.h>
#include <linux/unix_diag.h>

#include "ss_common.h"


#define SOCKET_BUFFER_SIZE (getpagesize() < 8192L ? getpagesize() : 8192L)
#define SS_ALL ((1<<SS_MAX)-1)
using namespace std;
static int tcpConnEstaplished, tcpConnFinWait, tcpConnTimeWait, tcpConnCloseWait;

/*
 * 
 */
void showbits(unsigned int x) {
	int i;
	for (int i = (sizeof (int)*8) / 2; i >= 0; i--) {
		(x & (1 << i)) ? putchar('1') : putchar('0');
	}
	printf("\n");
}

int sockdiag_send(int family, int fd, int protocol, struct filter *f) {
	struct sockaddr_nl nladdr;

	struct {
		struct nlmsghdr nlh;
		struct inet_diag_req r;
	} req;
	struct msghdr msg;
	struct iovec iov[3];


	memset(&nladdr, 0, sizeof (nladdr));
	nladdr.nl_family = AF_NETLINK;

	req.nlh.nlmsg_len = sizeof (req);
	req.nlh.nlmsg_type = TCPDIAG_GETSOCK;
	req.nlh.nlmsg_flags = NLM_F_ROOT | NLM_F_MATCH | NLM_F_REQUEST;
	req.nlh.nlmsg_pid = 0;
	req.nlh.nlmsg_seq = 123456;
	memset(&req.r, 0, sizeof (req.r));
	req.r.idiag_family = family;
	//	req.r.idiag_protocol = protocol;
	req.r.idiag_states = f->states;

	iov[0] = (struct iovec){&req, sizeof (req)};

	msg = (struct msghdr){&nladdr, sizeof (nladdr), iov, f->f ? 3 : 1, NULL, 0, 0};

	if (sendmsg(fd, &msg, 0) < 0) {
		printf("Error in sendmsg %s", strerror(errno));
		return -1;
	}
	return 0;
}

bool GetTCPState(int32_t family, int32_t fd) {

	int32_t protocol = IPPROTO_TCP;
	struct filter current_filter = (struct filter){~0, SS_ALL & ~((1 << SS_LISTEN) | (1 << SS_CLOSE) | (1 << SS_SYN_RECV)), (1 << AF_INET) | (1 << AF_INET6), NULL};
	int32_t send_af_sts = sockdiag_send(family, fd, protocol, &current_filter);
	if (send_af_sts) {
		printf("Error inside sockdiag_send %s", strerror(errno));
		return false;
	}

	struct iovec iov[3];
	char buf[SOCKET_BUFFER_SIZE];
	struct sockaddr_nl nladdr;
	struct msghdr msg;
	memset(&nladdr, 0, sizeof (nladdr));
	nladdr.nl_family = family;
	iov[0] = (struct iovec){buf, sizeof (buf)};
	while (1) {
		int status;
		struct nlmsghdr *h;
		msg = (struct msghdr){
			(void*) &nladdr, sizeof (nladdr),
			iov, 1,
			NULL, 0,
			0
		};

		status = recvmsg(fd, &msg, 0);
		if (status < 0) {
			if (errno == EINTR)
				continue;
			printf("OVERRUN\n");
			continue;
		}
		if (status == 0) {
			//printf("EOF on netlink\n");
			return true;
		}
		h = (struct nlmsghdr*) buf;
		while (NLMSG_OK(h, status)) {
			struct inet_diag_msg *r = (struct inet_diag_msg *) NLMSG_DATA(h);

			if (h->nlmsg_seq != 123456) {
				h = NLMSG_NEXT(h, status);
				continue;
			}
			if (h->nlmsg_type == NLMSG_DONE) {
				return true;
			}
			if (h->nlmsg_type == NLMSG_ERROR) {
				struct nlmsgerr *err = (struct nlmsgerr*) NLMSG_DATA(h);
				if (h->nlmsg_len < NLMSG_LENGTH(sizeof (struct nlmsgerr))) {
					printf("Message truncated\n");
				} else {
					errno = -err->error;
					printf("Error in message %s\n", strerror(errno));
					return false;
				}
				return true;
			}

			//			if (r->idiag_family == PF_INET6 || r->idiag_family == PF_INET)
			switch (r->idiag_state) {
				case 1:
					tcpConnEstaplished++;
					break;
				case 4:
				case 5:
					tcpConnFinWait++;
					break;
				case 6:
					tcpConnTimeWait++;
					break;
				case 8:
					tcpConnCloseWait++;
					break;
			}
			h = NLMSG_NEXT(h, status);
		}
	}
	return true;
}

int main(int argc, char** argv) {
	while (1) {
		tcpConnEstaplished = tcpConnFinWait = tcpConnTimeWait = tcpConnCloseWait = 0;
		int family = PF_INET; //same as PF_INET
		int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_INET_DIAG);
		if (fd < 0) {
			printf("Error open netlink socket: %s\n", strerror(errno));
			return false;
		}
		bool rs = GetTCPState(family, fd);
		//	printf("est: %d\n twait: %d \n cwait: %d \n fwait: %d\n", _networkstat->tcpConnEstaplished,
		//			_networkstat->tcpConnTimeWait, _networkstat->tcpConnCloseWait, _networkstat->tcpConnFinWait);
		close(fd);

		printf("tcpConnEstaplished: %d\t tcpConnFinWait:%d\t tcpConnTimeWait:%d\t  tcpConnCloseWait: %d\n",
				tcpConnEstaplished, tcpConnFinWait, tcpConnTimeWait, tcpConnCloseWait);
		sleep(1);
	}
	return 0;
}

