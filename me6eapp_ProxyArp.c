/******************************************************************************/
/* ファイル名 : me6eapp_ProxyArp.c                                            */
/* 機能概要   : Proxy ARPクラス ソースファイル                                */
/* 修正履歴   : 2013.01.11 S.Yoshikawa 新規作成                               */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
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
#include <arpa/inet.h>
#include <net/if_arp.h>
#include <netinet/ether.h>
#include <pthread.h>
#include <errno.h>


#include "me6eapp.h"
#include "me6eapp_config.h"
#include "me6eapp_statistics.h"
#include "me6eapp_util.h"
#include "me6eapp_log.h"
#include "me6eapp_timer.h"
#include "me6eapp_hashtable.h"
#include "me6eapp_IProcessor.h"
#include "me6eapp_ProxyArp.h"
#include "me6eapp_ProxyArp_data.h"
#include "me6eapp_network.h"		// MACフィルタ対応 2016/09/08 add


// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

///////////////////////////////////////////////////////////////////////////////
//! ProxyArpクラスのフィールド定義
///////////////////////////////////////////////////////////////////////////////
struct ProxyArpField{
        struct me6e_handler_t  *handler;               ///< ME6Eのアプリケーションハンドラー
};
typedef struct ProxyArpField ProxyArpField;

///////////////////////////////////////////////////////////////////////////////
//! ARPパケット解析ヘッダ定義
///////////////////////////////////////////////////////////////////////////////
// パース用の型定義(4バイト境界を跨ぐのでpacked属性を付ける)
typedef struct _arp_ether_ip_t {
    unsigned char ar_sha[ETH_ALEN];                 ///< Sender hardware address
    in_addr_t     ar_sip;                           ///< Sender IP address
    unsigned char ar_tha[ETH_ALEN];                 ///< Target hardware address
    in_addr_t     ar_tip;                           ///< Target IP address
} __attribute__ ((packed)) arp_ether_ip_t;


///////////////////////////////////////////////////////////////////////////////
//! ARPパケット解析データ構造
///////////////////////////////////////////////////////////////////////////////
struct me6eapp_arp_analyze_t {
    unsigned char  src_hw_addr[ETH_ALEN];           ///< 送信元リンクレイヤーアドレス
    unsigned char  dst_hw_addr[ETH_ALEN];           ///< 送信先リンクレイヤーアドレス
    unsigned short hw_type;                         ///< ハードウェアタイプ
    unsigned short proto_type;                      ///< プロトコルタイプ
    unsigned char  hw_size;                         ///< リンクレイヤーフレームサイズ
    unsigned char  proto_size;                      ///< プロトコルサイズ
    unsigned short opcode;                          ///< オペレーションコード
    unsigned char  sender_hw_addr[ETH_ALEN];        ///< 送信元リンクレイヤーアドレス
    struct in_addr sender_proto_addr;               ///< 送信元プロトコルアドレス
    unsigned char  target_hw_addr[ETH_ALEN];        ///< ターゲットリンクレイヤーアドレス
    struct in_addr target_proto_addr;               ///< ターゲットプロトコルアドレス
};
typedef struct me6eapp_arp_analyze_t me6eapp_arp_analyze;

////////////////////////////////////////////////////////////////////////////////
//! Proxy ARPテーブル 設定結果
////////////////////////////////////////////////////////////////////////////////
enum arp_table_set_result
{
    ME6E_ARP_SET_NEW,              ///< 新規追加
    ME6E_ARP_SET_UPDATE,           ///< 更新
    ME6E_ARP_ENTRY_STATIC,         ///< staticエントリーのため、処理なし
    ME6E_ARP_SET_RESULT_MAX        ///< 異常値
};

///////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
///////////////////////////////////////////////////////////////////////////////
static bool ProxyArp_Init(IProcessor* self, struct me6e_handler_t* handler);
static void ProxyArp_Release(IProcessor* proc);
static bool ProxyArp_RecvFromStub(IProcessor* self, char* recv_buffer, ssize_t recv_len);
static bool ProxyArp_RecvFromBackbone(IProcessor* self, char* recv_buffer, ssize_t recv_len);

static inline void ProxyArp_arp_analyze_dump(me6eapp_arp_analyze* arp);
static inline bool ProxyArp_arp_parse_packet(const char* packet, me6eapp_arp_analyze* arp);
static inline bool ProxyArp_arp_send_reply( me6eapp_arp_analyze* arp,
                const struct ether_addr* target_mac, const struct ether_addr* physical_mac, int send_fd);
static inline me6e_proxy_arp_t*  ProxyArp_init_arp_table(me6e_config_proxy_arp_t* conf);
static inline void ProxyArp_add_static_entry(const char* key, const void* value, void* userdata);
static inline void ProxyArp_end_arp_table(me6e_proxy_arp_t* handler);
static inline void ProxyArp_print_table_line(const char* key, const void* value, void* userdata);
static inline enum arp_table_set_result ProxyArp_arp_entry_set(me6e_proxy_arp_t*  handler,
                const struct in_addr* v4addr, const struct ether_addr*  macaddr);
static inline int ProxyArp_arp_entry_get(me6e_proxy_arp_t* handler,
                const struct in_addr*   v4daddr, struct ether_addr* macaddr);
static inline void ProxyArp_timeout_cb(const timer_t timerid, void* data);


///////////////////////////////////////////////////////////////////////////////
//! ProxyArpのフィールドアクセス用マクロ
///////////////////////////////////////////////////////////////////////////////
#define PROXYARP_FIELD(ptr)((ProxyArpField*)(ptr->data))

///////////////////////////////////////////////////////////////////////////////
//! @brief Proxy ARP インスタンス生成関数
//!
//! ProxyArpインスタンスを生成する。
//!
//! @return IProcessor構造体へのポインター
///////////////////////////////////////////////////////////////////////////////
IProcessor* ProxyArp_New()
{
    IProcessor* instance = NULL;

    DEBUG_LOG("ProxyArp_New start.");

    // インスタンスの生成
    instance = malloc(sizeof(IProcessor));
    if (instance == NULL) {
        me6e_logging(LOG_ERR, "fail to malloc for Proxy ARP instance.");
        return NULL;
    }

    memset(instance, 0, sizeof(IProcessor));

    // フィールドの生成
    instance->data = malloc(sizeof(ProxyArpField));
    if (instance->data== NULL) {
        me6e_logging(LOG_ERR, "fail to malloc for Proxy ARP instance field.");
        return NULL;
    }
    memset(instance->data, 0, sizeof(ProxyArpField));

    // メソッドの登録
    instance->init               = ProxyArp_Init;
    instance->release            = ProxyArp_Release;
    instance->recv_from_stub     = ProxyArp_RecvFromStub;
    instance->recv_from_backbone = ProxyArp_RecvFromBackbone;

    DEBUG_LOG("ProxyArp_New end.");
    return instance;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Proxy ARP インスタンス初期化関数
//!
//! ProxyArpインスタンスのフィールドを初期化する
//!
//! @param [in] sefl IProcessor構造体
//! @param [in] handler アプリケーションハンドラー
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool ProxyArp_Init(IProcessor* self, struct me6e_handler_t *handler)
{
    DEBUG_LOG("ProxyArp_Init start.\n");

    // 引数チェック
    if ( (handler == NULL) || (self == NULL) ) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyArp_Init).");
        return false;
    }

    // ハンドラーの設定
    PROXYARP_FIELD(self)->handler = handler;

    // ProxARPテーブルの初期化
    handler->proxy_arp_handler = ProxyArp_init_arp_table(handler->conf->arp);
    if (handler->proxy_arp_handler == NULL) {
        me6e_logging(LOG_ERR, "fail to create proxy arp table.");
        return false;
    }

    DEBUG_LOG("ProxyArp_Init end.\n");
    return  true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Proxy ARP インスタンス解放関数
//!
//! ProxyArpインスタンスが保持するリソースを解放する。
//!
//! @param [in] self IProcessor構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static void ProxyArp_Release(IProcessor* self)
{
    DEBUG_LOG("ProxyArp_Release start.\n");

    // 引数チェック
    if (self == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyArp_Release).");
        return;
    }

    // ProxARPテーブルの解放
    if (PROXYARP_FIELD(self)->handler != NULL ){
        if (PROXYARP_FIELD(self)->handler->proxy_arp_handler != NULL) {
            ProxyArp_end_arp_table(PROXYARP_FIELD(self)->handler->proxy_arp_handler);
        }
    }

    // フィールドの解放
    free(PROXYARP_FIELD(self));

    // インスタンスの解放
    free(self);

    DEBUG_LOG("ProxyArp_Release end.\n");
    return;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief StubNWパケット受信処理関数
//!
//! StubNWから受信したARP Requestに対して、ARP Replyを返す。
//!
//! @param [in] self IProcessor構造体
//! @param [in] recv_buffer 受信データ
//! @param [in] recv_len    受信データのサイズ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool ProxyArp_RecvFromStub(IProcessor* self, char* recv_buffer, ssize_t recv_len)
{
    // ローカル変数宣言
    bool isContinue = true;
    struct ethhdr*  p_orig_eth_hdr;
    int fd;

    DEBUG_LOG("ProxyArp_RecvFromStub\n");

    // 引数チェック
    if ((self == NULL) || (recv_buffer == NULL) || (recv_len <= 0)) {
       me6e_logging(LOG_ERR, "Parameter Check NG(ProxyArp_RecvFromStub).");
        return false;
    }

    // ローカル変数初期化
    p_orig_eth_hdr = (struct ethhdr*)recv_buffer;

    // ARPパケットのみ処理
    if(p_orig_eth_hdr->h_proto != htons(ETH_P_ARP)){
        DEBUG_LOG("Not ARP packet.\n");
        // 次のクラスの処理を継続
        return true;
    }

    DEBUG_LOG("recv ARP packet.\n");

    // ハードウェアタイプがEthernet(1)で、プロトコルタイプがIP(0x0800)のみ処理
    me6eapp_arp_analyze arp;
    if(!ProxyArp_arp_parse_packet(recv_buffer, &arp)){
        me6e_inc_disease_not_arp_request_recv_count(PROXYARP_FIELD(self)->handler->stat_info);
        me6e_logging(LOG_INFO, "Not hw type ETHER. or Not protocol type IP.");
        // パケット破棄
        return false;
    }

    _D_(ProxyArp_arp_analyze_dump(&arp);)

    // 送信元ハードウェアアドレスがユニキャストのみ処理
    if ((me6e_util_is_broadcast_mac(arp.sender_hw_addr))
            || (me6e_util_is_multicast_mac(arp.sender_hw_addr)) ){
        me6e_logging(LOG_INFO, "sender mac address not nunicast.");
        // パケット破棄
        return false;
    }

    // 送信元プロトコルアドレスがユニキャストのみ処理
    if (IN_MULTICAST(arp.sender_proto_addr.s_addr)
            || (arp.sender_proto_addr.s_addr == INADDR_BROADCAST)){
        me6e_logging(LOG_INFO, "sender ipv4 address not unicast.");
        // パケット破棄
        return false;
    }

    // 宛先プロトコルアドレスがブロードキャスト以外処理
    if (arp.target_proto_addr.s_addr == INADDR_BROADCAST){
        me6e_logging(LOG_INFO, "target ipv4 address broadcast.");
        // パケット破棄
        return false;
    }

    // ARP Requestのみ処理
    if (arp.opcode == ARPOP_REQUEST) {
        DEBUG_LOG("recv ARP REQUEST.\n");

        // target IPアドレスに対応するMACを検索
        struct ether_addr macaddr;
        int ret = ProxyArp_arp_entry_get(PROXYARP_FIELD(self)->handler->proxy_arp_handler,
               &(arp.target_proto_addr), &macaddr);

        if(ret == 0 ){
            DEBUG_LOG("ARP REQUEST Match.\n");
            me6e_inc_arp_request_recv_count(PROXYARP_FIELD(self)->handler->stat_info);

            // ARPリプライ返信
            fd = PROXYARP_FIELD(self)->handler->conf->capsuling->tunnel_device.option.tunnel.fd;
            // MACフィルタ対応 2016/09/08 chg start
            //if(ProxyArp_arp_send_reply(&arp, &macaddr, &macaddr_bridge, fd)) {
            if(ProxyArp_arp_send_reply(&arp, &macaddr, PROXYARP_FIELD(self)->handler->conf->capsuling->bridge_hwaddr, fd)) {
            // MACフィルタ対応 2016/09/08 chg end
                me6e_inc_arp_reply_send_count(PROXYARP_FIELD(self)->handler->stat_info);
            } else {
                me6e_inc_arp_reply_send_err_count(PROXYARP_FIELD(self)->handler->stat_info);
            }

            // 動的エントリー機能動作有無判定
            if ((PROXYARP_FIELD(self)->handler->conf->arp->arp_entry_update)) {
                // 処理を継続(ProxyARPの動的ARPエントリ更新が有効であれば、ARP Requestは転送する)
                isContinue = true;
            } else {
                // 処理を継続しない。
                isContinue = false;
            }

        } else {
            DEBUG_LOG("ARP REQUEST NO Match.\n");
            // 次のクラスの処理を継続
            isContinue = true;
        }
    }

    return  isContinue;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief BackBoneパケット受信処理関
//!
//! BackboneNWから受信したパケットがARP Request/Replyの場合
//! ARPテーブルへ登録する。
//!
//! @param [in] self IProcessor構造体
//! @param [in] recv_buffer 受信データ
//! @param [in] recv_len    受信データのサイズ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool ProxyArp_RecvFromBackbone(IProcessor* self, char* recv_buffer, ssize_t recv_len)
{
    DEBUG_LOG("ProxyArp_RecvFromBackbone\n");

    // 引数チェック
    if ((self == NULL) || (recv_buffer == NULL) || (recv_len <= 0)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyArp_RecvFromBackbone).");
        return false;
    }

    // 動的エントリー機能動作有無判定
    if (!(PROXYARP_FIELD(self)->handler->conf->arp->arp_entry_update)) {
        // 次のクラスの処理を継続
        return true;
    }

    DEBUG_LOG("ProxyArp dynamic entry enable route start.\n");

    struct ethhdr*  p_orig_eth_hdr = (struct ethhdr*)recv_buffer;

    // ARPパケットのみ処理
    if(p_orig_eth_hdr->h_proto != htons(ETH_P_ARP)){
        DEBUG_LOG("Not ARP packet.\n");
        // 次のクラスの処理を継続
        return true;
    }

    // ハードウェアタイプがEthernet(1)で、プロトコルタイプがIP(0x0800)のみ処理
    me6eapp_arp_analyze arp;
    if(!ProxyArp_arp_parse_packet(recv_buffer, &arp)){
        me6e_logging(LOG_INFO, "Not hw type ETHER. or Not protocol type IP.");
        // 次のクラスの処理を継続
        return true;
    }

    _D_(ProxyArp_arp_analyze_dump(&arp);)

    // ARP request/replyのみ処理
    if ((arp.opcode == ARPOP_REQUEST) || (arp.opcode == ARPOP_REPLY)) {
        DEBUG_LOG("Match ARP %s.\n", arp.opcode == ARPOP_REQUEST ? "Request": "Reply");

        // 送信元ハードウェアアドレスがユニキャストのみ処理
        if ((me6e_util_is_broadcast_mac(arp.sender_hw_addr))
                || (me6e_util_is_multicast_mac(arp.sender_hw_addr)) ){
            me6e_logging(LOG_INFO, "sender mac address not unicast.");
            // 次のクラスの処理を継続
            return true;
        }

        // 送信元プロトコルアドレスがユニキャストのみ処理
        if (IN_MULTICAST(arp.sender_proto_addr.s_addr)
                || (arp.sender_proto_addr.s_addr == INADDR_BROADCAST)){
            me6e_logging(LOG_INFO, "sender ipv4 address not unicast.");
            // 次のクラスの処理を継続
            return true;
        }

        // Proxy ARP テーブルへの登録
        enum arp_table_set_result ret = ProxyArp_arp_entry_set(
                PROXYARP_FIELD(self)->handler->proxy_arp_handler,
                &arp.sender_proto_addr,
                (struct ether_addr*)arp.sender_hw_addr);

        // ARP Replyで、静的/動的に関わらず、エントリーがあれば、破棄する。
        if (arp.opcode == ARPOP_REPLY) {
            if ((ret == ME6E_ARP_SET_UPDATE) || (ret == ME6E_ARP_ENTRY_STATIC)) {
                return false;
            }
        }

    } else {
        DEBUG_LOG("ARP other opecode = %d.\n", arp.opcode);
    }

    return  true;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief ARPパケット解析処理関数
//!
//! ARPパケットを解析する。
//!
//! @param [in]     packet  ARPパケット
//! @param [out]    arp     ARP解析データ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static inline bool ProxyArp_arp_parse_packet(
    const char*     packet,
    me6eapp_arp_analyze* arp
)
{
    // ローカル変数宣言
    struct ethhdr* p_ether;
    struct arphdr* p_arp;
    bool           result;

    // 引数チェック
    if((arp == NULL) || (packet == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyArp_arp_parse_packet).");
        return false;
    }

    // ローカル変数初期化
    p_ether = (struct ethhdr*)(packet);
    p_arp   = (struct arphdr*)(p_ether + 1);
    result  = true;

    memcpy(arp->src_hw_addr, p_ether->h_source, ETH_ALEN);
    memcpy(arp->dst_hw_addr, p_ether->h_dest,   ETH_ALEN);
    arp->hw_type            = ntohs(p_arp->ar_hrd);
    arp->proto_type         = ntohs(p_arp->ar_pro);
    arp->hw_size            = p_arp->ar_hln;
    arp->proto_size         = p_arp->ar_pln;
    arp->opcode             = ntohs(p_arp->ar_op);

    // ハードウェアタイプがEthernet(1)で、プロトコルタイプがIP(0x0800)のみ処理
    if((arp->hw_type == ARPHRD_ETHER) && (arp->proto_type == ETH_P_IP)){
        arp_ether_ip_t* p_arp_eth_ip = (arp_ether_ip_t*)(p_arp + 1);
        memcpy(arp->sender_hw_addr, p_arp_eth_ip->ar_sha, ETH_ALEN);
        arp->sender_proto_addr.s_addr = p_arp_eth_ip->ar_sip;
        memcpy(arp->target_hw_addr, p_arp_eth_ip->ar_tha, ETH_ALEN);
        arp->target_proto_addr.s_addr = p_arp_eth_ip->ar_tip;
    }
    else{
        // Ether/IP以外のarpは対象外
        result = false;
    }

    return result;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief ARP リプライ送信処理関数
//!
//! ARP リプライを送信する
//!
//! @param [in]     arp         ARP解析データ
//! @param [in]     target_mac  ターゲットMACアドレス
//! @param [in]     send_fd     送信用ファイルディスクリプタ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static inline bool ProxyArp_arp_send_reply(
    me6eapp_arp_analyze*       arp,
    const struct ether_addr*    target_mac,
    const struct ether_addr*    physical_mac,
    int                         send_fd
)
{
    // ローカル変数宣言
    struct ethhdr     eth_header;
    struct arphdr     arp_header;
    arp_ether_ip_t    arp_eth_ip;

    // 引数チェック
    if((arp == NULL) || (target_mac == NULL) || (send_fd < 0)){
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyArp_arp_send_reply).");
        return false;
    }

    // Ethernetヘッダ設定
    //memcpy(eth_header.h_source, target_mac->ether_addr_octet, ETH_ALEN);
    memcpy(eth_header.h_source, physical_mac->ether_addr_octet, ETH_ALEN);
    memcpy(eth_header.h_dest,   arp->src_hw_addr,    ETH_ALEN);
    eth_header.h_proto = htons(ETH_P_ARP);

    // ARPヘッダ設定
    arp_header.ar_hrd = htons(arp->hw_type);
    arp_header.ar_pro = htons(arp->proto_type);
    arp_header.ar_hln = arp->hw_size;
    arp_header.ar_pln = arp->proto_size;
    arp_header.ar_op  = htons(ARPOP_REPLY);

    // ARP Reply情報設定
    memcpy(arp_eth_ip.ar_sha, target_mac->ether_addr_octet, ETH_ALEN);
    arp_eth_ip.ar_sip = arp->target_proto_addr.s_addr;
    memcpy(arp_eth_ip.ar_tha, arp->sender_hw_addr, ETH_ALEN);
    arp_eth_ip.ar_tip = arp->sender_proto_addr.s_addr;

    // Scatter/Gather設定
    struct iovec iov[3];
     // 配列0に、Etherヘッダを格納
    iov[0].iov_base = &eth_header;
    iov[0].iov_len  = sizeof(struct ethhdr);
    // 配列1に、ARPヘッダを格納
    iov[1].iov_base = &arp_header;
    iov[1].iov_len  = sizeof(struct arphdr);
    // 配列2に、ARP Reply情報を格納
    iov[2].iov_base = &arp_eth_ip;
    iov[2].iov_len  = sizeof(arp_eth_ip);

    // パケット送信
    if(writev(send_fd, iov, 3) < 0){
        me6e_logging(LOG_ERR, "fail to send ARP Reply packet %s.", strerror(errno));
        return false;
    }
    else{
        DEBUG_LOG("sent ARP Reply packet\n");
        return true;
    }
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ARP解析結果出力関数
//!
//! ARP解析データを出力する。
//!
//! @param [in] arp ARP解析データ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static inline void ProxyArp_arp_analyze_dump(me6eapp_arp_analyze* arp)
{
    char macaddrstr[MAC_ADDRSTRLEN] = { 0 };
    char addr[INET6_ADDRSTRLEN] = { 0 };

    DEBUG_LOG("----Ether Header------------------\n");
    DEBUG_LOG("Ether SrcAdr = %s\n", ether_ntoa_r((struct ether_addr*)(arp->src_hw_addr), macaddrstr));
    DEBUG_LOG("Ether DstAdr = %s\n", ether_ntoa_r((struct ether_addr*)(arp->dst_hw_addr), macaddrstr));
    DEBUG_LOG("----ARP Header--------------------\n");
    DEBUG_LOG("Hardware type      : 0x%04x\n", arp->hw_type);
    DEBUG_LOG("Protocol type      : 0x%04x\n", arp->proto_type);
    DEBUG_LOG("Hardware size      : %u\n",     arp->hw_size);
    DEBUG_LOG("Protocol size      : %u\n",     arp->proto_size);
    DEBUG_LOG("Opcode             : 0x%04x\n", arp->opcode);
    DEBUG_LOG("Sender MAC address : %s\n", ether_ntoa_r((struct ether_addr*)(arp->sender_hw_addr), macaddrstr));
    DEBUG_LOG("Sender IP address  : %s\n", inet_ntop(AF_INET, &(arp->sender_proto_addr), addr, sizeof(addr)));
    DEBUG_LOG("Target MAC address : %s\n", ether_ntoa_r((struct ether_addr*)(arp->target_hw_addr), macaddrstr));
    DEBUG_LOG("Target IP address  : %s\n", inet_ntop(AF_INET, &(arp->target_proto_addr), addr, sizeof(addr)));

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Proxy ARP テーブル初期化関数
//!
//! Config情報から、ARP テーブル作成を行う。
//!
//! @param [in]  conf       config情報
//!
//! @return 生成したProxy ARP管理構造体へのポインタ
///////////////////////////////////////////////////////////////////////////////
static inline me6e_proxy_arp_t*  ProxyArp_init_arp_table(me6e_config_proxy_arp_t* conf)
{
    DEBUG_LOG("Proxy ARP table init\n");
    // 引数チェック
    if (conf == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyArp_init_arp_table).");
        return NULL;
    }

    me6e_proxy_arp_t* handler = malloc(sizeof(me6e_proxy_arp_t));
    if(handler == NULL){
       me6e_logging(LOG_ERR, "fail to malloc for me6e_proxy_arp_t.");
        return NULL;
    }

    memset(handler, 0, sizeof(handler));

    // hash table作成
    handler->table = me6e_hashtable_create(conf->arp_entry_max);
    if(handler->table == NULL){
        me6e_logging(LOG_ERR, "fail to hash table for me6e_proxy_arp_t.");
        free(handler);
        return NULL;
    }

    // 静的エントリーをテーブルに格納
    me6e_hashtable_foreach(conf->arp_static_entry, ProxyArp_add_static_entry, handler->table);

    // timer作成
    handler->timer_handler = me6e_init_timer();
    if(handler->timer_handler == NULL){
        me6e_hashtable_delete(handler->table);
        free(handler);
        me6e_logging(LOG_ERR, "ProxyARP fail to init timer.");
        return NULL;
    }

    // config情報保持
    handler->conf = conf;

    // 排他制御初期化
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init(&handler->mutex, &attr);

    _D_(ProxyArp_print_table(handler, STDOUT_FILENO);)

    return handler;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief  Proxy ARP テーブル終了関数
//!
//! テーブルの解放を行う。
//!
//! @param [in]  handler Proxy ARP管理構造体
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static inline void ProxyArp_end_arp_table(me6e_proxy_arp_t* handler)
{
    DEBUG_LOG("Proxy ARP table end\n");

    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyArp_end_arp_table).");
        return;
    }

    // 排他開始
    DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
    pthread_mutex_lock(&handler->mutex);

    // timer解除
    if (handler->timer_handler != NULL) {
        me6e_end_timer(handler->timer_handler);
    }

    // hash table削除
    if (handler->table != NULL) {
        me6e_hashtable_delete(handler->table);
    }

    // 排他解除
    pthread_mutex_unlock(&handler->mutex);
    DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    // 排他制御終了
    pthread_mutex_destroy(&handler->mutex);

    free(handler);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 静的ARPエントリー登録関数(ハッシュテーブルコールバック)
//!
//! Config情報の静的ARPエントリーを登録する。
//!
//! @param [in]     key       テーブルに登録されているキー
//! @param [in]     value     キーに対応する値
//! @param [in]     userdata  コールバック登録時に指定したユーザデータ
//!                           (ハッシュテーブルへのポインタ)
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static inline void ProxyArp_add_static_entry(const char* key, const void* value, void* userdata)
{
    bool                res     = false;
    me6e_hashtable_t*  table   = NULL;
    proxy_arp_data_t    data;

    // 引数チェック
    if ((key == NULL) || (value == NULL) || (userdata == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyArp_add_static_entry).");
        return;
    }

    table = (me6e_hashtable_t*)userdata;

    data.type = ME6E_PROXY_ARP_TYPE_STATIC;
    data.timerid = NULL;

    res = parse_macaddress(value, &(data.ethr_addr));
    if (!res) {
        me6e_logging(LOG_ERR, "fail to parse_macaddress.");
        return;
    }

    res = me6e_hashtable_add(table, key, &data, sizeof(data), true, NULL, NULL);
    if(!res){
        me6e_logging(LOG_ERR, "fail to add hashtable entry.");
        return;
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ハッシュテーブル内部情報出力関数
//!
//! Proxy ARP管理内のテーブルを出力する
//!
//! @param [in]  handler    Proxy ARP管理構造体
//! @param [in]  fd         出力先のディスクリプタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void ProxyArp_print_table(me6e_proxy_arp_t* handler, int fd)
{
    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyArp_print_table).");
        return;
    }

    DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
    pthread_mutex_lock(&handler->mutex);

    proxy_arp_print_data data = {
        .timer_handler = handler->timer_handler,
        .fd            = fd
    };

    dprintf(fd, "\n");
    dprintf(fd, "--------------------+--------------------+-------------\n");
    dprintf(fd, "    IPv4 Address    |     MAC Address    | aging time  \n");
    dprintf(fd, "--------------------+--------------------+-------------\n");
    me6e_hashtable_foreach(handler->table, ProxyArp_print_table_line, &data);
    dprintf(fd, "--------------------+--------------------+-------------\n");
    dprintf(fd, "\n");

    pthread_mutex_unlock(&handler->mutex);
    DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ハッシュテーブル出力コールバック関数
//!
//! ハッシュテーブル出力時にコールバック登録する関数
//!
//! @param [in]     key       テーブルに登録されているキー
//! @param [in]     value     キーに対応する値
//! @param [in]     userdata  コールバック登録時に指定したユーザデータ
//!                           (proxy_arp_print_data構造体)
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static inline void ProxyArp_print_table_line(const char* key, const void* value, void* userdata)
{
    char macaddrstr[MAC_ADDRSTRLEN] = { 0 };

    // 引数チェック
    if((key == NULL) || (value == NULL) || (userdata == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyArp_print_table_line).");
        return;
    }

    proxy_arp_print_data* print_data = (proxy_arp_print_data*)userdata;
    proxy_arp_data_t*   data       = (proxy_arp_data_t*)value;

    // 残り時間出力
    struct itimerspec tmspec;
    if(data->timerid != NULL){
        me6e_timer_get(print_data->timer_handler, data->timerid, &tmspec);

        // 出力
        dprintf(print_data->fd, "%-20s|%-20s|%12ld\n",
                key, ether_ntoa_r(&(data->ethr_addr), macaddrstr), tmspec.it_value.tv_sec);
    }
    else{
        // 出力
        dprintf(print_data->fd, "%-20s|%-20s|%12s\n",
                key, ether_ntoa_r(&(data->ethr_addr), macaddrstr), "static");
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ARP テーブル設定関数
//!
//! ARP テーブルにエントリーを新規追加/更新する。
//!
//! @param [in]     handler    Proxy ARP管理構造体
//! @param [in]     v4addr     ターゲットIPv4アドレス
//! @param [in]     macaddr    ターゲットMACアドレス
//!
//! @return ME6E_ARP_SET_NEW           新規追加
//! @return ME6E_ARP_SET_UPDATE        更新
//! @return ME6E_ARP_ENTRY_STATIC      Staticエントリーのため、処理なし
//! @return ME6E_ARP_SET_RESULT_MAX    異常
///////////////////////////////////////////////////////////////////////////////
static inline enum arp_table_set_result ProxyArp_arp_entry_set(
    me6e_proxy_arp_t*  handler,
    const struct in_addr*     v4addr,
    const struct ether_addr*  macaddr
)
{
    char dst_addr[INET_ADDRSTRLEN] = { 0 };
    char macaddrstr[MAC_ADDRSTRLEN] = { 0 };
    int  result;
    enum arp_table_set_result  ret = ME6E_ARP_SET_RESULT_MAX;

    // 引数チェック
    if ((handler == NULL) || (v4addr == NULL) || (macaddr == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyArp_arp_entry_set).");
        return ret;
    }

    DEBUG_LOG("ARP Entry set v4 addr = %s, MAC addr = %s.\n",
            inet_ntop(AF_INET, v4addr, dst_addr, sizeof(dst_addr)),
            ether_ntoa_r(macaddr, macaddrstr));

    // 文字列へ変換
    if (inet_ntop(AF_INET, v4addr, dst_addr, sizeof(dst_addr)) == NULL) {
        me6e_logging(LOG_ERR, "fail to inet_ntop.");
        return ret;
    }

    // 排他開始
    DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
    pthread_mutex_lock(&handler->mutex);

    // テーブルから対象の情報を取得
    proxy_arp_data_t* data = me6e_hashtable_get(handler->table, dst_addr);

    if(data != NULL){
        // 一致する情報がある場合
        DEBUG_LOG("mach arp entry.\n");

        if (data->type == ME6E_PROXY_ARP_TYPE_DYNAMIC) {
            // 動的エントリーの場合
            DEBUG_LOG("Success to update dynamic arp entry.\n");
            // 処理結果を更新へ設定
            ret = ME6E_ARP_SET_UPDATE;
            if(data->timerid != NULL){
                // タイマが起動中の場合は再設定
                DEBUG_LOG("timer exist.\n");
                result = me6e_timer_reset(
                    handler->timer_handler,
                    data->timerid,
                    handler->conf->arp_aging_time
                );
            }
            else{
                // タイマが起動中で無い場合はタイマ起動
                DEBUG_LOG("timer no exist.\n");
                proxy_arp_timer_cb_data_t* cb_data = malloc(sizeof(proxy_arp_timer_cb_data_t));
                if(cb_data != NULL){
                    cb_data->handler = handler;
                    strcpy(cb_data->dst_addr, dst_addr);

                    result = me6e_timer_register(
                        handler->timer_handler,
                        handler->conf->arp_aging_time,
                        ProxyArp_timeout_cb,
                        cb_data,
                        &data->timerid
                    );
                }
                else{
                    me6e_logging(LOG_WARNING, "fail to allocate timer callback data.");
                    result = -1;
                }
            }
            if(result != 0){
                me6e_logging(LOG_WARNING, "fail to reset proxy arp timer.");
                // タイマを停止に設定
                data->timerid = NULL;
            }
        }
        else {
            DEBUG_LOG("static arp entry ignore...\n");
            // 処理結果を処理なしへ設定
            ret = ME6E_ARP_ENTRY_STATIC;
        }
    }
    else{
        // 一致する情報がない場合
        DEBUG_LOG("New arp entry.\n");
        // 処理結果を新規追加へ設定
        ret = ME6E_ARP_SET_NEW;

        // 新規追加データ設定
        proxy_arp_data_t data = {.ethr_addr = *macaddr,
                                 .type      = ME6E_PROXY_ARP_TYPE_DYNAMIC,
                                 .timerid = NULL};

        // タイマアウト時に通知される情報のメモリ確保
        proxy_arp_timer_cb_data_t* cb_data = malloc(sizeof(proxy_arp_timer_cb_data_t));
        if(cb_data != NULL){
            cb_data->handler = handler;
            strcpy(cb_data->dst_addr, dst_addr);

            // タイマ登録
            result = me6e_timer_register(
                handler->timer_handler,
                handler->conf->arp_aging_time,
                ProxyArp_timeout_cb,
                cb_data,
                &data.timerid
            );
        }
        else{
            me6e_logging(LOG_WARNING, "fail to allocate timer callback data.");
            result = -1;
        }

        if(result == 0){
            // データ追加処理
            if(me6e_hashtable_add(handler->table, dst_addr, &data, sizeof(data), true, NULL, NULL)){
                DEBUG_LOG("Success to add new dynamic arp entry.\n");
                result = 0;
            }
            else{
                me6e_logging(LOG_WARNING, "fail to add new arp entry.");
                // 既存のタイマ解除
                me6e_timer_cancel(handler->timer_handler, data.timerid, NULL);
                result = -1;
            }
        }

        if(result != 0){
            me6e_logging(LOG_INFO, "proxy_arp_timer set failed.");
            // コールバックデータ解放
            free(cb_data);
        }
    }

    // 排他解除
    pthread_mutex_unlock(&handler->mutex);
    DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    return ret;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ターゲットMACアドレス取得関数
//!
//! ARP テーブルから、IPv4アドレスをキーに、MACアドレスを取得する
//!
//! @param [in]     handler    Proxy ARP管理構造体
//! @param [in]     v4daddr    ターゲットIPv4アドレス
//! @param [out]    macaddr    ターゲットMACアドレス
//!
//! @return  0      正常
//! @return  0以外  異常
///////////////////////////////////////////////////////////////////////////////
static inline int ProxyArp_arp_entry_get(
    me6e_proxy_arp_t*      handler,
    const struct in_addr*   v4daddr,
    struct ether_addr*      macaddr
)
{
    char                dst_addr[INET_ADDRSTRLEN] = { 0 };
    char                macaddrstr[MAC_ADDRSTRLEN] = { 0 };
    proxy_arp_data_t*   data = NULL;
    int                 result = 0;

    // パラメタチェック
    if((handler == NULL) || (v4daddr == NULL) || (macaddr == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyArp_arp_entry_get).");
        return -1;
    }

    // テーブル情報ログ出力
    _D_(ProxyArp_print_table(handler, STDOUT_FILENO);)

    // 排他開始
    DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
    pthread_mutex_lock(&handler->mutex);

    // IPv4アドレス文字列変換
    inet_ntop(AF_INET, v4daddr, dst_addr, sizeof(dst_addr));

    // v4アドレスをkeyにデータ検索
    data = me6e_hashtable_get(handler->table, dst_addr);
    if(data == NULL){
        DEBUG_LOG("Unmach arp etnry. target(%s)\n", dst_addr);
        result =  -1;
    } else {
        *macaddr = data->ethr_addr;
        DEBUG_LOG("Mach arp etnry. %s : %s\n", dst_addr, ether_ntoa_r(macaddr, macaddrstr));
        result =  0;
    }

    // 排他解除
    pthread_mutex_unlock(&handler->mutex);
    DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Proxy ARP 保持タイムアウトコールバック関数
//!
//! ARP テーブルのエントリーを削除する。
//!
//! @param [in]     timerid   タイマ登録時に払い出されたタイマID
//! @param [in]     data      タイマ登録時に設定したデータ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static inline void ProxyArp_timeout_cb(const timer_t timerid, void* data)
{

    if(data == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyArp_timeout_cb).");
        return;
    }

    proxy_arp_timer_cb_data_t* cb_data = (proxy_arp_timer_cb_data_t*)data;

    // 排他開始
    DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
    pthread_mutex_lock(&cb_data->handler->mutex);

    proxy_arp_data_t* arp_data = me6e_hashtable_get(cb_data->handler->table, cb_data->dst_addr);

    if(arp_data != NULL) {
        // エントリーあり
        if (arp_data->type == ME6E_PROXY_ARP_TYPE_DYNAMIC) {
            // 動的エントリーの場合、データを削除
            DEBUG_LOG("arp entry del. dst(%s)\n", cb_data->dst_addr);
            if(arp_data->timerid != timerid){
                // timeridの値が異なる場合、警告表示(データは一応削除する)
                me6e_logging(LOG_WARNING, "callback timerid different in proxy arp table.");
                me6e_logging(LOG_WARNING,
                    "callback timerid = %d, proxy arp table timerid = %d, addr = %s.",
                    timerid, arp_data->timerid, cb_data->dst_addr
                );
            }
            me6e_hashtable_remove(cb_data->handler->table, cb_data->dst_addr, NULL);
        }
        else{
            // 静的エントリーの場合(ありえないルート)
            me6e_logging(LOG_WARNING, "proxy arp static entry time out. addr = %s.", cb_data->dst_addr);
            arp_data->timerid = NULL;
        }
    }
    else{
        // エントリーなし
        me6e_logging(LOG_WARNING, "proxy arp entry is not found. addr = %s.", cb_data->dst_addr);
    }

    // 排他解除
    pthread_mutex_unlock(&cb_data->handler->mutex);
    DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    free(cb_data);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 静的ARPエントリー登録関数(コマンド用)
//!
//! 静的なARPエントリーを登録する。
//! 既にエントリー(静的/動的に関わらず)がある場合は、登録しない。
//!
//! @param [in]     handler         Proxy ARP管理構造体
//! @param [in]     v4daddr_str     ターゲットIPv4アドレス
//! @param [in]     macadd_strr     ターゲットMACアドレス
//! @param [in]     fd              出力先のディスクリプタ
//!
//! @return  0          正常
//! @return  1          既にエントリーあり
//! @return  上記以外   異常
///////////////////////////////////////////////////////////////////////////////
int ProxyArp_cmd_add_static_entry(
        me6e_proxy_arp_t* handler,
        const char* v4addr_str,
        const char* macaddr_str,
        int fd)
{
    proxy_arp_data_t    data;
    struct in_addr      v4addr;
    char                addr[INET_ADDRSTRLEN];
    bool                res = false;


    // 引数チェック
    if ((handler == NULL) || (v4addr_str == NULL) || (macaddr_str == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyArp_cmd_add_static_entry).");
        return -1;
    }

    if(parse_ipv4address(v4addr_str, &v4addr, NULL) <= 0){
        dprintf(fd, "fail to parse ipv4 address.\n");
        return -1;
    }

    if (!parse_macaddress(macaddr_str, &(data.ethr_addr))) {
        dprintf(fd, "fail to parse mac address.\n");
        return -1;
    }

    data.type = ME6E_PROXY_ARP_TYPE_STATIC;
    data.timerid = NULL;

    // 排他開始
    DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
    pthread_mutex_lock(&handler->mutex);

    // 既にエントリーがあるかチェックする
    proxy_arp_data_t* arp_data = me6e_hashtable_get(handler->table,
                                    inet_ntop(AF_INET, &v4addr, addr, sizeof(addr)));

    if (arp_data != NULL) {
        dprintf(fd, "proxy arp entry is already exists. addr = %s\n", addr);
        // 排他解除
        pthread_mutex_unlock(&handler->mutex);
        DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());
        return 1;
    }

    res = me6e_hashtable_add(handler->table, addr, &data, sizeof(data), true, NULL, NULL);
    if(!res){
        me6e_logging(LOG_ERR, "fail to add hashtable entry.");
        // 排他解除
        pthread_mutex_unlock(&handler->mutex);
        DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());
        return -1;
    }

    // 排他解除
    pthread_mutex_unlock(&handler->mutex);
    DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 静的ARPエントリー削除関数(コマンド用)
//!
//! 静的なARPエントリーを削除する。
//!
//! @param [in]     handler         Proxy ARP管理構造体
//! @param [in]     v4daddr_str     ターゲットIPv4アドレス
//! @param [in]     fd              出力先のディスクリプタ
//!
//! @return  0          正常
//! @return  1          エントリーなし
//! @return  上記以外   異常
///////////////////////////////////////////////////////////////////////////////
int ProxyArp_cmd_del_static_entry(
        me6e_proxy_arp_t* handler,
        const char* v4addr_str,
        int fd)
{
    struct in_addr      v4addr;
    char                addr[INET_ADDRSTRLEN];
    bool                res = false;

    // 引数チェック
    if ((handler == NULL) || (v4addr_str == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyArp_cmd_del_static_entry).");
        return -1;
    }

    if(parse_ipv4address(v4addr_str, &v4addr, NULL) <= 0){
        dprintf(fd, "fail to parse ipv4 address.\n");
        return -1;
    }

    // 排他開始
    DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
    pthread_mutex_lock(&handler->mutex);

    // 既にエントリーがあるかチェックする
    proxy_arp_data_t* arp_data = me6e_hashtable_get(handler->table,
                                    inet_ntop(AF_INET, &v4addr, addr, sizeof(addr)));

    if (arp_data == NULL) {
        dprintf(fd, "proxy arp entry is not exists. addr = %s\n", addr);
        // 排他解除
        pthread_mutex_unlock(&handler->mutex);
        DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());
        return 1;
    }

    res = me6e_hashtable_remove(handler->table, addr, NULL);
    if(!res){
        me6e_logging(LOG_ERR, "fail to delete hashtable entry.");
        // 排他解除
        pthread_mutex_unlock(&handler->mutex);
        DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());
        return -1;
    }

    // 排他解除
    pthread_mutex_unlock(&handler->mutex);
    DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    return 0;
}

