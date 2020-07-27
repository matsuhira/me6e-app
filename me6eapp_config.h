/******************************************************************************/
/* ファイル名 : me6eapp_config.h                                              */
/* 機能概要   : 設定情報管理 ヘッダファイル                                   */
/* 修正履歴   : 2013.01.08 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*            : 2016.06.01 S.Anai PR機能の追加                                */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __ME6EAPP_CONFIG_H__
#define __ME6EAPP_CONFIG_H__

#include <stdbool.h>
#include <netinet/in.h>

#include "me6eapp_list.h"
#include "me6eapp_hashtable.h"

///////////////////////////////////////////////////////////////////////////////
//! デバイス種別
///////////////////////////////////////////////////////////////////////////////
enum me6e_device_type
{
    ME6E_DEVICE_TYPE_TUNNEL_IPV4,      ///< トンネルデバイス(IPv4側)
    ME6E_DEVICE_TYPE_TUNNEL_IPV6,      ///< トンネルデバイス(IPv6側)
    ME6E_DEVICE_TYPE_VETH,             ///< veth方式
    ME6E_DEVICE_TYPE_MACVLAN,          ///< macvlan方式
    ME6E_DEVICE_TYPE_PHYSICAL,         ///< 物理デバイス方式
    ME6E_DEVICE_TYPE_NONE       = -1,  ///< 種別なし
};
typedef enum me6e_device_type me6e_device_type;

///////////////////////////////////////////////////////////////////////////////
//! デバイス情報
///////////////////////////////////////////////////////////////////////////////
struct me6e_device_t
{
    me6e_device_type   type;             ///< デバイスのネットワーク空間への移動方式
    char*               name;            ///< デバイス名
    char*               physical_name;   ///< 対応する物理デバイスのデバイス名
    struct in_addr*     ipv4_address;    ///< デバイスに設定するIPv4アドレス
    int                 ipv4_netmask;    ///< デバイスに設定するIPv4サブネットマスク
    struct in_addr*     ipv4_gateway;    ///< デフォルトゲートウェイアドレス
    struct in6_addr*    ipv6_address;    ///< デバイスに設定するIPv6アドレス
    int                 ipv6_prefixlen;  ///< デバイスに設定するIPv6プレフィックス長
    int                 mtu;             ///< デバイスに設定するMTU長
    struct ether_addr*  hwaddr;          ///< デバイスに設定するMACアドレス
    int                 ifindex;         ///< デバイスのインデックス番号
    union {
        struct {
            char* bridge;                ///< 接続するブリッジデバイスのデバイス名
            char* pair_name;             ///< vethの対向デバイス(Backboneに残すデバイス)のデバイス名
        } veth;                          ///< veth固有の設定
        struct {
            int mode;                    ///< MACVLANのモード
        } macvlan;                       ///< MACVLAN固有の設定
        struct {
            int mode;                    ///< トンネル方式(TUN/TAP)
            int fd;                      ///< トンネルデバイスファイルディスクリプタ
        } tunnel;                        ///< トンネルデバイス固有の設定
    } option;                            ///< オプション設定
};
typedef struct me6e_device_t me6e_device_t;

///////////////////////////////////////////////////////////////////////////////
//! 動作モード種別
///////////////////////////////////////////////////////////////////////////////
enum me6e_tunnel_mode
{
    ME6E_TUNNEL_MODE_FP     = 0,   ///< ME6E-FPモード
    ME6E_TUNNEL_MODE_PR     = 1,   ///< ME6E-PRモード
    ME6E_TUNNEL_MODE_NONE   = -1,  ///< モードなし
};
typedef enum me6e_tunnel_mode me6e_tunnel_mode;

///////////////////////////////////////////////////////////////////////////////
//! 共通設定
///////////////////////////////////////////////////////////////////////////////
struct me6e_config_common_t
{
    char*                plane_name;          ///< アプリケーション名
    bool                 debug_log;           ///< デバッグログを出力するかどうか
    bool                 daemon;              ///< デーモン化するかどうか
    char*                startup_script;      ///< スタートアップスクリプトのパス
    me6e_tunnel_mode     tunnel_mode;         ///< ME6Eの動作モード
};
typedef struct me6e_config_common_t me6e_config_common_t;

///////////////////////////////////////////////////////////////////////////////
//! カプセリング固有の設定
///////////////////////////////////////////////////////////////////////////////
struct me6e_config_capsuling_t
{
    struct in6_addr*     me6e_address_prefix;     ///< ME6E ユニキャストアドレスプレフィックス
    int                  unicast_prefixlen;       ///< ME6E unicast prefix長
    struct in6_addr*     me6e_multicast_prefix;   ///< ME6E multicast prefix(96bit固定)
    char*                plane_id;                ///< network plane ID
    int                  hop_limit;               ///< multicast hop limit
    char*                backbone_physical_dev;   ///< Backboneネットワーク網に接続される物理デバイス名
    int                  bb_fd;                   ///< Backboneネットワーク網の送受信用IPv6ソケット
    char*                stub_physical_dev;       ///< Stubネットワーク網に接続される物理デバイス名
    me6e_device_t        tunnel_device;           ///< トンネルデバイス情報
    char*                bridge_name;             ///< Bridgeデバイス名
    struct ether_addr*   bridge_hwaddr;           ///< BridgeデバイスのMAC  // MACフィルタ対応 2016/09/09 add
    bool                 l2multi_l3uni;           ///< L2マルチ-L3ユニキャスト機能の動作有無
    me6e_list            me6e_host_address_list;  ///< ME6Eサーバアドレスのリスト
    struct in6_addr*     me6e_pr_unicast_prefix;  ///< ME6E-PR ユニキャストアドレスプレフィックス
    int                  pr_unicat_prefixlen;     ///< ME6E-PR unicast prefix長
    struct in6_addr*     pr_unicast_prefixplaneid;///< ME6E-PR unicast prefix + plane ID
};
typedef struct me6e_config_capsuling_t me6e_config_capsuling_t;

///////////////////////////////////////////////////////////////////////////////
//! 代理ARP固有の設定
///////////////////////////////////////////////////////////////////////////////
struct me6e_config_proxy_arp_t
{
    bool                   arp_enable;              ///< 代理ARP機能の動作有無
    bool                   arp_entry_update;        ///< 動的エントリの追加/更新の動作有無
    int                    arp_aging_time;          ///< 動的エントリのエージングタイマ
    int                    arp_entry_max;           ///< 登録できるエントリの最大数
    me6e_hashtable_t*      arp_static_entry;        ///< 静的ARPエントリ
};
typedef struct me6e_config_proxy_arp_t me6e_config_proxy_arp_t;

///////////////////////////////////////////////////////////////////////////////
//! 代理NDP固有の設定
///////////////////////////////////////////////////////////////////////////////
struct me6e_config_proxy_ndp_t
{
    bool                   ndp_enable;              ///< 代理NDP機能の動作有無
    bool                   ndp_entry_update;        ///< 動的エントリの追加/更新の動作有無
    int                    ndp_aging_time;          ///< 動的エントリのエージングタイマ
    int                    ndp_entry_max;           ///< 登録できるエントリの最大数
    me6e_hashtable_t*      ndp_static_entry;        ///< 静的NDPエントリ
};
typedef struct me6e_config_proxy_ndp_t me6e_config_proxy_ndp_t;


///////////////////////////////////////////////////////////////////////////////
//! MAC管理固有の設定
///////////////////////////////////////////////////////////////////////////////
struct me6e_config_mng_macaddr_t
{
    bool                 mng_macaddr_enable;      ///< MACアドレス管理機能の動作有無
    bool                 mac_entry_update;        ///< 動的エントリの追加/更新の動作有無
    int                  mac_vaild_lifetime;      ///< 動的エントリのvalid life time
    int                  mac_entry_max;           ///< 登録できるエントリの最大数
    me6e_list            hosthw_addr_list;        ///< StubNWに収容するホストのMACアドレス
};
typedef struct me6e_config_mng_macaddr_t me6e_config_mng_macaddr_t;

///////////////////////////////////////////////////////////////////////////////
//! ME6E-PR Config 情報
///////////////////////////////////////////////////////////////////////////////
struct me6e_pr_config_entry_t
{
    bool                    enable;             ///< エントリーか有効(true)/無効(false)かを表すフラグ
    struct ether_addr*      macaddr;            ///< 送信先のmacアドレス
    struct in6_addr*        pr_prefix;          ///< ME6E-PR address prefixのIPv6アドレス
    int                     v6cidr;             ///< ME6E-PR address prefix長（CIDR形式）
};
typedef struct  me6e_pr_config_entry_t me6e_pr_config_entry_t;

///////////////////////////////////////////////////////////////////////////////
//! ME6E-PR Config Table
///////////////////////////////////////////////////////////////////////////////
struct me6e_pr_config_table_t
{
    int                     num;            ///< ME6E-PR Config Entry 数
    me6e_list               entry_list;     ///< ME6E-PR Config Entry list
};
typedef struct me6e_pr_config_table_t me6e_pr_config_table_t;

///////////////////////////////////////////////////////////////////////////////
//! ME6Eアプリケーション 設定
///////////////////////////////////////////////////////////////////////////////
struct me6e_config_t
{
    char*                       filename;      ///< 設定ファイルのパス
    me6e_config_common_t*       common;        ///< 共通設定
    me6e_config_capsuling_t*    capsuling;     ///< カプセリング固有の設定
    me6e_config_proxy_arp_t*    arp;           ///< 代理ARP固有の設定
    me6e_config_proxy_ndp_t*    ndp;           ///< 代理NDP固有の設定
    me6e_config_mng_macaddr_t*  mac;           ///< MAC管理固有の設定
    me6e_pr_config_table_t*     pr_conf_table; ///< ME6E-PR Config Table
};
typedef struct me6e_config_t me6e_config_t;

///////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ
///////////////////////////////////////////////////////////////////////////////
me6e_config_t* me6e_config_load(const char* filename);
void me6e_config_destruct(me6e_config_t* config);
void me6e_config_dump(const me6e_config_t* config, int fd);
bool parse_ipv4address(const char* str, struct in_addr* output, int* prefixlen);
bool parse_ipv6address(const char* str, struct in6_addr* output, int* prefixlen);
bool parse_macaddress(const char* str, struct ether_addr* output);

#endif // __ME6EAPP_CONFIG_H__

