/******************************************************************************/
/* ファイル名 : me6eapp_ProxyNdp.c                                            */
/* 機能概要   : Proxy NDPクラス ソースファイル                                */
/* 修正履歴   : 2013.01.11 S.Yoshikawa 新規作成                               */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <linux/unistd.h>
#include <net/ethernet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if_arp.h>
#include <netinet/ether.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <net/if.h>
#include <pthread.h>
#include <errno.h>

#include "me6eapp.h"
#include "me6eapp_config.h"
#include "me6eapp_statistics.h"
#include "me6eapp_util.h"
#include "me6eapp_log.h"
#include "me6eapp_timer.h"
#include "me6eapp_hashtable.h"
#include "me6eapp_print_packet.h"
#include "me6eapp_IProcessor.h"
#include "me6eapp_ProxyNdp.h"
#include "me6eapp_ProxyNdp_data.h"


// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

// Solicited-nodeマルチキャストアドレスのプレフィックス
#define SOLICITED_NODE_MC_PREFIX    "FF02::1:FF00:0000"

///////////////////////////////////////////////////////////////////////////////
//! ProxyNdpクラスのフィールド定義
///////////////////////////////////////////////////////////////////////////////
struct ProxyNdpField{
        struct me6e_handler_t  *handler;               ///< ME6Eのアプリケーションハンドラー
};
typedef struct ProxyNdpField ProxyNdpField;

///////////////////////////////////////////////////////////////////////////////
//! Multicast Group構造体
///////////////////////////////////////////////////////////////////////////////
struct ProxyNdp_multicast_data {
    int                 fd;                             ///< ソケットディスクリプタ
    char*               if_name;                        ///< インタフェース名
    struct in6_addr     solmulti_preifx;                ///< Solicited-nodeマルチキャストアドレスのプレフィックス
};
typedef struct ProxyNdp_multicast_data ProxyNdp_multicast_data;


///////////////////////////////////////////////////////////////////////////////
//! NS/NAパケット解析データ構造
///////////////////////////////////////////////////////////////////////////////
struct me6eapp_ns_na_analyze_t {
    // ETHER info
    unsigned char   src_hw_addr[ETH_ALEN];              ///< 送信元リンクレイヤーアドレス
    unsigned char   dst_hw_addr[ETH_ALEN];              ///< 送信先リンクレイヤーアドレス
    // IPv6 info
    struct in6_addr src_proto_addr;                     ///< 送信元プロトコルアドレス
    struct in6_addr dst_proto_addr;                     ///< 送信先プロトコルアドレス
    // ICMPv6 info
    unsigned char   icmpv6_type;                        ///< ICMPv6 タイプ
    unsigned char   icmpv6_code;                        ///< ICMPv6 コード
    unsigned short  checksum;                           ///< ICMPチェックサム
    struct in6_addr target_addr;                        ///< ターゲットプロトコルアドレス
    unsigned char   opt_type;                           ///< オプションタイプ
    unsigned char   source_lnk_layer_addr[ETH_ALEN];    ///< 送信元リンクレイヤーアドレス
    unsigned char   target_lnk_layer_addr[ETH_ALEN];    ///< ターゲットリンクレイヤーアドレス
};
typedef struct me6eapp_ns_na_analyze_t me6eapp_ns_na_analyze;

////////////////////////////////////////////////////////////////////////////////
//! Proxy NDPテーブル 設定結果
////////////////////////////////////////////////////////////////////////////////
enum ndp_table_set_result
{
    ME6E_NDP_SET_NEW,              ///< 新規追加
    ME6E_NDP_SET_UPDATE,           ///< 更新
    ME6E_NDP_ENTRY_STATIC,         ///< staticエントリーのため、処理なし
    ME6E_NDP_SET_RESULT_MAX        ///< 異常値
};

///////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
///////////////////////////////////////////////////////////////////////////////
static bool ProxyNdp_Init(IProcessor* self, struct me6e_handler_t *handler);
static void ProxyNdp_Release(IProcessor* proc);
static bool ProxyNdp_RecvFromStub(IProcessor* self, char* recv_buffer, ssize_t recv_len);
static bool ProxyNdp_RecvFromBackbone(IProcessor* self, char* recv_buffer, ssize_t recv_len);
static inline int ProxyNdp_open_socket(int* fd);
static inline bool ProxyNdp_ns_na_parse_packet(const char* packet, me6eapp_ns_na_analyze* data);
static inline void ProxyNdp_ns_na_analyze_dump(me6eapp_ns_na_analyze* data);
static inline int ProxyNdp_Set_Leave_Group(const int fd, const struct in6_addr* addr, const char* name);
static inline int ProxyNdp_Set_Join_Group(const int fd, const struct in6_addr* addr, const char * name);
static inline void ProxyNdp_Leave_Group(const char* key, const void* value, void* userdata);
static inline void ProxyNdp_Join_Group(const char* key, const void* value, void* userdata);
static inline int ProxyNdp_create_soli_multi_addr( struct in6_addr* prefix,
        struct in6_addr* addr, struct in6_addr* soli_multi_addr);
static inline int ProxyNdp_na_send( int fd, struct ether_addr* srcmacaddr,
            struct ether_addr* dstmacaddr, struct in6_addr* srcv6addr,
            struct in6_addr* targetaddr, struct ether_addr* target_mac);
static inline me6e_proxy_ndp_t*  ProxyNdp_init_ndp_table(me6e_config_proxy_ndp_t* conf);
static inline void ProxyNdp_add_static_entry(const char* key, const void* value, void* userdata);
static inline void ProxyNdp_end_ndp_table(me6e_proxy_ndp_t* handler);
static inline void ProxyNdp_print_table_line(const char* key, const void* value, void* userdata);
static inline enum ndp_table_set_result ProxyNdp_ndp_entry_set(me6e_proxy_ndp_t*  handler,
                struct in6_addr* v6addr, struct ether_addr*  macaddr, char *if_name);
static inline int ProxyNdp_ndp_entry_get( me6e_proxy_ndp_t* handler,
                const struct in6_addr*  v6daddr, struct ether_addr* macaddr);
static inline void ProxyNdp_timeout_cb(const timer_t timerid, void* data);



///////////////////////////////////////////////////////////////////////////////
//! ProxyNdpのフィールドアクセス用マクロ
///////////////////////////////////////////////////////////////////////////////
#define PROXYNDP_FIELD(ptr)((ProxyNdpField*)(ptr->data))

///////////////////////////////////////////////////////////////////////////////
//! @brief Proxy NDP インスタンス生成関数
//!
//! ProxyNdpインスタンスを生成する。
//!
//! @return IProcessor構造体へのポインター
///////////////////////////////////////////////////////////////////////////////
IProcessor* ProxyNdp_New()
{
    IProcessor* instance = NULL;

    DEBUG_LOG("ProxyNdp_New start.\n");

    // インスタンスの生成
    instance = malloc(sizeof(IProcessor));
    if (instance == NULL) {
        me6e_logging(LOG_ERR, "fail to malloc for Proxy NDP instance.");
        return NULL;
    }

    memset(instance, 0, sizeof(IProcessor));

    // フィールドの生成
    instance->data = malloc(sizeof(ProxyNdpField));
    if (instance->data== NULL) {
        me6e_logging(LOG_ERR, "fail to malloc for Proxy NDP instance field.");
        return NULL;
    }
    memset(instance->data, 0, sizeof(ProxyNdpField));


    // メソッドの登録
    instance->init               = ProxyNdp_Init;
    instance->release            = ProxyNdp_Release;
    instance->recv_from_stub     = ProxyNdp_RecvFromStub;
    instance->recv_from_backbone = ProxyNdp_RecvFromBackbone;

    DEBUG_LOG("ProxyNdp_New end.\n");
    return instance;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Proxy NDP インスタンス初期化関数
//!
//! ProxyNdpオブジェクトのフィールドを初期化する
//!
//! @param [in] sefl IProcessor構造体
//! @param [in] handler アプリケーションハンドラー
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool ProxyNdp_Init(IProcessor* self, struct me6e_handler_t *handler)
{
    int fd = -1;
    int ret = -1;
    ProxyNdp_multicast_data data;

    DEBUG_LOG("ProxyNdp_Init start.\n");

    // 引数チェック
    if ( (handler == NULL) || (self == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_Init).");
        return false;
    }

    // ハンドラーの設定
    PROXYNDP_FIELD(self)->handler = handler;

    // ProxNDPテーブルの初期化
    handler->proxy_ndp_handler = ProxyNdp_init_ndp_table(handler->conf->ndp);
    if (handler->proxy_ndp_handler == NULL) {
        me6e_logging(LOG_ERR, "fail to create proxy ndp table.");
        return false;
    }

    // StubNW側、MLDv2 Report送信用ソケットの取得
    ret = ProxyNdp_open_socket(&fd);
    if (ret != 0) {
        me6e_logging(LOG_ERR, "fail to open MLDv2 socket : %s\n", strerror(ret));
        return false;
    }
    PROXYNDP_FIELD(self)->handler->proxy_ndp_handler->sock = fd;


    // StubNW側、MLDv2 Reportの送信
    // Solicited-nodeマルチキャストアドレスのプレフィックスの生成
    inet_pton(AF_INET6, SOLICITED_NODE_MC_PREFIX,
            &(PROXYNDP_FIELD(self)->handler->proxy_ndp_handler->solmulti_preifx));

    // マルチキャストグループにJOINし、MLDv2 Report(JOIN)を送信する。
    data.fd = fd;
    data.if_name = handler->conf->capsuling->stub_physical_dev;
    data.solmulti_preifx = (PROXYNDP_FIELD(self)->handler->proxy_ndp_handler->solmulti_preifx);
    me6e_hashtable_foreach(handler->conf->ndp->ndp_static_entry, ProxyNdp_Join_Group, &data);

    DEBUG_LOG("ProxyNdp_Init end.\n");
    return  true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ProxyNDPインスタンス解放関数
//!
//! ProxyNdpオブジェクトが保持するリソースを解放する。
//!
//! @param [in] self IProcessor構造体
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void ProxyNdp_Release(IProcessor* self)
{
    ProxyNdp_multicast_data data;
    struct me6e_handler_t *handler = NULL;

    DEBUG_LOG("ProxyNdp_Release start.\n");

    // 引数チェック
    if (self == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_Release).");
        return;
    }

    handler = PROXYNDP_FIELD(self)->handler;
    if (handler != NULL) {
        // StubNW側、MLDv2 Reportの送信
        // マルチキャストグループにLeaveし、MLDv2 Reportを送信する。
        data.fd = PROXYNDP_FIELD(self)->handler->proxy_ndp_handler->sock;
        data.if_name = handler->conf->capsuling->stub_physical_dev;
        data.solmulti_preifx = (PROXYNDP_FIELD(self)->handler->proxy_ndp_handler->solmulti_preifx);
        me6e_hashtable_foreach(handler->conf->ndp->ndp_static_entry, ProxyNdp_Leave_Group, &data);

        // ソケットのクロース
        close(PROXYNDP_FIELD(self)->handler->proxy_ndp_handler->sock);

        // ProxNDPテーブルの解放
        if (PROXYNDP_FIELD(self)->handler->proxy_ndp_handler != NULL) {
            ProxyNdp_end_ndp_table(PROXYNDP_FIELD(self)->handler->proxy_ndp_handler);
        }
    }

    // フィールドの解放
    free(PROXYNDP_FIELD(self));

    // オブジェクトの解放
    free(self);
    DEBUG_LOG("ProxyNdp_Release end.\n");

    return;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief StubNWパケット受信処理関数
//!
//! StubNWから受信したNSに対してNAを送信する。
//!
//! @param [in] self IProcessor構造体
//! @param [in] recv_buffer 受信データ
//! @param [in] recv_len    受信データのサイズ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool ProxyNdp_RecvFromStub(IProcessor* self, char* recv_buffer, ssize_t recv_len)
{
    // ローカル変数宣言
    bool                isContinue = true;
    struct ethhdr*      p_orig_eth_hdr;

    DEBUG_LOG("ProxyNdp_RecvFromStub\n");

    // 引数チェック
    if ((self == NULL) || (recv_buffer == NULL) || (recv_len <= 0)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_RecvFromStub).");
        return false;
    }

    // 初期化
    p_orig_eth_hdr = (struct ethhdr*)recv_buffer;

    // IPv6のみ処理
    if(p_orig_eth_hdr->h_proto != htons(ETH_P_IPV6)){
        DEBUG_LOG("Not IPV6 packet.\n");
        // 次のクラスの処理を継続
        return true;
    }

    // ICMPv6のみ処理
    struct ip6_hdr* ip6h = (struct ip6_hdr *)(recv_buffer + ETH_HLEN);
    if (ip6h->ip6_nxt != IPPROTO_ICMPV6) {
        DEBUG_LOG("Not ICMPV6 packet.\n");
        // 次のクラスの処理を継続
        return true;
    }

    // NSのみ処理
    struct icmp6_hdr* icmph =
        (struct icmp6_hdr *)(recv_buffer + ETH_HLEN + sizeof( struct ip6_hdr));
    if (icmph->icmp6_type != ND_NEIGHBOR_SOLICIT) {
        DEBUG_LOG("Not NEIGHBOR SOLICIT packet.\n");
        // 次のクラスの処理を継続
        return true;
    }

    DEBUG_LOG("recv icmp6 Neighbor Solicitation packet.\n");

    // パケット解析
    me6eapp_ns_na_analyze ns;
    if(!ProxyNdp_ns_na_parse_packet(recv_buffer, &ns)) {
        me6e_logging(LOG_ERR, "fail to ProxyNdp_ns_na_parse_packet.");
        // パケット破棄
        return false;

    }

    _D_(ProxyNdp_ns_na_analyze_dump(&ns);)


    // ターゲットアドレスがマルチキャストの場合は破棄
    if (IN6_IS_ADDR_MULTICAST(&(ns.target_addr))) {
        me6e_logging(LOG_INFO, "drop target protocol address multicast IPv6 NS packet.");
        // パケット破棄
        return false;
    }

    // テーブルから検索
    struct ether_addr target_mac;
    memset(&target_mac, 0, sizeof(target_mac));
    int ret = ProxyNdp_ndp_entry_get(PROXYNDP_FIELD(self)->handler->proxy_ndp_handler,
            &(ns.target_addr), &target_mac);

    if (ret == 0) {
        DEBUG_LOG("NS REQUEST Match.\n");
        me6e_inc_ns_recv_count(PROXYNDP_FIELD(self)->handler->stat_info);

        int fd = PROXYNDP_FIELD(self)->handler->conf->capsuling->tunnel_device.option.tunnel.fd;

        // NS返信
        // MACフィルタ対応 2016/09/08 chg start
        //if (ProxyNdp_na_send(fd, &target_mac,
        if (ProxyNdp_na_send(fd, PROXYNDP_FIELD(self)->handler->conf->capsuling->bridge_hwaddr,
        // MACフィルタ対応 2016/09/08 chg end
                    (struct ether_addr*)p_orig_eth_hdr->h_source,
                    &(ns.target_addr), &(ns.src_proto_addr), &target_mac) < 0) {
            me6e_inc_na_send_err_count(PROXYNDP_FIELD(self)->handler->stat_info);
        } else {
            me6e_inc_na_send_count(PROXYNDP_FIELD(self)->handler->stat_info);
        }

        // 動的エントリー機能動作有無判定
        if ((PROXYNDP_FIELD(self)->handler->conf->ndp->ndp_entry_update)) {
            // 処理を継続(ProxyNDPの動的NDPエントリ更新が有効であれば、NSは転送する)
            isContinue = true;
        } else {
            // 処理を継続しない。
            isContinue = false;
        }
    } else {
        DEBUG_LOG("NS REQUEST No Match.\n");
        // 次のクラスの処理を継続
        isContinue = true;
    }

    return  isContinue;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief NA 送信処理関数
//!
//! NA を送信する
//!
//! @param [in]     fd          ソケットディスクリプタ
//! @param [in]     srcmacaddr  送信元MACアドレス
//! @param [in]     dstmacaddr  送信先MACアドレス
//! @param [in]     targetaddr  送信元IPv6アドレス
//! @param [in]     srcv6addr   送信先IPv6アドレス
//! @param [in]     target_mac  ターゲットMACアドレス
//!
//! @retval 0       正常終了
//! @retval 0以外   異常終了
///////////////////////////////////////////////////////////////////////////////
static inline int ProxyNdp_na_send(
            int fd,
            struct ether_addr* srcmacaddr,
            struct ether_addr* dstmacaddr,
            struct in6_addr* targetaddr,
            struct in6_addr* dstv6addr,
            struct ether_addr* target_mac)
{
    struct ethhdr               send_ethhdr;
    struct ip6_hdr              ip6_header;
    struct nd_opt_hdr           opthdr;
    struct nd_neighbor_advert   na;

    // 引数チェック
    if ((srcmacaddr == NULL) || (dstmacaddr == NULL) ||
            ((dstv6addr == NULL) || (targetaddr == NULL) || (target_mac == NULL))) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_na_send).");
        return -1;
    }

    // Ethernetフレームのヘッダを設定
    memset(&send_ethhdr, 0, sizeof(struct ethhdr) );
    memcpy(send_ethhdr.h_source, srcmacaddr->ether_addr_octet, ETH_ALEN);
    memcpy(send_ethhdr.h_dest,   dstmacaddr->ether_addr_octet,    ETH_ALEN);
    send_ethhdr.h_proto = htons(ETH_P_IPV6);

    // IPv6ヘッダの設定
    memset(&ip6_header, 0, sizeof(struct ip6_hdr));
    ip6_header.ip6_flow = 0;
    ip6_header.ip6_vfc  = 6 << 4;
    ip6_header.ip6_plen = ntohs(sizeof(na) + sizeof(opthdr) + sizeof(struct ether_addr));
    ip6_header.ip6_nxt  = IPPROTO_ICMPV6;
    ip6_header.ip6_hops = 0xFF;
    memcpy(&(ip6_header.ip6_dst), dstv6addr, sizeof(ip6_header.ip6_dst));
    memcpy(&(ip6_header.ip6_src), targetaddr, sizeof(ip6_header.ip6_src));

    // NAヘッダの設定
    memset(&na, 0, sizeof(struct nd_neighbor_advert));
    na.nd_na_type = ND_NEIGHBOR_ADVERT;
    na.nd_na_code = 0;
    na.nd_na_flags_reserved |= ND_NA_FLAG_SOLICITED | ND_NA_FLAG_ROUTER;

    memcpy(&(na.nd_na_target), targetaddr, sizeof(struct in6_addr) );

    opthdr.nd_opt_type = ND_OPT_TARGET_LINKADDR;
    opthdr.nd_opt_len = 1; // Units of 8-octets

    // Scatter/Gather設定
    struct iovec iov[5];
     // 配列0に、Etherヘッダを格納
    iov[0].iov_base = &send_ethhdr;
    iov[0].iov_len  = sizeof(struct ethhdr);
     // 配列1に、IPv6ヘッダを格納
    iov[1].iov_base = &ip6_header;
    iov[1].iov_len  = sizeof(struct ip6_hdr);
     // 配列2に、ICMPv6 + NAヘッダを格納
    iov[2].iov_base = &na;
    iov[2].iov_len  = sizeof(struct nd_neighbor_advert);
     // 配列3に、NAオプションを格納
    iov[3].iov_base = &opthdr;
    iov[3].iov_len  = sizeof(struct nd_opt_hdr);
     // 配列4に、ターゲットMACアドレスを格納
    iov[4].iov_base = target_mac;
    iov[4].iov_len  = sizeof(struct ether_addr);

    // ICMPV6のチェックサムの計算
    na.nd_na_cksum = me6e_util_pseudo_checksumv(AF_INET6, &iov[1], 4);

    // パケット送信
    if(writev(fd, iov, 5) < 0){
        me6e_logging(LOG_ERR, "fail to send NA packet %s.", strerror(errno));
        return errno;
    }
    DEBUG_LOG("sent NS Reply packet\n");
    return 0;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief BackBoneパケット受信処理関
//!
//! BackboneNWから受信したパケットがNS/NAの場合、NDPテーブルへ登録する。
//!
//! @param [in] self IProcessor構造体
//! @param [in] recv_buffer 受信データ
//! @param [in] recv_len    受信データのサイズ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool ProxyNdp_RecvFromBackbone(IProcessor* self, char* recv_buffer, ssize_t recv_len)
{
    // backboneの処理
    DEBUG_LOG("ProxyNdp_RecvFromBackbone\n");

    // 引数チェック
    if ((self == NULL) || (recv_buffer == NULL) || (recv_len <= 0)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_RecvFromBackbone).");
        return false;
    }

    // 動的エントリー機能動作有無判定
    if (!(PROXYNDP_FIELD(self)->handler->conf->ndp->ndp_entry_update)) {
        // 次のクラスの処理を継続
        return true;
    }

    DEBUG_LOG("ProxyNdp dynamic entry enable route start.\n");

    // 初期化
    struct ethhdr* p_orig_eth_hdr = (struct ethhdr*)recv_buffer;

    // IPv6のみ処理
    if(p_orig_eth_hdr->h_proto != htons(ETH_P_IPV6)){
        DEBUG_LOG("Not IPV6 packet.\n");
        // 次のクラスの処理を継続
        return true;
    }

    // ICMPv6のみ処理
    struct ip6_hdr* ip6h = (struct ip6_hdr *)(recv_buffer + ETH_HLEN);
    if (ip6h->ip6_nxt != IPPROTO_ICMPV6) {
        DEBUG_LOG("Not ICMPV6 packet.\n");
        // 次のクラスの処理を継続
        return true;
    }

    // NS/NAのみ処理
    struct icmp6_hdr* icmph =
        (struct icmp6_hdr *)(recv_buffer + ETH_HLEN + sizeof( struct ip6_hdr));
    if (icmph->icmp6_type == ND_NEIGHBOR_SOLICIT) {
        DEBUG_LOG("NEIGHBOR_SOLICIT Match.\n");

        // パケット解析
        me6eapp_ns_na_analyze ns;
        if(!ProxyNdp_ns_na_parse_packet(recv_buffer, &ns)) {
            me6e_logging(LOG_ERR, "fail to NS analyze.");
            // 次のクラスの処理を継続
            return true;
        }

        _D_(ProxyNdp_ns_na_analyze_dump(&ns);)

        // 送信元ハードウェアアドレスがユニキャストのみ処理
        if ((me6e_util_is_broadcast_mac(ns.source_lnk_layer_addr))
                || (me6e_util_is_multicast_mac(ns.source_lnk_layer_addr)) ){
            me6e_logging(LOG_INFO, "source mac address not nunicast.");
            // 次のクラスの処理を継続
            return true;
        }

        // 送信元プロトコルアドレスがユニキャストのみ処理
        if (IN6_IS_ADDR_MULTICAST(&ns.src_proto_addr)) {
            me6e_logging(LOG_INFO, "target ipv6 address not unicast.");
            // 次のクラスの処理を継続
            return true;
        }

        // Proxy NDP テーブルへの登録
        (void)ProxyNdp_ndp_entry_set(
                PROXYNDP_FIELD(self)->handler->proxy_ndp_handler,
                &(ns.src_proto_addr),
                (struct ether_addr*)(ns.source_lnk_layer_addr),
                PROXYNDP_FIELD(self)->handler->conf->capsuling->stub_physical_dev);

    } else if (icmph->icmp6_type == ND_NEIGHBOR_ADVERT) {
        DEBUG_LOG("NEIGHBOR_ADVERT Match.\n");

        // パケット解析
        me6eapp_ns_na_analyze na;
        if(!ProxyNdp_ns_na_parse_packet(recv_buffer, &na)) {
            me6e_logging(LOG_ERR, "fail to NA analyze.");
            // 次のクラスの処理を継続
            return true;
        }

        _D_(ProxyNdp_ns_na_analyze_dump(&na);)


        // ターゲットハードウェアアドレスがユニキャストのみ処理
        if ((me6e_util_is_broadcast_mac(na.target_lnk_layer_addr))
                || (me6e_util_is_multicast_mac(na.target_lnk_layer_addr)) ){
            me6e_logging(LOG_INFO, "target ipv6 address not unicast.");
            // 次のクラスの処理を継続
            return true;
        }

        // ターゲットプロトコルアドレスがユニキャストのみ処理
        if (IN6_IS_ADDR_MULTICAST(&na.target_addr)) {
            me6e_logging(LOG_INFO, "target ipv6 address not unicast.");
            // 次のクラスの処理を継続
            return true;
        }

        // Proxy NDP テーブルへの登録
        enum ndp_table_set_result ret = ProxyNdp_ndp_entry_set(
                PROXYNDP_FIELD(self)->handler->proxy_ndp_handler,
                &(na.target_addr),
                (struct ether_addr*)(na.target_lnk_layer_addr),
                PROXYNDP_FIELD(self)->handler->conf->capsuling->stub_physical_dev);

        // NAで、静的/動的に関わらず、エントリーがあれば、破棄する。
        if ((ret == ME6E_NDP_SET_UPDATE) || (ret == ME6E_NDP_ENTRY_STATIC)) {
            return false;
        }
    } else {
        DEBUG_LOG("NDP other type = 0x%X.\n", icmph->icmp6_type);
    }

    return  true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MLDv6 Report送信用ソケット取得処理関数
//!
//! Stub側のMLDv6 Report送信用ソケットを取得する。
//!
//! @param [out] fd ソケットディスクリプタ
//!
//! @retval 0       正常終了
//! @retval 0以外   異常終了
///////////////////////////////////////////////////////////////////////////////
static inline int ProxyNdp_open_socket(int* fd)
{
    // 引数チェック
    if (fd == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_open_socket).");
        return -1;
    }

    int sock = socket(PF_INET6, SOCK_RAW,  IPPROTO_ICMPV6);
    if(sock < 0){
        me6e_logging(LOG_ERR, "socket open error : %s.", strerror(errno));
        return errno;
    }

    int hops = 64;
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof(hops))) {
        me6e_logging(LOG_ERR, "fail to set sockopt IPV6_MULTICAST_HOPS : %s.", strerror(errno));
        close(sock);
        return errno;
    }

    int loop = 0;
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof(loop))) {
        me6e_logging(LOG_ERR, "fail to set sockopt IPV6_MULTICAST_LOOP : %s.", strerror(errno));
        close(sock);
        return errno;
    }

    *fd = sock;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief NS/NAパケット解析処理関数
//!
//! NS/NAパケットを解析する。
//!
//! @param [in]     packet  NS/NAパケット
//! @param [out]    data    NS/NA解析データ
//!
//! @retval true    正常終了
//! @retval false   異常終了
///////////////////////////////////////////////////////////////////////////////
static inline bool ProxyNdp_ns_na_parse_packet(
    const char*     packet,
    me6eapp_ns_na_analyze* data
)
{
    // 引数チェック
    if((data == NULL) || (packet == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_ns_na_parse_packet).");
        return false;
    }

    // 初期化
    memset(data, 0, sizeof(me6eapp_ns_na_analyze));

    // ETHER ヘッダ解析
    struct ethhdr* ether_hdr = (struct ethhdr*)(packet);
    memcpy(data->src_hw_addr, ether_hdr->h_source, ETH_ALEN);
    memcpy(data->dst_hw_addr, ether_hdr->h_dest,   ETH_ALEN);

    // IPv6 ヘッダ解析
    struct ip6_hdr* ip6_header  = (struct ip6_hdr*)(packet + sizeof(struct ethhdr));
    data->src_proto_addr = ip6_header->ip6_src;
    data->dst_proto_addr = ip6_header->ip6_dst;

    // ICMPv6 ヘッダ解析
    struct icmp6_hdr* icmp6_header = (struct icmp6_hdr *)(packet + sizeof(struct ethhdr) + sizeof( struct ip6_hdr));
    data->icmpv6_type   = icmp6_header->icmp6_type;
    data->icmpv6_code   = icmp6_header->icmp6_code;
    data->checksum      = ntohs(icmp6_header->icmp6_cksum);

    if (icmp6_header->icmp6_type == ND_NEIGHBOR_SOLICIT) {
        struct nd_neighbor_solicit* ns = (struct nd_neighbor_solicit *)
                (packet +  sizeof(struct ethhdr) + sizeof( struct ip6_hdr));
        data->target_addr = ns->nd_ns_target;

        struct nd_opt_hdr* opthdr = (struct nd_opt_hdr *)
                (packet +  sizeof(struct ethhdr) + sizeof( struct ip6_hdr) + sizeof(struct nd_neighbor_solicit));
        data->opt_type = opthdr->nd_opt_type;

        const char* add = (packet +  sizeof(struct ethhdr) + sizeof( struct ip6_hdr)
                + sizeof(struct nd_neighbor_solicit) + sizeof(struct nd_opt_hdr));
        memcpy(data->source_lnk_layer_addr, add, ETH_ALEN);

    } else if (icmp6_header->icmp6_type == ND_NEIGHBOR_ADVERT) {
        struct nd_neighbor_advert* na = (struct nd_neighbor_advert *)
                (packet +  sizeof(struct ethhdr) + sizeof( struct ip6_hdr));
        data->target_addr = na->nd_na_target;

        struct nd_opt_hdr* opthdr = (struct nd_opt_hdr *)
                (packet +  sizeof(struct ethhdr) + sizeof( struct ip6_hdr) + sizeof(struct nd_neighbor_advert));
        data->opt_type = opthdr->nd_opt_type;
        const char * add = (packet +  sizeof(struct ethhdr) + sizeof( struct ip6_hdr)
                + sizeof(struct nd_neighbor_advert) + sizeof(struct nd_opt_hdr));
        memcpy(data->target_lnk_layer_addr, add, ETH_ALEN);

    } else {
        DEBUG_LOG("not NS or NA.");
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief NS NA解析結果出力関数
//!
//! NS NA解析データを出力する。
//!
//! @param [in] data NS NA解析データ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static inline void ProxyNdp_ns_na_analyze_dump(me6eapp_ns_na_analyze* data)
{
    char macaddrstr[MAC_ADDRSTRLEN] = { 0 };
    char addr[INET6_ADDRSTRLEN] = { 0 };

    // 引数チェック
    if(data == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_ns_na_analyze_dump).");
        return;
    }

    DEBUG_LOG("----Ether Header-----------------\n");
    DEBUG_LOG("Ether SrcAdr      :%s\n", ether_ntoa_r((struct ether_addr*)(data->src_hw_addr), macaddrstr));
    DEBUG_LOG("Ether DstAdr      :%s\n", ether_ntoa_r((struct ether_addr*)(data->dst_hw_addr), macaddrstr));
    DEBUG_LOG("----IPv6 Header------------------\n");
    DEBUG_LOG("Src IP address    : %s\n", inet_ntop(AF_INET6, &(data->src_proto_addr), addr, sizeof(addr)));
    DEBUG_LOG("Dst IP address    : %s\n", inet_ntop(AF_INET6, &(data->dst_proto_addr), addr, sizeof(addr)));
    DEBUG_LOG("----ICMPv6 Header----------------\n");
    DEBUG_LOG("ICMPv6 type       : 0x%04x\n", data->icmpv6_type);
    DEBUG_LOG("ICMPv6 code       : 0x%04x\n", data->icmpv6_code);
    DEBUG_LOG("ICMPv6 checksum   : 0x%04x\n", data->checksum);

    if (data->icmpv6_type == ND_NEIGHBOR_SOLICIT) {
        DEBUG_LOG("----NEIGHBOR SOLICIT-------------\n");
        DEBUG_LOG("Target protocol address : %s\n",
                inet_ntop(AF_INET6, &(data->target_addr), addr, sizeof(addr)));
        DEBUG_LOG("Source link-layer address : %s\n",
                ether_ntoa_r((struct ether_addr*)(data->source_lnk_layer_addr), macaddrstr));
    } else if (data->icmpv6_type == ND_NEIGHBOR_ADVERT) {
        DEBUG_LOG("----NEIGHBOR ADVERT--------------\n");
        DEBUG_LOG("Target protocol address : %s\n",
                inet_ntop(AF_INET6, &(data->target_addr), addr, sizeof(addr)));
        DEBUG_LOG("Target link-layer address : %s\n",
                ether_ntoa_r((struct ether_addr*)(data->target_lnk_layer_addr), macaddrstr));
    } else {
        DEBUG_LOG("----other type-------------------\n");
    }
}


///////////////////////////////////////////////////////////////////////////////
//! @brief マルチキャストJoinGroup関数(ハッシュテーブルコールバック)
//!
//! ハッシュテーブルに登録されているIPv6アドレスを使用し
//! JoinGroup関数を起動する。
//!
//! @param [in]     key       テーブルに登録されているキー
//! @param [in]     value     キーに対応する値(未使用)
//! @param [in]     userdata  コールバック登録時に指定したユーザデータ
//!                           (ソケットディスクリプタ)
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static inline void ProxyNdp_Join_Group(const char* key, const void* value, void* userdata)
{
    ProxyNdp_multicast_data* data = NULL;
    struct in6_addr addr;
    struct in6_addr soli_multi_addr;

    // 引数チェック
    if(key == NULL || userdata == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_Join_Group).");
        return;
    }

    data = (ProxyNdp_multicast_data *)(userdata);
    if (inet_pton(AF_INET6, key, &addr) <= 0) {
        me6e_logging(LOG_ERR, "fail to inet_pton : %s.", strerror(errno));
        return;
    }

    if (ProxyNdp_create_soli_multi_addr(&data->solmulti_preifx, &addr, &soli_multi_addr)) {
        me6e_logging(LOG_ERR, "fail to create solicited node multicast address.");
        return;
    }

    if (ProxyNdp_Set_Join_Group(data->fd, &soli_multi_addr, data->if_name) != 0) {
        me6e_logging(LOG_ERR, "fail to Join Group.");
        return;
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief マルチキャストLeaveGroup関数(ハッシュテーブルコールバック)
//!
//! ハッシュテーブルに登録されているIPv6アドレスを使用し
//! LeaveGroup関数を起動する。
//!
//! @param [in]     key       テーブルに登録されているキー
//! @param [in]     value     キーに対応する値(未使用)
//! @param [in]     userdata  コールバック登録時に指定したユーザデータ
//!                           (ソケットディスクリプタ)
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static inline void ProxyNdp_Leave_Group(const char* key, const void* value, void* userdata)
{
    ProxyNdp_multicast_data* data = NULL;
    struct in6_addr addr;
    struct in6_addr soli_multi_addr;

    // 引数チェック
    if(key == NULL || userdata == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_Leave_Group).");
        return;
    }

    data = (ProxyNdp_multicast_data *)(userdata);
    if (inet_pton(AF_INET6, key, &addr) <= 0) {
        me6e_logging(LOG_ERR, "fail to inet_pton : %s.", strerror(errno));
        return;
    }

    if (ProxyNdp_create_soli_multi_addr(&data->solmulti_preifx, &addr, &soli_multi_addr)) {
        me6e_logging(LOG_ERR, "fail to create solicited node multicast address.");
        return;
    }

    if (ProxyNdp_Set_Leave_Group(data->fd, &soli_multi_addr, data->if_name) != 0) {
        me6e_logging(LOG_ERR, "fail to Leave Group.");
        return;
    }
}

///////////////////////////////////////////////////////////////////////////////
//! @brief マルチキャストグループへのJoin処理関数
//!
//! マルチキャストグループへのJoinする。
//!
//! @param [in] fd      ソケットディスクリプタ
//! @param [in] addr    JoinするIPv6アドレス
//! @param [in] name    インタフェース名
//!
//! @retval 0       正常終了
//! @retval 0以外   異常終了
///////////////////////////////////////////////////////////////////////////////
static inline int ProxyNdp_Set_Join_Group(const int fd, const struct in6_addr* addr, const char * name)
{
    struct ipv6_mreq mreq;

    // 引数チェック
    if ( (fd < 0) || (addr == NULL) || (name == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_Set_Join_Group).");
        return -1;
    }

    memset(&mreq, 0, sizeof(mreq));
    mreq.ipv6mr_multiaddr =  *addr;
    mreq.ipv6mr_interface = if_nametoindex(name);
    if (setsockopt(fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq))) {
        me6e_logging(LOG_ERR, "fail to set sockopt IPV6_JOIN_GROUP : %s.", strerror(errno));
        return errno;
    }

    char addr_str[INET6_ADDRSTRLEN] = { 0 };
    DEBUG_LOG("Join address = %s\n",
            inet_ntop(AF_INET6, addr, addr_str, sizeof(addr_str)));

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief マルチキャストグループへのLeave処理関数
//!
//! マルチキャストグループへのLeaveする。
//!
//! @param [in] fd      ソケットディスクリプタ
//! @param [in] addr    LeaveするIPv6アドレス
//! @param [in] name    インタフェース名
//!
//! @retval 0       正常終了
//! @retval 0以外   異常終了
///////////////////////////////////////////////////////////////////////////////
static inline int ProxyNdp_Set_Leave_Group(const int fd, const struct in6_addr* addr, const char* name)
{
    struct ipv6_mreq mreq;

    // 引数チェック
    if ( (fd < 0) || (addr == NULL) || (name == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_Set_Leave_Group).");
        return -1;
    }

    memset(&mreq, 0, sizeof(mreq));
    mreq.ipv6mr_multiaddr =  *addr;
    mreq.ipv6mr_interface = if_nametoindex(name);
    if (setsockopt(fd, IPPROTO_IPV6, IPV6_LEAVE_GROUP, &mreq, sizeof(mreq))) {
        me6e_logging(LOG_ERR, "fail to set sockopt IPV6_LEAVE_GROUP : %s.", strerror(errno));
        return errno;
    }

    char addr_str[INET6_ADDRSTRLEN] = { 0 };
    DEBUG_LOG("Leave address = %s\n",
            inet_ntop(AF_INET6, addr, addr_str, sizeof(addr_str)));
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief solicited node マルチキャストアドレス生成処理関数
//!
//! ユニキャストアドレスから、solicited node マルチキャストアドレスを生成する。
//!
//! @param [in]  prefix             solicited node マルチキャストアドレスのプレフィックス
//! @param [in]  addr               ユニキャストアドレス
//! @param [out] soli_multi_addr    solicited node マルチキャストアドレス
//!
//! @retval 0       正常終了
//! @retval 0以外   異常終了
///////////////////////////////////////////////////////////////////////////////
static inline int ProxyNdp_create_soli_multi_addr(
        struct in6_addr* prefix,
        struct in6_addr* addr,
        struct in6_addr* soli_multi_addr
        )
{
    // 引数チェック
    if ( (prefix == NULL) || (addr == NULL) || (soli_multi_addr == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_create_soli_multi_addr).");
        return -1;
    }

    soli_multi_addr->s6_addr32[0]  = prefix->s6_addr32[0];
    soli_multi_addr->s6_addr32[1]  = prefix->s6_addr32[1];
    soli_multi_addr->s6_addr32[2]  = prefix->s6_addr32[2];
    soli_multi_addr->s6_addr[12]   = prefix->s6_addr[12];
    soli_multi_addr->s6_addr[13]   = addr->s6_addr[13];
    soli_multi_addr->s6_addr[14]   = addr->s6_addr[14];
    soli_multi_addr->s6_addr[15]   = addr->s6_addr[15];

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Proxy NDP テーブル初期化関数
//!
//! Config情報から、NDP テーブル作成を行う。
//!
//! @param [in]  conf       config情報
//!
//! @return 生成したProxy NDP管理構造体へのポインタ
///////////////////////////////////////////////////////////////////////////////
static inline me6e_proxy_ndp_t*  ProxyNdp_init_ndp_table(me6e_config_proxy_ndp_t* conf)
{
    DEBUG_LOG("Proxy NDP table init\n");
    // 引数チェック
    if (conf == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_init_ndp_table).");
        return NULL;
    }

    me6e_proxy_ndp_t* handler = malloc(sizeof(me6e_proxy_ndp_t));
    if(handler == NULL){
        me6e_logging(LOG_ERR, "fail to malloc for me6e_proxy_ndp_t.");
        return NULL;
    }

    memset(handler, 0, sizeof(handler));

    // hash table作成
    handler->table = me6e_hashtable_create(conf->ndp_entry_max);
    if(handler->table == NULL){
        me6e_logging(LOG_ERR, "fail to hash table for me6e_proxy_ndp_t.");
        free(handler);
        return NULL;
    }

    // 静的エントリーをテーブルに格納
    me6e_hashtable_foreach(conf->ndp_static_entry, ProxyNdp_add_static_entry, handler->table);

    // timer作成
    handler->timer_handler = me6e_init_timer();
    if(handler->timer_handler == NULL){
        me6e_hashtable_delete(handler->table);
        free(handler);
        return NULL;
    }

    // config情報保持
    handler->conf        = conf;

    // 排他制御初期化
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init(&handler->mutex, &attr);

    _D_(ProxyNdp_print_table(handler, STDOUT_FILENO);)

    return handler;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief  Proxy NDP テーブル終了関数
//!
//! NDP テーブルの解放を行う。
//!
//! @param [in]  handler Proxy NDP管理構造体
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static inline void ProxyNdp_end_ndp_table(me6e_proxy_ndp_t* handler)
{
    DEBUG_LOG("Proxy NDP table end\n");

    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_end_ndp_table).");
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
//! @brief 静的NDPエントリー登録関数(ハッシュテーブルコールバック)
//!
//! Config情報の静的NDPエントリーを登録する。
//!
//! @param [in]     key       テーブルに登録されているキー
//! @param [in]     value     キーに対応する値
//! @param [in]     userdata  コールバック登録時に指定したユーザデータ
//!                           (ハッシュテーブルへのポインタ)
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static inline void ProxyNdp_add_static_entry(const char* key, const void* value, void* userdata)
{
    bool                res     = false;
    me6e_hashtable_t*  table   = NULL;
    proxy_ndp_data_t    data;

    // 引数チェック
    if ((key == NULL) || (value == NULL) || (userdata == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_add_static_entry).");
        return;
    }

    table = (me6e_hashtable_t*)userdata;

    data.type = ME6E_PROXY_NDP_TYPE_STATIC;
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
//! Proxy NDP管理内のテーブルを出力する
//!
//! @param [in]  fd         出力先のディスクリプタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void ProxyNdp_print_table(me6e_proxy_ndp_t* handler, int fd)
{

    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_print_table).");
        return;
    }

    DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
    pthread_mutex_lock(&handler->mutex);

    proxy_ndp_print_data data = {
        .timer_handler = handler->timer_handler,
        .fd            = fd
    };

    dprintf(fd, "\n");
    dprintf(fd, "----------------------------------------+--------------------+-------------\n");
    dprintf(fd, "             IPv6 Address               |     MAC Address    | aging time  \n");
    dprintf(fd, "----------------------------------------+--------------------+-------------\n");
    me6e_hashtable_foreach(handler->table, ProxyNdp_print_table_line, &data);
    dprintf(fd, "----------------------------------------+--------------------+-------------\n");
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
//!                           (proxy_ndp_print_data構造体)
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static inline void ProxyNdp_print_table_line(const char* key, const void* value, void* userdata)
{
    char macaddrstr[MAC_ADDRSTRLEN] = { 0 };

    // 引数チェック
    if((key == NULL) || (value == NULL) || (userdata == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_print_table_line).");
        return;
    }

    proxy_ndp_print_data* print_data = (proxy_ndp_print_data*)userdata;
    proxy_ndp_data_t*   data       = (proxy_ndp_data_t*)value;

    // 残り時間出力
    struct itimerspec tmspec;
    if(data->timerid != NULL){
        me6e_timer_get(print_data->timer_handler, data->timerid, &tmspec);

        // 出力
        dprintf(print_data->fd, "%-40s|%-20s|%12ld\n",
                key, ether_ntoa_r(&(data->ethr_addr), macaddrstr), tmspec.it_value.tv_sec);
    }
    else{
        // 出力
        dprintf(print_data->fd, "%-40s|%-20s|%12s\n",
                key, ether_ntoa_r(&(data->ethr_addr), macaddrstr), "static");
    }


    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief NDP テーブル設定関数
//!
//! NDP テーブルに新規のエントリーを追加する。
//!
//! @param [in]     handler    Proxy NDP管理構造体
//! @param [in]     v6addr     ターゲットIPv6アドレス
//! @param [in]     macaddr    ターゲットMACアドレス
//! @param [in]     if_name    Join用デバイス名
//!
//! @return ME6E_NDP_SET_NEW           新規追加
//! @return ME6E_NDP_SET_UPDATE        更新
//! @return ME6E_NDP_ENTRY_STATIC      Staticエントリーのため、処理なし
//! @return ME6E_NDP_SET_RESULT_MAX    異常
///////////////////////////////////////////////////////////////////////////////
static inline enum ndp_table_set_result ProxyNdp_ndp_entry_set(
    me6e_proxy_ndp_t*  handler,
    struct in6_addr*      v6addr,
    struct ether_addr*    macaddr,
    char*                       if_name
)
{
    char dst_addr[INET6_ADDRSTRLEN] = { 0 };
    char macaddrstr[MAC_ADDRSTRLEN] = { 0 };
    int  result;
    int  ret = ME6E_NDP_SET_RESULT_MAX;

    // 引数チェック
    if ((handler == NULL) || (v6addr == NULL) || (macaddr == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_ndp_entry_set).");
        return ret;
    }

    DEBUG_LOG("NDP Entry set v6 addr = %s, MAC addr.\n",
            inet_ntop(AF_INET6, v6addr, dst_addr, sizeof(dst_addr)),
            ether_ntoa_r(macaddr, macaddrstr));

    // 文字列へ変換
    if (inet_ntop(AF_INET6, v6addr, dst_addr, sizeof(dst_addr)) == NULL) {
        me6e_logging(LOG_ERR, "fail to inet_ntop.");
        return ret;
    }

    // 排他開始
    DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
    pthread_mutex_lock(&handler->mutex);

    // テーブルから対象の情報を取得
    proxy_ndp_data_t* data = me6e_hashtable_get(handler->table, dst_addr);

    if(data != NULL){
        // 一致する情報がある場合
        DEBUG_LOG("mach ndp entry.\n");

        if (data->type == ME6E_PROXY_NDP_TYPE_DYNAMIC) {
            // 動的エントリーの場合
            DEBUG_LOG("Success to update dynamic ndp entry.\n");
            // 処理結果を更新へ設定
            ret = ME6E_NDP_SET_UPDATE;
            if(data->timerid != NULL){
                // タイマが起動中の場合は再設定
                DEBUG_LOG("timer exist.\n");
                result = me6e_timer_reset(
                    handler->timer_handler,
                    data->timerid,
                    handler->conf->ndp_aging_time
                );
            }
            else{
                // タイマが起動中で無い場合はタイマ起動
                DEBUG_LOG("timer no exist.\n");
                proxy_ndp_timer_cb_data_t* cb_data = malloc(sizeof(proxy_ndp_timer_cb_data_t));
                if(cb_data != NULL){
                    cb_data->handler = handler;
                    strcpy(cb_data->dst_addr, dst_addr);
                    cb_data->if_name = if_name;

                    result = me6e_timer_register(
                        handler->timer_handler,
                        handler->conf->ndp_aging_time,
                        ProxyNdp_timeout_cb,
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
                me6e_logging(LOG_WARNING, "fail to reset proxy ndp timer.");
                // タイマを停止に設定
                data->timerid = NULL;
            }
        }
        else {
            DEBUG_LOG("static ndp entry ignore...\n");
            // 処理結果を処理なしへ設定
            ret = ME6E_NDP_ENTRY_STATIC;
        }
    }
    else{
        // 一致する情報がない場合
        DEBUG_LOG("New ndp entry.\n");
        // 処理結果を新規追加へ設定
        ret = ME6E_NDP_SET_NEW;

        // 新規追加データ設定
        proxy_ndp_data_t data = {.ethr_addr = *macaddr,
                                 .type      = ME6E_PROXY_NDP_TYPE_DYNAMIC,
                                 .timerid = NULL};

        // タイマアウト時に通知される情報のメモリ確保
        proxy_ndp_timer_cb_data_t* cb_data = malloc(sizeof(proxy_ndp_timer_cb_data_t));
        if(cb_data != NULL){
            cb_data->handler = handler;
            strcpy(cb_data->dst_addr, dst_addr);
            cb_data->if_name = if_name;

            // タイマ登録
            result = me6e_timer_register(
                handler->timer_handler,
                handler->conf->ndp_aging_time,
                ProxyNdp_timeout_cb,
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
                DEBUG_LOG("Success to add new dynamic ndp entry.\n");
                result = 0;
            }
            else{
                me6e_logging(LOG_WARNING, "fail to add new ndp entry.");
                // 既存のタイマ解除
                me6e_timer_cancel(handler->timer_handler, data.timerid, NULL);
                result = -1;
            }
        }

        if(result != 0){
            me6e_logging(LOG_INFO, "proxy_ndp_timer set failed.");
            // コールバックデータ解放
            free(cb_data);
        }
        else {
            // Multicast GroupへのJOIN処理
            struct in6_addr soli_multi_addr;

            // solicited node マルチキャストアドレスの生成
            if (ProxyNdp_create_soli_multi_addr(&handler->solmulti_preifx, v6addr, &soli_multi_addr)) {
                me6e_logging(LOG_ERR, "fail to create solicited node multicast address.");
            }
            else {
                // Multicast GroupへのJOIN
                if (ProxyNdp_Set_Join_Group(handler->sock, &soli_multi_addr, if_name) != 0) {
                    me6e_logging(LOG_ERR, "fail to Join Group.");
                }
            }
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
//! NDP テーブルから、IPv6アドレスをキーに、MACアドレスを取得する
//!
//! @param [in]     handler    Proxy NDP管理構造体
//! @param [in]     v6daddr    ターゲットIPv6アドレス
//! @param [out]    macaddr    ターゲットMACアドレス
//!
//! @return  0      正常
//! @return  0以外  異常
///////////////////////////////////////////////////////////////////////////////
static inline int ProxyNdp_ndp_entry_get(
    me6e_proxy_ndp_t*      handler,
    const struct in6_addr*  v6daddr,
    struct ether_addr*      macaddr
)
{
    char                dst_addr[INET6_ADDRSTRLEN] = { 0 };
    char                macaddrstr[MAC_ADDRSTRLEN] = { 0 };
    proxy_ndp_data_t*   data = NULL;
    int                 result = 0;

    // パラメタチェック
    if((handler == NULL) || (v6daddr == NULL) || (macaddr == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_ndp_entry_get).");
        return -1;
    }

    // テーブル情報ログ出力
    _D_(ProxyNdp_print_table(handler, STDOUT_FILENO);)

    // 排他開始
    DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
    pthread_mutex_lock(&handler->mutex);

    // IPv6アドレス文字列変換
    inet_ntop(AF_INET6, v6daddr, dst_addr, sizeof(dst_addr));

    // v6アドレスをkeyにデータ検索
    data = me6e_hashtable_get(handler->table, dst_addr);
    if(data == NULL){
        DEBUG_LOG("Unmach ndp etnry. target(%s)\n", dst_addr);
        result =  -1;
    } else {
        *macaddr = data->ethr_addr;
        DEBUG_LOG("Mach ndp etnry. %s : %s\n", dst_addr, ether_ntoa_r(macaddr, macaddrstr));
        result = 0;
    }

    // 排他解除
    pthread_mutex_unlock(&handler->mutex);
    DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Proxy NDP 保持タイムアウトコールバック関数
//!
//! NDP テーブルのエントリーを削除する。
//!
//! @param [in]     timerid   タイマ登録時に払い出されたタイマID
//! @param [in]     data      タイマ登録時に設定したデータ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static inline void ProxyNdp_timeout_cb(const timer_t timerid, void* data)
{

    if(data == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_timeout_cb).");
        return;
    }

    proxy_ndp_timer_cb_data_t* cb_data = (proxy_ndp_timer_cb_data_t*)data;

    // 排他開始
    DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
    pthread_mutex_lock(&cb_data->handler->mutex);

    proxy_ndp_data_t* apr_data = me6e_hashtable_get(cb_data->handler->table, cb_data->dst_addr);

    if(apr_data != NULL){
        // エントリーあり
        if (apr_data->type == ME6E_PROXY_NDP_TYPE_DYNAMIC) {
            // 動的エントリーの場合、データを削除
            DEBUG_LOG("ndp entry del. dst(%s)\n", cb_data->dst_addr);
            if(apr_data->timerid != timerid){
                // timeridの値が異なる場合、警告表示(データは一応削除する)
                me6e_logging(LOG_WARNING, "callback timerid different in proxy ndp table.");
                me6e_logging(LOG_WARNING,
                        "callback timerid = %d, proxy ndp table timerid = %d, addr = %s.",
                        timerid, apr_data->timerid, cb_data->dst_addr
                        );
            }
            me6e_hashtable_remove(cb_data->handler->table, cb_data->dst_addr, NULL);

            // Multicast GroupからのLEAVE
            // solicited node マルチキャストアドレスの生成
            struct in6_addr v6addr;
            struct in6_addr soli_multi_addr;
            inet_pton(AF_INET6, cb_data->dst_addr, &v6addr);
            if (ProxyNdp_create_soli_multi_addr(&cb_data->handler->solmulti_preifx, &v6addr, &soli_multi_addr)) {
                me6e_logging(LOG_ERR, "fail to create solicited node multicast address.");
            }
            else {
                // Multicast GroupからのLEAVE
                if (ProxyNdp_Set_Leave_Group(cb_data->handler->sock, &soli_multi_addr, cb_data->if_name) != 0) {
                    me6e_logging(LOG_ERR, "fail to Leave Group.");
                }
            }
        }
        else{
            // 静的エントリーの場合(ありえないルート)
            me6e_logging(LOG_WARNING, "proxy ndp static entry time out. addr = %s.", cb_data->dst_addr);
            apr_data->timerid = NULL;
        }
    }
    else{
        me6e_logging(LOG_WARNING, "proxy ndp entry is not found. addr = %s.", cb_data->dst_addr);
    }

    // 排他解除
    pthread_mutex_unlock(&cb_data->handler->mutex);
    DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    free(cb_data);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 静的NDPエントリー登録関数(コマンド用)
//!
//! 静的なNDPエントリーを登録する。
//! 既にエントリー(静的/動的に関わらず)がある場合は、登録しない。
//!
//! @param [in]     handler         Proxy NDP管理構造体
//! @param [in]     v6daddr_str     ターゲットIPv6アドレス
//! @param [out]    macadd_strr     ターゲットMACアドレス
//! @param [out]    if_name         JOINで使用するインターフェイス名
//! @param [in]     fd              出力先のディスクリプタ
//!
//! @return  0          正常
//! @return  1          既にエントリーあり
//! @return  上記以外   異常
///////////////////////////////////////////////////////////////////////////////
int ProxyNdp_cmd_add_static_entry(
        me6e_proxy_ndp_t* handler,
        const char* v6addr_str,
        const char* macaddr_str,
        const char* if_name,
        int fd)
{
    proxy_ndp_data_t    data;
    struct in6_addr     v6addr;
    char                addr[INET6_ADDRSTRLEN];
    bool                res = false;
    struct in6_addr soli_multi_addr;

    // 引数チェック
    if ((handler == NULL) || (v6addr_str == NULL) || (macaddr_str == NULL) || (if_name == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_cmd_add_static_entry).");
        return -1;
    }

    if (!parse_ipv6address(v6addr_str, &v6addr, NULL)){
        dprintf(fd, "fail to parse ipv6 address.\n");
        return -1;
    }

    if (!parse_macaddress(macaddr_str, &(data.ethr_addr))) {
        dprintf(fd, "fail to parse mac address.\n");
        return -1;
    }

    // 排他開始
    DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
    pthread_mutex_lock(&handler->mutex);

    // 既にエントリーがあるかチェックする
    proxy_ndp_data_t* ndp_data = me6e_hashtable_get(handler->table,
                                    inet_ntop(AF_INET6, &v6addr, addr, sizeof(addr)));

    if (ndp_data != NULL) {
        dprintf(fd, "proxy ndp entry is already exists. addr = %s\n", addr);
        // 排他解除
        pthread_mutex_unlock(&handler->mutex);
        DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());
        return -1;
    }

    // 登録処理
    data.type = ME6E_PROXY_NDP_TYPE_STATIC;
    data.timerid = NULL;
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

    // solicited node マルチキャストアドレスの生成
    if (ProxyNdp_create_soli_multi_addr(&handler->solmulti_preifx, &v6addr, &soli_multi_addr)) {
        me6e_logging(LOG_ERR, "fail to create solicited node multicast address.");
        return -1;
    }

    // Multicast GroupへのJOIN
    if (ProxyNdp_Set_Join_Group(handler->sock, &soli_multi_addr, if_name) != 0) {
        me6e_logging(LOG_ERR, "fail to Join Group.");
        return -1;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 静的NDPエントリー削除関数(コマンド用)
//!
//! 静的なNDPエントリーを削除する。
//!
//! @param [in]     handler         Proxy NDP管理構造体
//! @param [in]     v6daddr_str     ターゲットIPv6アドレス
//! @param [out]    if_name         LEAVEで使用するインターフェイス名
//! @param [in]     fd              出力先のディスクリプタ
//!
//! @return  0          正常
//! @return  1          エントリーなし
//! @return  上記以外   異常
///////////////////////////////////////////////////////////////////////////////
int ProxyNdp_cmd_del_static_entry(
        me6e_proxy_ndp_t* handler,
        const char* v6addr_str,
        const char* if_name,
        int fd)
{
    struct in6_addr     v6addr;
    char                addr[INET6_ADDRSTRLEN];
    struct in6_addr soli_multi_addr;
    bool                res = false;

    // 引数チェック
    if ((v6addr_str == NULL) || (handler == NULL) || (if_name == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(ProxyNdp_cmd_del_static_entry).");
        return -1;
    }

    if(!parse_ipv6address(v6addr_str, &v6addr, NULL)){
        dprintf(fd, "fail to parse ipv6 address.\n");
        return -1;
    }

    // 排他開始
    DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
    pthread_mutex_lock(&handler->mutex);

    // 既にエントリーがあるかチェックする
    proxy_ndp_data_t* ndp_data = me6e_hashtable_get(handler->table,
                                    inet_ntop(AF_INET6, &v6addr, addr, sizeof(addr)));

    if (ndp_data == NULL) {
        dprintf(fd, "proxy ndp entry is not exists. addr = %s\n", addr);
        // 排他解除
        pthread_mutex_unlock(&handler->mutex);
        DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());
        return 1;
    }

    // 削除処理
    res = me6e_hashtable_remove(handler->table, addr, NULL);
    if(!res){
        me6e_logging(LOG_ERR, "fail to delete hashtable entry.");
        // 排他解除
        pthread_mutex_unlock(&handler->mutex);
        DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());
        return 1;
    }

    // 排他解除
    pthread_mutex_unlock(&handler->mutex);
    DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    // solicited node マルチキャストアドレスの生成
    if (ProxyNdp_create_soli_multi_addr(&handler->solmulti_preifx, &v6addr, &soli_multi_addr)) {
        me6e_logging(LOG_ERR, "fail to create solicited node multicast address.");
        return -1;
    }

    // Multicast GroupからのLEAVE
    if (ProxyNdp_Set_Leave_Group(handler->sock, &soli_multi_addr, if_name) != 0) {
        me6e_logging(LOG_ERR, "fail to Leave Group.");
        return -1;
    }

    return 0;
}

