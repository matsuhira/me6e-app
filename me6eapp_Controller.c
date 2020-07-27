/******************************************************************************/
/* ファイル名 : me6eapp_Controller.c                                          */
/* 機能概要   : パケット処理クラスを制御するクラス ソースファイル             */
/* 修正履歴   : 2013.01.10 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*            : 2016.06.01 S.Anai PR機能の追加                                */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <limits.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/if_ether.h>
#include <netpacket/packet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/epoll.h>


#include "me6eapp.h"
#include "me6eapp_statistics.h"
#include "me6eapp_log.h"
#include "me6eapp_util.h"
#include "me6eapp_IProcessor.h"
#include "me6eapp_Controller.h"
#include "me6eapp_ProxyArp.h"
#include "me6eapp_ProxyNdp.h"
#include "me6eapp_MacManager.h"
#include "me6eapp_Capsuling.h"
#include "me6eapp_print_packet.h"
#include "me6eapp_EtherIP.h"

// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

//! 受信バッファのサイズ
#define TUNNEL_RECV_BUF_SIZE 65535

// ME6Eユニキャストアドレスのプレフィックス判定
#define IS_EQUAL_ME6E_UNI_PREFIX(a, b) \
        (((__const uint32_t *) (a))[0] == ((__const uint32_t *) (b))[0]     \
         && ((__const uint32_t *) (a))[1] == ((__const uint32_t *) (b))[1]  \
         && ((__const uint16_t *) (a))[4] == ((__const uint16_t *) (b))[4])

// ME6Eマルチキャストアドレスのプレフィックス判定
#define IS_EQUAL_ME6E_MULTI_PREFIX(a, b) \
        (((__const uint32_t *) (a))[0] == ((__const uint32_t *) (b))[0]     \
         && ((__const uint32_t *) (a))[1] == ((__const uint32_t *) (b))[1]  \
         && ((__const uint32_t *) (a))[2] == ((__const uint32_t *) (b))[2]  \
         && ((__const uint32_t *) (a))[3] == ((__const uint32_t *) (b))[3])

// ME6E-PR PlaneID判定
#define IS_EQUAL_ME6E_PR_PLANE_ID(a, b) \
        (((__const uint16_t *) (a))[3] == ((__const uint16_t *) (b))[3] \
         && ((__const uint16_t *) (a))[4] == ((__const uint16_t*) (b))[4])

////////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
static inline int me6e_construct_proxyndp(me6e_config_proxy_ndp_t* conf, me6e_list* list);
static inline int me6e_construct_macmanager(me6e_config_mng_macaddr_t* conf, me6e_list* list);
static inline int me6e_construct_Capsuling(me6e_config_capsuling_t* conf, me6e_list* list);
static inline void tunnel_buffer_cleanup(void* buffer);
static inline void tunnel_backbone_main_loop(struct me6e_handler_t* handler);
static inline void tunnel_stub_main_loop(struct me6e_handler_t* handler);
static inline void tunnel_forward_from_stub(struct me6e_handler_t* handler, char* recv_buffer, ssize_t recv_len);
static inline void tunnel_forward_from_backbone(struct me6e_handler_t* handler, struct msghdr* msg, ssize_t recv_len);
static inline bool me6e_prefix_check( struct me6e_handler_t* handler, struct in6_addr* ipi6_addr);
static inline bool me6e_pr_planeid_check(struct me6e_handler_t* handler, struct in6_addr* ipi6_addr);

///////////////////////////////////////////////////////////////////////////////
//! @brief Proxy ARPインスタンス生成関数
//!
//! Proxy ARPクラスのインスタンス生成を行う。
//!
//! @param [in]     handler ME6Eハンドラ
//! @param [out]    list    インスタンスリスト
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_construct_proxyarp(me6e_config_proxy_arp_t* conf, me6e_list* list)
{
    // 引数チェック
    if ((conf == NULL) || (list == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_construct_proxyarp).");
        return -1;
    }

    if (conf->arp_enable) {
        me6e_list* node = NULL;
        IProcessor* pro = NULL;

        // Proxy ARPクラスのインスタンス生成
        node = malloc(sizeof(me6e_list));
        if (node == NULL) {
            me6e_logging(LOG_ERR, "fail to malloc for Proxy ARP instance node.");
            return -1;
        }

        // メンバ関数ポインタの登録
        pro = ProxyArp_New();
        if (pro == NULL) {
            me6e_logging(LOG_ERR, "fail to malloc for Proxy ARP instance.");
            return -1;
        }

        // インスタンスリストへの登録
        me6e_list_init(node);
        me6e_list_add_data(node, pro);
        me6e_list_add_tail(list, node);

        DEBUG_LOG("proxyarp construct.\n");
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Proxy NDPインスタンス生成関数
//!
//! Proxy NDPクラスのインスタンス生成を行う。
//!
//! @param [in]     handler ME6Eハンドラ
//! @param [out]    list    インスタンスリスト
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
static inline int me6e_construct_proxyndp(me6e_config_proxy_ndp_t* conf, me6e_list* list)
{
    // 引数チェック
    if ((conf == NULL) || (list == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_construct_proxyndp).");
        return -1;
    }

    if (conf->ndp_enable) {
        me6e_list* node = NULL;
        IProcessor* pro = NULL;

        // Proxy NDPクラスのインスタンス生成
        node = malloc(sizeof(me6e_list));
        if (node == NULL) {
            me6e_logging(LOG_ERR, "fail to malloc for Proxy NDP instance node.");
            return -1;
        }

        // メンバ関数ポインタの登録
        pro = ProxyNdp_New();
        if (pro == NULL) {
            me6e_logging(LOG_ERR, "fail to malloc for Proxy NDP instance.");
            return -1;
        }

        // インスタンスリストへの登録
        me6e_list_init(node);
        me6e_list_add_data(node, pro);
        me6e_list_add_tail(list, node);

        DEBUG_LOG("proxyndp construct.\n");
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Mac Managerインスタンス生成関数
//!
//! Mac Managerクラスのインスタンス生成を行う。
//!
//! @param [in]     handler ME6Eハンドラ
//! @param [out]    list    インスタンスリスト
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
static inline int me6e_construct_macmanager(me6e_config_mng_macaddr_t* conf, me6e_list* list)
{
    // 引数チェック
    if ((conf == NULL) || (list == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_construct_macmanager).");
        return -1;
    }

    if (conf->mng_macaddr_enable) {
        me6e_list* node = NULL;
        IProcessor* pro = NULL;

        // Mac Managerクラスのインスタンス生成
        node = malloc(sizeof(me6e_list));
        if (node == NULL) {
            me6e_logging(LOG_ERR, "fail to malloc for Mac Manager instance node.");
            return -1;
        }

        // メンバ関数ポインタの登録
        pro = MacManager_New();
        if (pro == NULL) {
            me6e_logging(LOG_ERR, "fail to malloc for Mac Manager instance.");
            return -1;
        }

        // インスタンスリストへの登録
        me6e_list_init(node);
        me6e_list_add_data(node, pro);
        me6e_list_add_tail(list, node);

        DEBUG_LOG("macmanager construct.\n");
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Capsulingインスタンス生成関数
//!
//! Capsuling クラスのインスタンス生成を行う。
//!
//! @param [in]     handler      ME6Eハンドラ
//! @param [out]    list    インスタンスリスト
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
static inline int me6e_construct_Capsuling(me6e_config_capsuling_t* conf, me6e_list* list)
{
    me6e_list* node = NULL;
    IProcessor* pro = NULL;

    // 引数チェック
    if ((conf == NULL) || (list == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_construct_Capsuling).");
        return -1;
    }

    // カプセリング クラスのインスタンス生成
    node = malloc(sizeof(me6e_list));
    if (node == NULL) {
        me6e_logging(LOG_ERR, "fail to malloc for Capsuling instance node.");
        return -1;
    }

    // メンバ関数ポインタの登録
    pro = Capsuling_New();
    if (pro == NULL) {
        me6e_logging(LOG_ERR, "fail to malloc for Capsuling instance.");
        return -1;
    }

    // インスタンスリストへの登録
    me6e_list_init(node);
    me6e_list_add_data(node, pro);
    me6e_list_add_tail(list, node);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief インスタンス-リスト生成関数
//!
//! 各機能のクラスのインスタンス生成を行う。
//! インスタンスの生成順は、ProxyARP、ProxyNDP、MAC Manager、Capsulingである。
//! 但し、ProxyARP、ProxyNDP、MAC Managerに関しては、
//! 設定ファイルで、当該機能が有効になっている場合に、生成される。
//! Capsuling機能は必ず生成される。
//!
//! @param [in,out] handler ME6Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_construct_instances(struct me6e_handler_t* handler)
{
    me6e_list* list;

    // 引数チェック
    if ( handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_construct_instances).");
        return -1;
    }

    // インスタンスリストの初期化
    me6e_list_init(&(handler->instance_list));

    // ProxyARPの生成
    list = &(handler->instance_list);
    if (me6e_construct_proxyarp(handler->conf->arp, list) != 0) {
        me6e_logging(LOG_ERR, "fail to ProxyARP construct.");
        return -1;
    }

    // ProxyNDPの生成
    if (me6e_construct_proxyndp(handler->conf->ndp, list) != 0) {
        me6e_logging(LOG_ERR, "fail to ProxyNDP construct.");
        return -1;
    }

    // MAC Managerの生成
    if (me6e_construct_macmanager(handler->conf->mac, list) != 0) {
        me6e_logging(LOG_ERR, "fail to MAC Manager construct.");
        return -1;
    }

    // Capsulingの生成
    if (me6e_construct_Capsuling(handler->conf->capsuling, list) != 0) {
        me6e_logging(LOG_ERR, "fail to Capsuling construct.");
        return -1;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief インスタンスへの初期化依頼関数
//!
//! 各機能のインスタンスへ初期化依頼を行う
//!
//! @param [in]     handler      ME6Eハンドラ
//!
//! @retval true     正常終了
//! @retval false    異常終了
///////////////////////////////////////////////////////////////////////////////
bool me6e_init_instances(struct me6e_handler_t* handler)
{
    me6e_list* iter = NULL;
    me6e_list* list = NULL;
    bool result = true;

    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_init_instances).");
        return false;
    }

    list = &(handler->instance_list);
    me6e_list_for_each(iter, list){
        IProcessor* proc = iter->data;
        // 初期化依頼
        result = IProcessor_Init(proc, handler);
        if (!result) {
            me6e_logging(LOG_ERR, "fail to IProcessor_Init.");
            break;
        }
    }
    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief インスタンスの解放関数
//!
//! 各機能のインスタンスを解放する。
//!
//! @param [in,out] handler      ME6Eハンドラ
//!
//! @retval なし
///////////////////////////////////////////////////////////////////////////////
void me6e_release_instances(struct me6e_handler_t* handler)
{
    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_release_instances).");
        return;
    }

    if((handler->instance_list.next == NULL) ||
            (handler->instance_list.prev == NULL)) {
        return;
    }

    // 各機能のインスタンスの解放
    me6e_list* iter;
    me6e_list* list = &(handler->instance_list);
    me6e_list_for_each(iter, list){
        IProcessor* proc = iter->data;
        IProcessor_Release(proc);
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief インスタンス-リストの削除関数
//!
//! インスタンス-リストを削除する。
//!
//! @param [in,out] handler      ME6Eハンドラ
//!
//! @retval なし
///////////////////////////////////////////////////////////////////////////////
void me6e_destroy_instances(struct me6e_handler_t* handler)
{
    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_destroy_instances).");
        return;
    }

    if((handler->instance_list.next == NULL) ||
            (handler->instance_list.prev == NULL)) {
        return;
    }

    // インスタンス リストの削除
    me6e_list* list = &(handler->instance_list);

    while(!me6e_list_empty(list)) {
        me6e_list* node = list->next;
        me6e_list_del(node);
        free(node);
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief BackboneNW デカプセル化メインループ関数
//!
//! Backboneネットワークのデカプセル化処理のメインループ。
//! Backboneからのパケット受信を待ち受け、
//! 受信したパケットをデカプセル化する処理を起動する。
//!
//! @param [in] handler   ME6Eハンドラ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static inline void tunnel_backbone_main_loop(struct me6e_handler_t* handler)
{
    // ローカル変数宣言
    char*               recv_buffer;
    ssize_t             recv_len;
    int                 epfd, bb_fd;
    int                 loop, num;
    struct msghdr       msg = {0};
    char                cmsgbuf[CMSG_SPACE(sizeof(struct in6_pktinfo))] = {0};
    struct sockaddr_in6 daddr;
    struct iovec        iov[2];
    struct etheriphdr   ether_ip_hdr;
    struct epoll_event  ev, ev_ret[RECV_NEVENT_NUM];

    // 引数チェック
    if(handler == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(tunnel_backbone_main_loop).");
        return;
    }

    // 受信バッファ領域を確保
    recv_buffer = (char*)malloc(TUNNEL_RECV_BUF_SIZE);
    if(recv_buffer == NULL){
        me6e_logging(LOG_ERR, "receive buffer allocation failed.");
        return;
    }

    // 後始末ハンドラ登録
    pthread_cleanup_push(tunnel_buffer_cleanup, (void*)recv_buffer);

    // ファイルディスクリプタ
    bb_fd  = handler->conf->capsuling->bb_fd;

    // epollの生成
    epfd = epoll_create(RECV_NEVENT_NUM);
    if (epfd < 0) {
        me6e_logging(LOG_ERR, "fail to create epoll backbone : %s.", strerror(errno));
        return;
    }

    // 受信ソケットをepollへ登録
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = bb_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, bb_fd, &ev) != 0) {
        me6e_logging(LOG_ERR, "fail to control epoll backbone : %s.", strerror(errno));
        return;
    }

    // 受信パケットのScatter/Gather設定
    msg.msg_name = &daddr;
    msg.msg_namelen = sizeof(daddr);

    // 配列0に、EtherIPヘッダを格納
    iov[0].iov_base = &ether_ip_hdr;
    iov[0].iov_len  = sizeof(ether_ip_hdr);

    // 配列1に、EtherIPヘッダ以降のデータを格納(デカプセル化データ)
    iov[1].iov_base = recv_buffer;
    iov[1].iov_len  = TUNNEL_RECV_BUF_SIZE;
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;
    msg.msg_control = &cmsgbuf[0];
    msg.msg_controllen = sizeof(cmsgbuf);


    // ループ前に今溜まっているデータを全て吐き出す
    while(1){
        // 受信待ち
        num = epoll_wait(epfd, ev_ret, RECV_NEVENT_NUM, 0);

        if (num > 0 ) {
            for (loop = 0; loop < num; loop++) {
                if (ev_ret[loop].data.fd == bb_fd) {
                    recv_len = read(bb_fd, recv_buffer, TUNNEL_RECV_BUF_SIZE);
                } else {
                    me6e_logging(LOG_ERR, "unknown fd = %d.", ev_ret[loop].data.fd);
                    me6e_logging(LOG_ERR, "bb_fd = %d.", bb_fd);
                }
            }
        } else{
            // 即時に受信できるデータが無くなったのでループを抜ける
            break;
        }
    }


    me6e_logging(LOG_INFO, "Backbone tunnel thread main loop start.");
    while(1){
        // 受信待ち
        num = epoll_wait(epfd, ev_ret, RECV_NEVENT_NUM, -1);

        if(num < 0){
            if(errno == EINTR){
                // シグナル割込みの場合は処理継続
                me6e_logging(LOG_INFO, "Backbone tunnel main loop receive signal : %s.", strerror(errno));
                continue;
            }
            else{
                me6e_logging(LOG_ERR, "Backbone tunnel main loop receive error : %s.", strerror(errno));
                break;;
            }
        }

        // Backbone用ソケットでデータ受信
        for (loop = 0; loop < num; loop++) {
            if (ev_ret[loop].data.fd == bb_fd) {
                if((recv_len = recvmsg(bb_fd, &msg, 0)) > 0){
                    DEBUG_LOG("\n");
                    DEBUG_LOG("\n");
                    DEBUG_LOG("---------- backbone massage receive. ----------\n");
                    tunnel_forward_from_backbone(handler, &msg, recv_len);
                }
                else{
                    me6e_logging(LOG_ERR, "backbone recvmsg error : %s.", strerror(errno));
                }
            } else {
                me6e_logging(LOG_ERR, "unknown fd = %d.", ev_ret[loop].data.fd);
                me6e_logging(LOG_ERR, "bb_fd = %d.", bb_fd);
            }
        }
    }

    me6e_logging(LOG_INFO, "Backbone tunnel thread main loop end.");

    // 後始末
    pthread_cleanup_pop(1);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief BackboneNW デカプセル化スレッドメイン関数
//!
//! Backboneネットワークのデカプセル化処理のメインループを起動する。
//!
//! @param [in] arg ME6Eハンドラ
//!
//! @return NULL固定
///////////////////////////////////////////////////////////////////////////////
void* me6e_tunnel_backbone_thread(void* arg)
{
    // ローカル変数宣言
    struct me6e_handler_t* handler = NULL;

    // 引数チェック
    if(arg == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_tunnel_backbone_thread).");
        pthread_exit(NULL);
    }

    // ローカル変数初期化
    handler = (struct me6e_handler_t*)arg;

    // メインループ開始
    tunnel_backbone_main_loop(handler);

    pthread_exit(NULL);

    return NULL;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief StubNW カプセル化メインループ関数
//!
//! Stubネットワークのカプセル化処理のメインループ。
//! トンネルデバイスからのパケット受信を待ち受け、
//! 受信したパケットをカプセル化する処理を起動する。
//!
//! @param [in] handler   ME6Eハンドラ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static inline void tunnel_stub_main_loop(struct me6e_handler_t* handler)
{
    // ローカル変数宣言
    int                 epfd, stub_fd;
    char*               recv_buffer;
    ssize_t             recv_len;
    int                 loop, num;
    struct epoll_event  ev, ev_ret[RECV_NEVENT_NUM];


    // 引数チェック
    if(handler == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(tunnel_stub_main_loop).");
        return;
    }

    // 受信バッファ領域を確保
    recv_buffer = (char*)malloc(TUNNEL_RECV_BUF_SIZE);
    if(recv_buffer == NULL){
        me6e_logging(LOG_ERR, "receive buffer allocation failed.");
        return;
    }

    // 後始末ハンドラ登録
    pthread_cleanup_push(tunnel_buffer_cleanup, (void*)recv_buffer);

    // ファイルディクリプタの取得
    stub_fd =  handler->conf->capsuling->tunnel_device.option.tunnel.fd;

    // epollの生成
    epfd = epoll_create(RECV_NEVENT_NUM);
    if (epfd < 0) {
        me6e_logging(LOG_ERR, "fail to create epoll stub : %s.", strerror(errno));
        return;
    }

    // 受信用ソケットをepollへ登録
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = stub_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, stub_fd, &ev) != 0) {
        me6e_logging(LOG_ERR, "fail to control epoll stub : %s.", strerror(errno));
        return;
    }


    // メインループ前に溜まっているデータを全て吐き出す
    while(1){
        // 受信待ち
        num = epoll_wait(epfd, ev_ret, RECV_NEVENT_NUM, 0);

        if (num > 0 ) {
            for (loop = 0; loop < num; loop++) {
                if (ev_ret[loop].data.fd == stub_fd) {
                    recv_len = read(stub_fd, recv_buffer, TUNNEL_RECV_BUF_SIZE);
                } else {
                    me6e_logging(LOG_ERR, "unknown fd = %d.", ev_ret[loop].data.fd);
                    me6e_logging(LOG_ERR, "stub_fd = %d.", stub_fd);
                }
            }
        } else{
            // 即時に受信できるデータが無くなったのでループを抜ける
            break;
        }
    }


    me6e_logging(LOG_INFO, "Stub tunnel thread main loop start.");
    while(1){
        // 受信待ち
        num = epoll_wait(epfd, ev_ret, RECV_NEVENT_NUM, -1);

        if(num < 0){
            if(errno == EINTR){
                // シグナル割込みの場合は処理継続
                me6e_logging(LOG_INFO, "Stub tunnel main loop receive signal : %s.", strerror(errno));
                continue;
            }
            else{
                me6e_logging(LOG_ERR, "Stub tunnel main loop receive error : %s.", strerror(errno));
                break;;
            }
        }

        // Stub用TAPデバイスでデータ受信
        for (loop = 0; loop < num; loop++) {
            if (ev_ret[loop].data.fd == stub_fd) {
                if((recv_len=read(stub_fd, recv_buffer, TUNNEL_RECV_BUF_SIZE)) > 0){
                    DEBUG_LOG("\n");
                    DEBUG_LOG("\n");
                    DEBUG_LOG("---------- stub massage receive. ----------\n");
                    _D_(me6eapp_hex_dump(recv_buffer, recv_len);)
                    _D_(me6e_print_packet(recv_buffer);)
                    tunnel_forward_from_stub(handler, recv_buffer, recv_len);
                }
                else{
                    me6e_logging(LOG_ERR, "stub read error : %s.", strerror(errno));
                }
            } else {
                me6e_logging(LOG_ERR, "unknown fd = %d.", ev_ret[loop].data.fd);
                me6e_logging(LOG_ERR, "stub_fd = %d.", stub_fd);
            }
        }
    }

    me6e_logging(LOG_INFO, "Stub tunnel thread main loop end.");

    // 後始末
    pthread_cleanup_pop(1);

    return;

}



///////////////////////////////////////////////////////////////////////////////
//! @brief StubNW カプセル化スレッドメイン関数
//!
//! Stubネットワークのカプセル化処理のメインループを起動する。
//!
//! @param [in] arg ME6Eハンドラ
//!
//! @return NULL
///////////////////////////////////////////////////////////////////////////////
void* me6e_tunnel_stub_thread(void* arg)
{
    // ローカル変数宣言
    struct me6e_handler_t* handler = NULL;

    // 引数チェック
    if(arg == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_tunnel_stub_thread).");
        pthread_exit(NULL);
    }

    // ローカル変数初期化
    handler = (struct me6e_handler_t*)arg;

    // メインループ開始
    tunnel_stub_main_loop(handler);

    pthread_exit(NULL);

    return NULL;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 受信バッファ解放関数
//!
//! 引数で指定されたバッファを解放する。
//! スレッドの終了時に呼ばれる。
//!
//! @param [in] buffer    受信バッファ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static inline void tunnel_buffer_cleanup(void* buffer)
{
    DEBUG_LOG("tunnel_buffer_cleanup\n");

    // 確保したメモリを解放
    free(buffer);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief StubNWパケット処理関数
//!
//! Stub側から受信したパケットに対して起動されている各機能の
//! インスタンスの処理を起動する。
//!
//! インスタンスからの戻り値がtrueの場合、
//! 次のインスタンスの処理を起動する。
//!
//! インスタンスからの戻り値がfalseの場合、
//! 次のインスタンスの処理は起動せず、処理を終了する。
//!
//! @param [in,out] handler     ME6Eハンドラ
//! @param [in]     recv_buffer 受信パケットデータ
//! @param [in]     recv_len    受信パケット長
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static inline void tunnel_forward_from_stub(
                struct me6e_handler_t* handler,
                char* recv_buffer, ssize_t recv_len)
{
    // 引数チェック
    if ((handler == NULL) || (recv_buffer == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(tunnel_forward_from_stub).");
        pthread_exit(NULL);
    }

    // 各機能のインスタンス リストを取得
    me6e_list* list = &(handler->instance_list);

    bool isContinue = true;
    struct me6e_list* iter;
    me6e_list_for_each(iter, list){
        // 各機能のインスタンスへ処理を依頼
        IProcessor* proc = iter->data;
        isContinue = IProcessor_RecvFromStub(proc, recv_buffer, recv_len);

        if(!isContinue) {
            break;
        }
    }
    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Backbone側パケット転送関数
//!
//! Backbone側から受信したパケットをデカプセル化し、
//! 各機能のインスタンスへ処理を依頼する。
//!
//! インスタンスからの戻り値がtrueの場合、
//! 次のインスタンスの処理を起動する。
//!
//! インスタンスからの戻り値がfalseの場合、
//! 次のインスタンスの処理は起動せず、処理を終了する。
//!
//! @param [in,out] handler     ME6Eハンドラ
//! @param [in]     msg         受信メッセージ
//! @param [in]     recv_len    受信パケット長
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static inline void tunnel_forward_from_backbone(
                struct me6e_handler_t* handler,
                struct msghdr* msg, ssize_t recv_len)
{
    struct cmsghdr*     cmsg = NULL;
    struct etheriphdr*  ether_ip_hdr = NULL;
    char*               recv_buffer = NULL;
    ssize_t             packet_len = 0;
    me6e_list*         list = NULL;


    // 引数チェック
    if ((handler == NULL) || (msg == NULL) ){
        me6e_logging(LOG_ERR, "Parameter Check NG(tunnel_forward_from_backbone).");
        return;
    }

    // ETHER_IPヘッダのバージョンチェック
    ether_ip_hdr =  msg->msg_iov[0].iov_base;
    if (ether_ip_hdr->version != ETHERIP_VERSION) {
        me6e_inc_decapsuling_unmatch_header_count(handler->stat_info);
        me6e_logging(LOG_ERR, "fail to EtherIP Version.");
        return;
    }

    // カプセル化メッセージの先頭を取得(当該処理がデカプセル化)
    recv_buffer =   msg->msg_iov[1].iov_base;
    packet_len = recv_len - sizeof(struct etheriphdr);

    // 送信元情報の取得
    struct sockaddr_in6* s_srcaddr = (struct sockaddr_in6*)(msg->msg_name);

    char addr[INET6_ADDRSTRLEN] = {0};
    DEBUG_LOG("src addr : %s\n",
            inet_ntop(AF_INET6, &s_srcaddr->sin6_addr, addr, sizeof(addr)));

    // 送信元情報の正常性チェック
    // 送信元アドレスがマルチキャストアドレスの場合は破棄
    // ユニキャストアドレスのみ処理するため。
    if (IN6_IS_ADDR_MULTICAST(&s_srcaddr->sin6_addr)) {
        DEBUG_LOG("drop packet. src address multicast.");
        // パケット破棄
        return;
    }

    // モードがME6E-PRモードでなければ、prefixをチェック
    if(handler->conf->common->tunnel_mode != ME6E_TUNNEL_MODE_PR){
        // 送信元アドレスのprefixが自身の管理するprefixと一致していない場合は破棄
        if(!me6e_prefix_check(handler, &s_srcaddr->sin6_addr)) {
            DEBUG_LOG("drop packet. src address not equal prefix.");
            // パケット破棄
            return;
        }
    }
    /*
    else{
        // PRモードの場合、PlaneIDが一致するかチェック
        if(!me6e_pr_planeid_check(handler, &s_srcaddr->sin6_addr)){
            DEBUG_LOG("drop packet. MAC addr not exist in PR Table.");
            return;
       }
    }
    */

    // 送信先情報の取得
    struct in6_pktinfo* info = NULL;
    for (cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL; cmsg = CMSG_NXTHDR(msg, cmsg)){
        if(cmsg->cmsg_level == IPPROTO_IPV6 && cmsg->cmsg_type == IPV6_PKTINFO){
            info = (struct in6_pktinfo*)CMSG_DATA(cmsg);
            DEBUG_LOG("dst addr : %s\n",
                            inet_ntop(AF_INET6, &info->ipi6_addr, addr, sizeof(addr)));
            break;
        }
    }

    // 送信先アドレスのprefixが自身の管理するprefixと一致していない場合は破棄
    if (info != NULL) {
        if(!me6e_prefix_check(handler, &info->ipi6_addr)) {
            DEBUG_LOG("drop packet. dst address not equal prefix.\n");
            // パケット破棄
            return;
        }
    } else {
        me6e_logging(LOG_ERR, "packet info not exists.");
        // パケット破棄
        return;
    }


    // 各機能のインスタンスへ処理を依頼
    list = &(handler->instance_list);

    struct me6e_list* iter;
    me6e_list_for_each(iter, list){
        IProcessor* proc = iter->data;
        bool isContinue = IProcessor_RecvFromBackbone(proc, recv_buffer, packet_len);

        if(!isContinue) {
            break;
        }
    }
    return ;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Prefixチェック処理関数
//!
//! Backbone側から受信したパケットのME6Eプレフィックスをチェックする。
//!
//! @param [in,out] handler     ME6Eハンドラ
//! @param [in]     ipi6_addr   送信元アドレス
//!
//! @retval true  Prefixが自planeと同じ
//! @retval false Prefixが自planeと異なる
///////////////////////////////////////////////////////////////////////////////
static inline bool me6e_prefix_check(
        struct me6e_handler_t* handler,
        struct in6_addr* ipi6_addr)
{
    // 引数チェック
    if ((handler == NULL) || (ipi6_addr == NULL) ){
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_prefix_check).");
        return false;
    }

    // 送信元がUNSPECIFIEDは破棄
    if (IN6_IS_ADDR_UNSPECIFIED(ipi6_addr)) {
        DEBUG_LOG("check error unspecified address.\n");
        return false;
    }

    // 送信元がLOOPBACKは破棄
    if (IN6_IS_ADDR_LOOPBACK(ipi6_addr)) {
        DEBUG_LOG("check error loopback address.\n");
        return false;
    }

    bool ret = false;
    if (IN6_IS_ADDR_MULTICAST(ipi6_addr)) {
        // マルチキャストの場合
        DEBUG_LOG("IPv6 multicast packet.\n");
        ret = IS_EQUAL_ME6E_MULTI_PREFIX(ipi6_addr, &handler->multicast_prefix);

    } else {
        // ユニキャストの場合
        DEBUG_LOG("IPv6 unicast packet.\n");
        ret = IS_EQUAL_ME6E_UNI_PREFIX(ipi6_addr, &handler->unicast_prefix);
    }

    return ret;
}


///////////////////////////////////////////////////////////////////////////////
//! @breif PR Prefixチェック処理関数
//!
//! Backbone側から受信したパケットのME6EプレフィックスのPlane IDとMACアドレス
//! をチェックする。
//!
//! @param [in,out] handler     ME6Eハンドラ
//! @param [in]     ipi6_addr   送信元アドレス
//!
//! @ret true  Plane IDとMACアドレスがPR Tableに存在する
//! @ret false Plane IDとMACアドレスがPR Tableに存在しない
///////////////////////////////////////////////////////////////////////////////
static inline bool me6e_pr_planeid_check(
        struct me6e_handler_t* handler,
        struct in6_addr* ipi6_addr)
{
    // 引数チェック
    if((handler == NULL) || (ipi6_addr == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_pr_prefix_check).");
        return false;
    }
    // ローカル変数
    char   v6addr[INET6_ADDRSTRLEN] = { 0 };
    /*
    me6e_logging(LOG_ERR,
                 "Recv pr_prefix:%s, Own pr_prefix:%s\n",
                 inet_ntop(AF_INET6, ipi6_addr, v6addr, INET6_ADDRSTRLEN),
                 inet_ntop(AF_INET6, handler->conf->capsuling->pr_unicast_prefixplaneid,
                                     v6addr, INET6_ADDRSTRLEN));
    */
    me6e_logging(LOG_ERR, "### Rcv Prefix:%s\n",
                inet_ntop(AF_INET6, ipi6_addr, v6addr, INET6_ADDRSTRLEN));
    me6e_logging(LOG_ERR, "### Own Prefix:%s\n",
                inet_ntop(AF_INET6, handler->conf->capsuling->pr_unicast_prefixplaneid,
                v6addr, INET6_ADDRSTRLEN));

    bool ret;
    ret = IS_EQUAL_ME6E_PR_PLANE_ID(ipi6_addr, handler->conf->capsuling->pr_unicast_prefixplaneid);


    return ret;
}

