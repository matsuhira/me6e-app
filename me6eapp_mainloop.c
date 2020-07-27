/*****************************************************************************/
/* ファイル名 : me6eapp_mainloop.c                                           */
/* 機能概要   : メインループクラス ソースファイル                            */
/* 修正履歴   : 2013.01.24 Y.Shibata 新規作成                                */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                   */
/*            : 2016.05.27 M.Kawano     PRのコマンド機能の追加               */
/*                                                                           */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016               */
/*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <sched.h>
#include <alloca.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/signalfd.h>
#include <sys/epoll.h>

#include "me6eapp.h"
#include "me6eapp_mainloop.h"
#include "me6eapp_statistics.h"
#include "me6eapp_log.h"
#include "me6eapp_socket.h"
#include "me6eapp_command.h"
#include "me6eapp_util.h"
#include "me6eapp_ProxyArp.h"
#include "me6eapp_ProxyNdp.h"

#include "me6eapp_pr.h"

// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif


////////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
static inline bool command_handler(int fd, struct me6e_handler_t* handler);
static inline bool signal_handler(int fd, struct me6e_handler_t* handler);


///////////////////////////////////////////////////////////////////////////////
//! @brief コマンド＆シグナル受信用のメインループ
//!
//! @param [in] handler ME6Eハンドラ
//!
//! @retval 0      正常終了
//! @retval 0以外  異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_mainloop(struct me6e_handler_t* handler)
{
    int                 epfd;
    int                 loop, num;
    struct epoll_event  ev, ev_ret[RECV_NEVENT_NUM];
    int                 command_fd;
    char                path[sizeof(((struct sockaddr_un*)0)->sun_path)] = {0};
    char*               offset = &path[1];

    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_mainloop).");
        return -1;
    }

    sprintf(offset, ME6E_COMMAND_SOCK_NAME, handler->conf->common->plane_name);

    command_fd = socket(PF_UNIX, SOCK_SEQPACKET, 0);
    if(command_fd < 0){
        me6e_logging(LOG_ERR, "fail to create command socket : %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));

    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, path, sizeof(addr.sun_path));

    if(bind(command_fd, (struct sockaddr*)&addr, sizeof(addr))){
        me6e_logging(LOG_ERR, "fail to bind command socket : %s.", strerror(errno));
        close(command_fd);
        return -1;
    }

    if(listen(command_fd, 100)){
        me6e_logging(LOG_ERR, "fail to listen command socket : %s.", strerror(errno));
        close(command_fd);
        return -1;
    }

    // epollの生成
    epfd = epoll_create(RECV_NEVENT_NUM);
    if (epfd < 0) {
        me6e_logging(LOG_ERR, "fail to create epoll main : %s." , strerror(errno));
        return -1;
    }

    // epollへ登録
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = command_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, command_fd, &ev) != 0) {
        me6e_logging(LOG_ERR, "fail to control epoll main : %s.", strerror(errno));
        return -1;
    }

    // epollへ登録
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = handler->signalfd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, handler->signalfd, &ev) != 0) {
        me6e_logging(LOG_ERR, "fail to control epoll.");
        return -1;
    }

    DEBUG_LOG("mainloop start");
    while(1){
        // 受信待ち
        num = epoll_wait(epfd, ev_ret, RECV_NEVENT_NUM, -1);

        if(num < 0){
            if(errno == EINTR){
                // シグナル割込みの場合は処理継続
                me6e_logging(LOG_INFO, "mainloop receive signal.");
                continue;
            }
            else{
                me6e_logging(LOG_ERR, "mainloop receive error : %s.", strerror(errno));
                break;;
            }
        }

        for (loop = 0; loop < num; loop++) {
            if (ev_ret[loop].data.fd == command_fd) {
                DEBUG_LOG("command receive\n");
                if(!command_handler(command_fd, handler)){
                    // ハンドラの戻り値がfalseの場合はループを抜ける
                    goto FINISH;    // 多重ループを抜けるためgotoを使用
                }
            } else if(ev_ret[loop].data.fd == handler->signalfd) {
                DEBUG_LOG("signal receive\n");
                if(!signal_handler(handler->signalfd, handler)){
                    // ハンドラの戻り値がfalseの場合はループを抜ける
                    goto FINISH;    // 多重ループを抜けるためgotoを使用
                }
            } else {
                me6e_logging(LOG_ERR, "unknown fd = %d.", ev_ret[loop].data.fd);
                me6e_logging(LOG_ERR, "command_fd = %d.", command_fd);
                me6e_logging(LOG_ERR, "signalfd = %d.", signalfd);
            }
        }
    }

FINISH:
    DEBUG_LOG("mainloop end\n");
    close(command_fd);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 外部コマンドハンドラ
//!
//! 外部コマンドからの要求受信時に呼ばれるハンドラ。
//!
//! @param [in] fd      コマンドを受信したソケットのディスクリプタ
//! @param [in] handler ME6Eハンドラ
//!
//! @retval true   メインループを継続する
//! @retval false  メインループを継続しない
///////////////////////////////////////////////////////////////////////////////
static inline bool command_handler(int fd, struct me6e_handler_t* handler)
{
    struct me6e_command_t command;
    int ret;
    int sock;

    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(command_handler).");
        return true;
    }

    sock = accept(fd, NULL, 0);
    if(sock <= 0){
        return true;
    }
    DEBUG_LOG("accept ok\n");

    if(fcntl(sock, F_SETFD, FD_CLOEXEC)){
        me6e_logging(LOG_ERR, "fail to set close-on-exec flag : %s.", strerror(errno));
        close(sock);
        return true;
    }

    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_PASSCRED, &opt, sizeof(opt))) {
        me6e_logging(LOG_ERR, "fail to set sockopt SO_PASSCRED : %s.", strerror(errno));
        close(sock);
        return true;
    }

    ret = me6e_socket_recv_cred(sock, &command.code, &command.req, sizeof(command.req));
    DEBUG_LOG("command receive. code = %d,ret = %d.", command.code, ret);

    switch(command.code){
    case ME6E_SHOW_STATISTIC:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = me6e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            me6e_logging(LOG_WARNING, "fail to send response to external command : %s.", strerror(-ret));
        }
        if(command.res.result == 0){
            me6e_printf_statistics_info(handler->stat_info, sock);
        }
        break;

    case ME6E_SHOW_CONF:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = me6e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            me6e_logging(LOG_WARNING, "fail to send response to external command : %s.", strerror(-ret));
        }
        if(command.res.result == 0){
            me6e_config_dump(handler->conf, sock);
        }
        break;

    case ME6E_SHOW_ARP:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = me6e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            me6e_logging(LOG_WARNING, "fail to send response to external command : %s.", strerror(-ret));
        }
        if(command.res.result == 0){
            if (handler->conf->arp->arp_enable) {
                ProxyArp_print_table(handler->proxy_arp_handler, sock);
            } else {
                dprintf(fd, "Proxy ARP disable...\n");
            }
        }
        break;

    case ME6E_SHOW_NDP:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = me6e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            me6e_logging(LOG_WARNING, "fail to send response to external command : %s.", strerror(-ret));
        }
        if(command.res.result == 0){
            if (handler->conf->ndp->ndp_enable) {
                ProxyNdp_print_table(handler->proxy_ndp_handler, sock);
            } else {
                dprintf(fd, "Proxy NDP disable...\n");
            }
        }
        break;

    case ME6E_SHUTDOWN:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = me6e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            me6e_logging(LOG_WARNING, "fail to send response to external command : %s.", strerror(-ret));
        }
        if(command.res.result == 0){
            // コマンドループを抜ける
            return false;
        }
        break;

    case ME6E_ADD_ARP:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = me6e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            me6e_logging(LOG_WARNING, "fail to send response to external command : %s.", strerror(-ret));
        }
        if(command.res.result == 0){
            if (handler->conf->arp->arp_enable) {
                ProxyArp_cmd_add_static_entry(
                            handler->proxy_arp_handler,
                            command.req.arp.ipv4addr,
                            command.req.arp.macaddr,
                            sock);
                ProxyArp_print_table(handler->proxy_arp_handler, sock);
            } else {
                dprintf(fd, "Proxy ARP disable...\n");
            }
        }
        break;

    case ME6E_DEL_ARP:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = me6e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            me6e_logging(LOG_WARNING, "fail to send response to external command : %s.", strerror(-ret));
        }
        if(command.res.result == 0){
            if (handler->conf->arp->arp_enable) {
                ProxyArp_cmd_del_static_entry(
                            handler->proxy_arp_handler,
                            command.req.arp.ipv4addr,
                            sock);
                ProxyArp_print_table(handler->proxy_arp_handler, sock);
            } else {
                dprintf(fd, "Proxy ARP disable...\n");
            }
        }
        break;

    case ME6E_ADD_NDP:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = me6e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            me6e_logging(LOG_WARNING, "fail to send response to external command : %s.", strerror(-ret));
        }
        if(command.res.result == 0){
            if (handler->conf->ndp->ndp_enable) {
                ProxyNdp_cmd_add_static_entry(
                            handler->proxy_ndp_handler,
                            command.req.ndp.ipv6addr,
                            command.req.ndp.macaddr,
                            handler->conf->capsuling->stub_physical_dev,
                            sock);
                ProxyNdp_print_table(handler->proxy_ndp_handler, sock);
            } else {
                dprintf(fd, "Proxy NDP disable...\n");
            }
        }
        break;

    case ME6E_DEL_NDP:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = me6e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            me6e_logging(LOG_WARNING, "fail to send response to external command : %s.", strerror(-ret));
        }
        if(command.res.result == 0){
            if (handler->conf->ndp->ndp_enable) {
                ProxyNdp_cmd_del_static_entry(
                            handler->proxy_ndp_handler,
                            command.req.ndp.ipv6addr,
                            handler->conf->capsuling->stub_physical_dev,
                            sock);
                ProxyNdp_print_table(handler->proxy_ndp_handler, sock);
            } else {
                dprintf(fd, "Proxy NDP disable...\n");
            }
        }
        break;

    case ME6E_SHOW_PR:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = me6e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            me6e_logging(LOG_WARNING, "fail to send response to external command : %s.", strerror(-ret));
        }
        else {
            //動作モードチェック
            if(handler->conf->common->tunnel_mode != ME6E_TUNNEL_MODE_PR){
               me6e_pr_print_error(sock, ME6E_PR_COMMAND_MODE_ERROR);
               break;
            }
        }
        if(command.res.result == 0){
            me6e_pr_show_entry_pr_table(handler->pr_handler, sock, handler->conf->capsuling->plane_id);
        }
        break;

    case ME6E_ADD_PR:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = me6e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            me6e_logging(LOG_WARNING, "fail to send response to external command : %s.", strerror(-ret));
        }
        else {
            //動作モードチェック
            if(handler->conf->common->tunnel_mode != ME6E_TUNNEL_MODE_PR){
               me6e_pr_print_error(sock, ME6E_PR_COMMAND_MODE_ERROR);
               break;
            }
        }
        if(command.res.result == 0){
            if(!me6e_pr_add_entry_pr_table(handler, &command.req)) {
                // エントリ登録失敗
                me6e_logging(LOG_ERR,"fail to add ME6E-PR Entry to ME6E-PR Table\n");
            }
           // PR画面表示に時間がかかる為、首絞め
           // me6e_pr_show_entry_pr_table(handler->pr_handler, sock, handler->conf->capsuling->plane_id);
            close(sock);
        }
        break;

    case ME6E_DEL_PR:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = me6e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            me6e_logging(LOG_WARNING, "fail to send response to external command : %s.", strerror(-ret));
        }
        else {
            //動作モードチェック
            if(handler->conf->common->tunnel_mode != ME6E_TUNNEL_MODE_PR){
               me6e_pr_print_error(sock, ME6E_PR_COMMAND_MODE_ERROR);
               break;
            }
        }
        if(command.res.result == 0){
            if(!me6e_pr_del_entry_pr_table(handler, &command.req)) {
                // エントリ削除失敗
                me6e_logging(LOG_ERR,"fail to delete ME6E-PR Entry from ME6E-PR Table\n");
            }
            //PR画面表示に時間がかかる為、首絞め
            //me6e_pr_show_entry_pr_table(handler->pr_handler, sock, handler->conf->capsuling->plane_id);
            close(sock);
        }
        break;

    case ME6E_ENABLE_PR:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = me6e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            me6e_logging(LOG_WARNING, "fail to send response to external command : %s.", strerror(-ret));
        }
        else {
            //動作モードチェック
            if(handler->conf->common->tunnel_mode != ME6E_TUNNEL_MODE_PR){
               me6e_pr_print_error(sock, ME6E_PR_COMMAND_MODE_ERROR);
               break;
            }
        }
        if(command.res.result == 0){
            if(!me6e_pr_enable_entry_pr_table(handler, &command.req)) {
                // エントリ活性化失敗
                me6e_logging(LOG_ERR,"fail to enable ME6E-PR Entry in ME6E-PR Table\n");
            }
            //PR画面表示に時間がかかる為、首絞め
            //me6e_pr_show_entry_pr_table(handler->pr_handler, sock, handler->conf->capsuling->plane_id);
            close(sock);
        }
        break;

    case ME6E_DISABLE_PR:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = me6e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            me6e_logging(LOG_WARNING, "fail to send response to external command : %s.", strerror(-ret));
        }
        else {
            //動作モードチェック
            if(handler->conf->common->tunnel_mode != ME6E_TUNNEL_MODE_PR){
               me6e_pr_print_error(sock, ME6E_PR_COMMAND_MODE_ERROR);
               break;
            }
        }
        if(command.res.result == 0){
            if(!me6e_pr_disable_entry_pr_table(handler, &command.req)) {
                // エントリ非活性化失敗
                me6e_logging(LOG_ERR,"fail to disable ME6E-PR Entry in ME6E-PR Table\n");
            }
            //PR画面表示に時間がかかる為、首絞め
            //me6e_pr_show_entry_pr_table(handler->pr_handler, sock, handler->conf->capsuling->plane_id);
            close(sock);
        }
        break;

    case ME6E_DELALL_PR:
        if(ret > 0){
            command.res.result = 0;
        }
        else{
            command.res.result = -ret;
        }
        ret = me6e_socket_send(sock, command.code, &command.res, sizeof(command.res), -1);
        if(ret < 0){
            me6e_logging(LOG_WARNING, "fail to send response to external command : %s.", strerror(-ret));
        }
        else {
            //動作モードチェック
            if(handler->conf->common->tunnel_mode != ME6E_TUNNEL_MODE_PR){
               me6e_pr_print_error(sock, ME6E_PR_COMMAND_MODE_ERROR);
               break;
            }
        }
        if(command.res.result == 0){
            if(!me6e_pr_delall_entry_pr_table(handler, &command.req)) {
                // エントリ全削除失敗
                me6e_logging(LOG_ERR,"fail to disable ME6E-PR Entry in ME6E-PR Table\n");
            }
            me6e_pr_show_entry_pr_table(handler->pr_handler, sock, handler->conf->capsuling->plane_id);
            close(sock);
        }
        break;

    case ME6E_LOAD_PR:
        break;

    default:
        break;
    }

    close(sock);

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Backboneネットワーク用シグナルハンドラ
//!
//! シグナル受信時に呼ばれるハンドラ。
//!
//! @param [in] fd      シグナルを受信したディスクリプタ
//! @param [in] handler ME6Eハンドラ
//!
//! @retval true   メインループを継続する
//! @retval false  メインループを継続しない
///////////////////////////////////////////////////////////////////////////////
static inline bool signal_handler(int fd, struct me6e_handler_t* handler)
{
    struct signalfd_siginfo siginfo;
    int                     ret;
    bool                    result;
    pid_t                   pid;

    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(signal_handler).");
        return true;
    }

    ret = read(fd, &siginfo, sizeof(siginfo));
    if (ret < 0) {
        me6e_logging(LOG_ERR, "failed to read signal info.");
        return true;
    }

    if (ret != sizeof(siginfo)) {
        me6e_logging(LOG_ERR, "unexpected siginfo size.");
        return true;
    }

    DEBUG_LOG("---------------------------\n");
    DEBUG_LOG("シグナル番号   : %d\n", siginfo.ssi_signo);
    DEBUG_LOG("シグナルコード : %d\n", siginfo.ssi_code);
    DEBUG_LOG("送信元の PID   : %d\n", siginfo.ssi_pid);
    DEBUG_LOG("送信元の実 UID : %d\n", siginfo.ssi_uid);
    DEBUG_LOG("---------------------------\n");

    switch(siginfo.ssi_signo){
    case SIGCHLD:
        DEBUG_LOG("signal %d catch. waiting for child process.\n", siginfo.ssi_signo);
        do{
            pid = waitpid(-1, &ret, WNOHANG);
            DEBUG_LOG("child process end. pid=%d, status=%d\n", pid, WEXITSTATUS(ret));
        } while(pid > 0);

        result = true;
        break;

    case SIGINT:
    case SIGTERM:
    case SIGQUIT:
    case SIGHUP:
        DEBUG_LOG("signal %d catch. finish process.\n", siginfo.ssi_signo);
        result = false;
        break;

    default:
        DEBUG_LOG("signal %d catch. ignore...\n", siginfo.ssi_signo);
        result = true;
        break;
    }

    return result;
}

