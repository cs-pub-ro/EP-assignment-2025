#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/inet_diag.h>
#include <linux/sock_diag.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/******************************************************************************
 * Useful Macros
 ******************************************************************************/

/* on assertion, exit with -1 */
#define DIE(assertion, msg...) \
    do {                       \
        if (assertion) {       \
            printf(msg);       \
            printf("\n");      \
            exit(-1);          \
        }                      \
    } while(0)

/* on assertion, jump to cleanup label */
#define GOTO(assertion, label, msg...) \
    do {                               \
        if (assertion) {               \
            printf(msg);               \
            printf("\n");              \
            goto label;                \
        }                              \
    } while (0)

/* linux/tcp.h and netinet/tcp.h don't mix well */
#define TCP_ESTABLISHED 1

/******************************************************************************
 * Your work starts here
 ******************************************************************************/

int32_t
main(int32_t argc, char *argv[])
{
    int32_t                 nl_fd;          /* netlink socket              */
    int32_t                 ns_fd;          /* namespace file descriptor   */
    struct msghdr           msg;            /* message chunk integrator    */
    struct nlmsghdr         nlh;            /* netlink header              */
    struct nlmsghdr         *nlh_it;        /* netlink header iterator     */
    struct inet_diag_req_v2 conn_req;       /* netlink diagnostic request  */
    struct sockaddr_nl      sa;             /* socket address              */
    struct iovec            iov[2];         /* buffer aggregators          */
    struct rtattr           *attr;          /* netlink response attributes */
    struct tcp_info         *info;          /* TCP socket stats            */
    struct inet_diag_msg    *diag_msg;      /* netlink diagnositc reponse  */
    uint8_t                 recv_buf[4096]; /* netlink response buffer     */
    size_t                  rta_len;        /* attribute buffer length     */
    ssize_t                 ans;            /* answer                      */
    int32_t                 ret = -1;       /* program return code         */

    /* sanity check */
    DIE(argc != 2, "Usage: ./a.out /proc/*/ns/net");

    /* open network namespace magic link */
    ns_fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    DIE(ns_fd == -1, "failed to open netns magic link (%s)", strerror(errno));

    /* switch network namespace                         *
     * NOTE: must be done before opening netlink socket */
    ans = setns(ns_fd, CLONE_NEWNET);
    GOTO(ans == -1, clean_ns, "failed to swtich netns (%s)", strerror(errno));

    /* open netlink socket diagnostics connection */
    nl_fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_INET_DIAG);
    GOTO(nl_fd == -1, clean_ns, "failed to open netlink socket (%s)",
         strerror(errno));

    /* initial zero-ing of structures */
    memset(&msg,      0, sizeof(msg));
    memset(&sa,       0, sizeof(sa));
    memset(&nlh,      0, sizeof(nlh));
    memset(&conn_req, 0, sizeof(conn_req));

    /* configure request parameters */
    sa.nl_family = AF_NETLINK;                        /* socket protocol     */

    conn_req.sdiag_family   = AF_INET;                /* target addr family  */
    conn_req.sdiag_protocol = IPPROTO_TCP;            /* target protocol     */
    conn_req.idiag_states   = 1 << TCP_ESTABLISHED;   /* filter sock states  */

    /* TODO 1: get tcp_info as extended information */
    conn_req.idiag_ext      = 0;

    nlh.nlmsg_len   = NLMSG_LENGTH(sizeof(conn_req)); /* message length      */
    nlh.nlmsg_type  = SOCK_DIAG_BY_FAMILY;            /* message type        */
    nlh.nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST;     /* request diag list   */

    /* TODO 2: initialize socket filtering criteria */
    conn_req.id.idiag_src[0] = 0;                     /* src ip addr filter  */
    conn_req.id.idiag_dst[0] = 0;                     /* dst ip addr filter  */
    conn_req.id.idiag_sport  = htons(0);              /* src port filter     */
    conn_req.id.idiag_dport  = htons(0);              /* dst port filter     */

    iov[0].iov_base = (void *) &nlh;                  /* include nl header   */
    iov[0].iov_len  = sizeof(nlh);                    /* nl header size      */
    iov[1].iov_base = (void *) &conn_req;             /* include payload     */
    iov[1].iov_len  = sizeof(conn_req);               /* payload size        */

    msg.msg_name    = (void*) &sa;                    /* sock address        */
    msg.msg_namelen = sizeof(sa);                     /* length of sock addr */
    msg.msg_iov     = iov;                            /* message segments    */
    msg.msg_iovlen  = 2;                              /* number of segments  */

    /* send socket diagnostic request */
    ans = sendmsg(nl_fd, &msg, 0);
    GOTO(ans == -1, clean_nl, "unable to send socket diagnostics request (%s)",
         strerror(errno));

    /* wait for response (can come in multiple instances) */
    while (1) {
        ans = recv(nl_fd, recv_buf, sizeof(recv_buf), 0);
        GOTO(ans == -1, clean_nl, "unable to receive diagnostic data (%s)",
             strerror(errno));

        /* for all parts of the full message */
        nlh_it = (struct nlmsghdr *) recv_buf;
        while(NLMSG_OK(nlh_it, ans)) {
            /* check for error or response end */
            GOTO(nlh_it->nlmsg_type == NLMSG_ERROR, clean_nl, "NLMSG_ERROR");
            if (nlh_it->nlmsg_type == NLMSG_DONE)
                goto out;

            /* extract payload from current message */
            diag_msg = (struct inet_diag_msg *) NLMSG_DATA(nlh_it);

            printf("=================================\n");
            printf("sport  : %hu\n", ntohs(diag_msg->id.idiag_sport));
            printf("dport  : %hu\n", ntohs(diag_msg->id.idiag_dport));
            printf("src ip : %u.%u.%u.%u\n",
                (diag_msg->id.idiag_src[0] >>  0) & 0xff,
                (diag_msg->id.idiag_src[0] >>  8) & 0xff,
                (diag_msg->id.idiag_src[0] >> 16) & 0xff,
                (diag_msg->id.idiag_src[0] >> 24) & 0xff);
            printf("dst ip : %u.%u.%u.%u\n",
                (diag_msg->id.idiag_dst[0] >>  0) & 0xff,
                (diag_msg->id.idiag_dst[0] >>  8) & 0xff,
                (diag_msg->id.idiag_dst[0] >> 16) & 0xff,
                (diag_msg->id.idiag_dst[0] >> 24) & 0xff);
            printf("inode  : %u\n", diag_msg->idiag_inode);

            /* TODO 3: iterate over attributes in search for tcp_info */

            /* skip to next message encapsulated in response */
            nlh_it = NLMSG_NEXT(nlh_it, ans);
        }
    }

out:
    /* success */
    ret = 0;

clean_nl:
    close(nl_fd);

clean_ns:
    close(ns_fd);

    return ret;
}
