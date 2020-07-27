/******************************************************************************/
/* ファイル名 : me6eapp_Capsuling.c                                           */
/* 機能概要   : カプセリング クラス ソースファイル                            */
/* 修正履歴   : 2013.01.11 S.Yoshikawa 新規作成                               */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*            : 2016.06.01 S.Anai PR機能の追加                                */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <net/ethernet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <net/if_arp.h>
#include <netinet/ether.h>
#include <netinet/ip6.h>
#include <errno.h>


#include "me6eapp.h"
#include "me6eapp_config.h"
#include "me6eapp_statistics.h"
#include "me6eapp_util.h"
#include "me6eapp_log.h"
#include "me6eapp_timer.h"
#include "me6eapp_hashtable.h"
#include "me6eapp_IProcessor.h"
#include "me6eapp_Capsuling.h"
#include "me6eapp_EtherIP.h"
#include "me6eapp_print_packet.h"
#include "me6eapp_pr.h"
#include "me6eapp_pr_struct.h"


// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif


///////////////////////////////////////////////////////////////////////////////
//! Capsulingクラスのフィールド定義
///////////////////////////////////////////////////////////////////////////////
struct CapsulingField{
        struct me6e_handler_t *handler;        ///< ME6Eのアプリケーションハンドラー
        // 以下に必要なメンバを追加する
};
typedef struct CapsulingField CapsulingField;

////////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
static bool Capsuling_Init(IProcessor* self, struct me6e_handler_t *handler);
static void Capsuling_Release(IProcessor* proc);
static bool Capsuling_RecvFromStub(IProcessor* self, char* recv_buffer, ssize_t recv_len);
static bool Capsuling_RecvFromBackbone(IProcessor* self, char* recv_buffer, ssize_t recv_len);
static inline bool Capsuling_capsule_msg_send(IProcessor* self, struct in6_addr* src,
                struct in6_addr* dst, char* recv_buffer, ssize_t recv_len);
// L2MC-L3UC機能 start
static inline bool Capsuling_capsule_msg_send_l2mc_l3uc( IProcessor* self,
                struct in6_addr* src, char* recv_buffer, ssize_t recv_len);
// L2MC-L3UC機能 end

///////////////////////////////////////////////////////////////////////////////
//! Capsulingのフィールドアクセス用マクロ
///////////////////////////////////////////////////////////////////////////////
#define CAPSULING_FIELD(ptr)((CapsulingField*)(ptr->data))


///////////////////////////////////////////////////////////////////////////////
//! @brief カプセリング インスタンス生成関数
//!
//! Capsulingインスタンスを生成する。
//!
//! @return IProcessor構造体へのポインター
///////////////////////////////////////////////////////////////////////////////
IProcessor* Capsuling_New()
{
    IProcessor* instance = NULL;

    DEBUG_LOG("Capsuling_New start.");

    // インスタンスの生成
    instance = malloc(sizeof(IProcessor));
    if (instance == NULL) {
        me6e_logging(LOG_ERR, "fail to malloc for Capsuling instance.");
        return NULL;
    }

    memset(instance, 0, sizeof(IProcessor));

    // フィールドの生成
    instance->data = malloc(sizeof(CapsulingField));
    if (instance->data== NULL) {
        me6e_logging(LOG_ERR, "fail to malloc for Capsuling instance field.");
        return NULL;
    }
    memset(instance->data, 0, sizeof(CapsulingField));


    // メソッドの登録
    instance->init               = Capsuling_Init;
    instance->release            = Capsuling_Release;
    instance->recv_from_stub     = Capsuling_RecvFromStub;
    instance->recv_from_backbone = Capsuling_RecvFromBackbone;

    DEBUG_LOG("Capsuling_New end.");
    return instance;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Capsuling インスタンス初期化関数
//!
//! Capsulingインスタンスのフィールドを初期化する
//!
//! @param [in] sefl IProcessor構造体
//! @param [in] handler アプリケーションハンドラー
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool Capsuling_Init(IProcessor* self, struct me6e_handler_t *handler)
{
    DEBUG_LOG("Capsuling_Init start.");

    // 引数チェック
    if ( (handler == NULL) || (self == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(Capsuling_Init).\n");
        return false;
    }

    // ハンドラーの設定
    CAPSULING_FIELD(self)->handler = handler;

    DEBUG_LOG("Capsuling_Init end.");
    return  true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Capsuling インスタンス解放関数
//!
//! Capsulingインスタンスが保持するリソースを解放する。
//!
//! @param [in] self IProcessor構造体
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void Capsuling_Release(IProcessor* self)
{
    DEBUG_LOG("Capsuling_Release start.");

    // 引数チェック
    if (self == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(Capsuling_Release).\n");
        return;
    }

    // フィールドの解放
    free(CAPSULING_FIELD(self));

    // インスタンスの解放
    free(self);

    DEBUG_LOG("Capsuling_Release start.");
    return;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief StubNWパケット受信処理関数
//!
//! StubNWから受信したパケットをカプセル化する。
//!
//! @param [in] self        IProcessor構造体
//! @param [in] recv_buffer 受信データ
//! @param [in] recv_len    受信データのサイズ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool Capsuling_RecvFromStub(IProcessor* self, char* recv_buffer, ssize_t recv_len)
{
    DEBUG_LOG("Capsuling_RecvFromStub\n");

    // 引数チェック
    if ((self == NULL) || (recv_buffer == NULL) || (recv_len <= 0)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(Capsuling_RecvFromStub).\n");
        return false;
    }

    struct ethhdr*      p_orig_eth_hdr = NULL;
    struct in6_addr*    uni_prefix = NULL;
    struct in6_addr     src = in6addr_any;
    struct in6_addr     dst = in6addr_any;
    me6e_pr_entry_t*    pr_entry;

    uni_prefix = &(CAPSULING_FIELD(self)->handler->unicast_prefix);

    // 受信パケットの先頭を ETHERヘッダへキャスト
    p_orig_eth_hdr = (struct ethhdr*)recv_buffer;

    if(me6e_util_is_broadcast_mac(&p_orig_eth_hdr->h_dest[0])){
        // ブロードキャストパケット
        DEBUG_LOG("recv broadcast packet.\n");

        // トンネルモードがME6E_TUNNEL_MODE_PRならば、パケットを破棄
        if(CAPSULING_FIELD(self)->handler->conf->common->tunnel_mode == ME6E_TUNNEL_MODE_PR){
            return false;
        }

        // L2MC-L3UC機能 start
        if (CAPSULING_FIELD(self)->handler->conf->capsuling->l2multi_l3uni) {
            if (!Capsuling_capsule_msg_send_l2mc_l3uc(self,
                        &(CAPSULING_FIELD(self)->handler->me6e_own_v6addr),
                        recv_buffer, recv_len)) {
                me6e_logging(LOG_ERR, "fail to send capsuling packet.\n");
                return false;
            }
            return true;
        }
        // L2MC-L3UC機能 end

        // 送信先ME6Eマルチキャストアドレスの設定
        dst = CAPSULING_FIELD(self)->handler->multicast_prefix;

    } else if(me6e_util_is_multicast_mac(&p_orig_eth_hdr->h_dest[0])){
        // マルチキャストパケット
        DEBUG_LOG("recv multicast packet.\n");

        // トンネルモードがME6E_TUNNEL_MODE_PRならば、パケットを破棄
        if(CAPSULING_FIELD(self)->handler->conf->common->tunnel_mode == ME6E_TUNNEL_MODE_PR){
            return false;
        }

        // L2MC-L3UC機能 start
        if (CAPSULING_FIELD(self)->handler->conf->capsuling->l2multi_l3uni) {
            if (!Capsuling_capsule_msg_send_l2mc_l3uc(self,
                        &(CAPSULING_FIELD(self)->handler->me6e_own_v6addr),
                        recv_buffer, recv_len)) {
                me6e_logging(LOG_ERR, "fail to send capsuling packet.\n");
                return false;
            }
            return true;
        }
        // L2MC-L3UC機能 end

        // 送信先ME6Eマルチキャストアドレスの設定
        dst = CAPSULING_FIELD(self)->handler->multicast_prefix;

    } else{
        // ユニキャストパケット
        DEBUG_LOG("recv unicast packet.\n");

        // 送信先ME6Eユニキャストキャストアドレスの生成
        // トンネルモードがME6E_TUNNEL_MODE_PRならば、
        
        if(CAPSULING_FIELD(self)->handler->conf->common->tunnel_mode == ME6E_TUNNEL_MODE_PR){
            // PR Tableより送信先MACアドレスと同一のエントリーを検索する
            pr_entry = me6e_pr_entry_search_stub(
                            CAPSULING_FIELD(self)->handler->pr_handler,
                            (struct ether_addr*)p_orig_eth_hdr->h_dest
                            );
            if(pr_entry == NULL){
                me6e_logging(LOG_ERR,"drop packet so that dest MAC address is NOT in M46E-PR Table.\n");
                return false;
            }else{
                // PRエントリのprefixをuni_prefixに設定
                uni_prefix = &pr_entry->pr_prefix_planeid;
                char   addr[INET6_ADDRSTRLEN] = { 0 };
                DEBUG_LOG("dst prefix: %s\n", inet_ntop(AF_INET6,uni_prefix, addr, INET6_ADDRSTRLEN));
            }            
        }
        
        if (NULL == me6e_create_me6eaddr(uni_prefix,
                    (struct ether_addr*)p_orig_eth_hdr->h_dest,
                    &dst)) {
            me6e_logging(LOG_ERR, "fail to create dst me6e address.\n");
            return false;
        }
    }

    char   addr[INET6_ADDRSTRLEN] = { 0 };
    DEBUG_LOG("dst addr : %s\n",inet_ntop(AF_INET6, &dst, addr, INET6_ADDRSTRLEN));

    // 送信元ME6Eアドレスの生成
    // トンネルモードがME6E_TUNNEL_MODE_PRならば、
    if(CAPSULING_FIELD(self)->handler->conf->common->tunnel_mode == ME6E_TUNNEL_MODE_PR){
        // ME6E-PR ユニキャストアドレスプレフィックスをprefixをuni_prefixに設定
        uni_prefix = CAPSULING_FIELD(self)->handler->conf->capsuling->pr_unicast_prefixplaneid;
        DEBUG_LOG("src prefix: %s\n",inet_ntop(AF_INET6, uni_prefix, addr, INET6_ADDRSTRLEN));
    }

    if (NULL == me6e_create_me6eaddr(uni_prefix,
                (struct ether_addr*)p_orig_eth_hdr->h_source,
                &src)) {
        me6e_logging(LOG_ERR, "fail to create src me6e address.\n");
        return false;
    }

    DEBUG_LOG("src addr : %s\n",inet_ntop(AF_INET6, &src, addr, INET6_ADDRSTRLEN));

    // 受信メッセージをカプセル化し、Backboneへ送信
    if (!Capsuling_capsule_msg_send(self, &src, &dst, recv_buffer, recv_len)) {
        me6e_logging(LOG_ERR, "fail to send capsuling packet.\n");
        return false;
    }

    return  true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief カプセリング処理関数
//!
//! StubNWから受信したパケットをカプセル化し、BackboneNWへ送信する。
//!
//! @param [in] self        IProcessor構造体へのポインター
//! @param [in] src         送信元IPv6アドレス
//! @param [in] dst         送信先IPv6アドレス
//! @param [in] recv_buffer 受信データ
//! @param [in] recv_len    受信データのサイズ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static inline bool Capsuling_capsule_msg_send(
        IProcessor* self,
        struct in6_addr* src,
        struct in6_addr* dst,
        char* recv_buffer,
        ssize_t recv_len)
{

    // 引数チェック
    if ((self == NULL) || (recv_buffer == NULL) || (src == NULL) || (dst == NULL) ) {
        me6e_logging(LOG_ERR, "Parameter Check NG(Capsuling_capsule_msg_send).\n");
        return false;
    }

    struct sockaddr_in6 daddr;
    struct in6_pktinfo* info;
    struct msghdr       msg = {0};
    struct cmsghdr*     cmsg;
    char                cmsgbuf[CMSG_SPACE(sizeof(struct in6_pktinfo))] = {0};
    struct  iovec       iov[2];
    struct etheriphdr   ether_ip_hdr;
    int                 fd = -1;
    int                 ret = -1;

    fd = CAPSULING_FIELD(self)->handler->conf->capsuling->bb_fd;

    // EtherIPヘッダの設定
    ether_ip_hdr.version = ETHERIP_VERSION;
    ether_ip_hdr.reserved = 0;
    ether_ip_hdr.reserved2 = 0;

    // IPv6ヘッダの設定
    daddr.sin6_family = AF_INET6;
    daddr.sin6_port = htons(ME6E_IPPROTO_ETHERIP);
    daddr.sin6_addr = *dst;
    daddr.sin6_flowinfo = 0;
    daddr.sin6_scope_id = 0;

    // Scatter/Gather設定
    // 配列0に、EtherIPヘッダを格納
    iov[0].iov_base = &ether_ip_hdr;
    iov[0].iov_len  = sizeof(ether_ip_hdr);

    // 配列1に、EtherIPヘッダ以降のデータを格納(デカプセル化データ)
    iov[1].iov_base = recv_buffer;
    iov[1].iov_len  = recv_len;

    msg.msg_name = &daddr;
    msg.msg_namelen = sizeof(daddr);
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;
    msg.msg_control = &cmsgbuf[0];
    msg.msg_controllen = sizeof(cmsgbuf);

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
    cmsg->cmsg_level = IPPROTO_IPV6;
    cmsg->cmsg_type = IPV6_PKTINFO;

    // 送信元情報(in6_pktinfo)をcmsgへ設定
    info = (struct in6_pktinfo*)CMSG_DATA(cmsg);
    info->ipi6_addr = *src;

    if (IN6_IS_ADDR_MULTICAST(dst)) {
        // マルチキャスト送信
        info->ipi6_ifindex = if_nametoindex(
            CAPSULING_FIELD(self)->handler->conf->capsuling->backbone_physical_dev);
    } else {
        // ユニキャスト送信
        info->ipi6_ifindex = 0;
    }

    // カプセル化したデータを送信
    ret = sendmsg(fd, &msg, 0);
    if (ret < 0) {
        me6e_inc_capsuling_failure_count(CAPSULING_FIELD(self)->handler->stat_info);
        me6e_logging(LOG_ERR, "fail to sendmsg capsuling packet. %s\n", strerror(errno));
        return false;
    }

    me6e_inc_capsuling_success_count(CAPSULING_FIELD(self)->handler->stat_info);
    DEBUG_LOG("forward %d bytes to encap.\n", recv_len + sizeof(ether_ip_hdr));

    return true;
}

// L2MC-L3UC機能 start
///////////////////////////////////////////////////////////////////////////////
//! @brief カプセリング処理関数(L2multicast-L3unicast用)
//!
//! StubNWから受信したパケットをカプセル化し、
//! 設定情報に登録されているME6Eホスト分、BackboneNWへ送信する。
//!
//! @param [in] self        IProcessor構造体へのポインター
//! @param [in] src         送信元IPv6アドレス
//! @param [in] recv_buffer 受信データ
//! @param [in] recv_len    受信データのサイズ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static inline bool Capsuling_capsule_msg_send_l2mc_l3uc(
        IProcessor* self,
        struct in6_addr* src,
        char* recv_buffer,
        ssize_t recv_len)
{

    // 引数チェック
    if ((self == NULL) || (recv_buffer == NULL) || (src == NULL) ) {
        me6e_logging(LOG_ERR, "Parameter Check NG(Capsuling_capsule_msg_send_l2mc_l3uc).\n");
        return false;
    }

    // EtherIPヘッダの設定
    struct sockaddr_in6 daddr;
    struct in6_pktinfo* info;
    struct msghdr       msg = {0};
    struct cmsghdr*     cmsg;
    char                cmsgbuf[CMSG_SPACE(sizeof(struct in6_pktinfo))] = {0};
    struct  iovec       iov[2];
    struct etheriphdr   ether_ip_hdr;
    int                 fd = -1;
    int                 ret = -1;

    fd = CAPSULING_FIELD(self)->handler->conf->capsuling->bb_fd;

    // EtherIPヘッダの設定
    ether_ip_hdr.version = ETHERIP_VERSION;
    ether_ip_hdr.reserved = 0;
    ether_ip_hdr.reserved2 = 0;

    // IPv6ヘッダの設定
    daddr.sin6_family = AF_INET6;
    daddr.sin6_port = htons(ME6E_IPPROTO_ETHERIP);
    daddr.sin6_flowinfo = 0;
    daddr.sin6_scope_id = 0;

    // Scatter/Gather設定
    // 配列0に、EtherIPヘッダを格納
    iov[0].iov_base = &ether_ip_hdr;
    iov[0].iov_len  = sizeof(ether_ip_hdr);

    // 配列1に、EtherIPヘッダ以降のデータを格納(デカプセル化データ)
    iov[1].iov_base = recv_buffer;
    iov[1].iov_len  = recv_len;

    msg.msg_name = &daddr;
    msg.msg_namelen = sizeof(daddr);
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;
    msg.msg_control = &cmsgbuf[0];
    msg.msg_controllen = sizeof(cmsgbuf);

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
    cmsg->cmsg_level = IPPROTO_IPV6;
    cmsg->cmsg_type = IPV6_PKTINFO;

    // 送信元情報(in6_pktinfo)をcmsgへ設定
    info = (struct in6_pktinfo*)CMSG_DATA(cmsg);
    info->ipi6_addr = *src;
    info->ipi6_ifindex = 0;

    // 登録されているリスト分、カプセル化したデータを送信
    me6e_list* host_list = &(CAPSULING_FIELD(self)->handler->conf->capsuling->me6e_host_address_list);
    me6e_list* iter;
    me6e_list_for_each(iter, host_list) {
    struct in6_addr* dst = iter->data;
    char   addr[INET6_ADDRSTRLEN] = { 0 };

        if(dst != NULL){
            // パケット送信
            daddr.sin6_addr = *(dst);
            ret = sendmsg(fd, &msg, 0);
            if (ret < 0) {
                me6e_inc_capsuling_failure_count(CAPSULING_FIELD(self)->handler->stat_info);
                me6e_logging(LOG_ERR, "fail to send address %s (l2mc_l3uni) : %s.",
                                inet_ntop(AF_INET6, dst, addr, INET6_ADDRSTRLEN), strerror(errno));
            }
            else {
                me6e_inc_capsuling_success_count(CAPSULING_FIELD(self)->handler->stat_info);
                DEBUG_LOG("forward %d bytes to encap %s (l2mc_l3uni).\n",
                        recv_len + sizeof(ether_ip_hdr), inet_ntop(AF_INET6, dst, addr, INET6_ADDRSTRLEN));
            }
        }
    }

    return true;
}
// L2MC-L3UC機能 end

///////////////////////////////////////////////////////////////////////////////
//! @brief BackBoneパケット受信処理関
//!
//! BackboneNWから受信したパケットをStubNWへ送信する。
//!
//! @param [in] self        IProcessor構造体へのポインター
//! @param [in] recv_buffer 受信データ(ETHER_IPヘッダ除去済み)
//! @param [in] recv_len    受信データのサイズ(ETHER_IPヘッダサイズ除去済み)
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool Capsuling_RecvFromBackbone(IProcessor* self, char* recv_buffer, ssize_t recv_len)
{
    DEBUG_LOG("Capsuling_RecvFromBackbone\n");

    // 引数チェック
    if ((self == NULL) || (recv_buffer == NULL) || (recv_len <= 0)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(Capsuling_RecvFromBackbone).\n");
        return false;
    }

#if 0
    // MACフィルタ対応 2016/09/09 add start
    // src MACをbridgeデバイスのMACに書き換える。
    struct ethhdr* p_ether = (struct ethhdr*)recv_buffer;
	struct ether_addr* bridge_hwaddr = CAPSULING_FIELD(self)->handler->conf->capsuling->bridge_hwaddr;
	memcpy((struct ether_addr*)&p_ether->h_source, bridge_hwaddr, sizeof(struct ether_addr));
    // MACフィルタ対応 2016/09/09 add end
#endif
     // 受信パケット解析＆表示
    _D_(me6eapp_hex_dump(recv_buffer, recv_len);)
    _D_(me6e_print_packet(recv_buffer);)


    // 既にデカプセル化されているので、そのままStubNWへ送信
    ssize_t         send_len    = recv_len;
    char*           send_buffer = recv_buffer;

    me6e_device_t* send_dev    =
                    &(CAPSULING_FIELD(self)->handler->conf->capsuling->tunnel_device);

    // デカプセル化したデータを送信
    if((send_len = write(send_dev->option.tunnel.fd, send_buffer, send_len)) < 0){
        me6e_inc_decapsuling_failure_count(CAPSULING_FIELD(self)->handler->stat_info);
        me6e_logging(LOG_ERR, "fail to send decapsuling packet : %s\n", strerror(errno));
        return false;
    }
    else{
        me6e_inc_decapsuling_success_count(CAPSULING_FIELD(self)->handler->stat_info);
        DEBUG_LOG("forward %d bytes to decap\n", send_len);
    }

    return  true;
}

