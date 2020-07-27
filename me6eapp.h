/******************************************************************************/
/* ファイル名 : me6eapp.h                                                     */
/* 機能概要   : ME6E共通ヘッダファイル                                        */
/* 修正履歴   : 2013.01.08 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*            : 2016.06.01 S.ANAI PR機能の追加                                */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __ME6EAPP_H__
#define __ME6EAPP_H__

#include <unistd.h>
#include <netinet/in.h>
#include <net/ethernet.h>

#include "me6eapp_timer.h"
#include "me6eapp_config.h"
#include "me6eapp_statistics.h"
#include "me6eapp_ProxyArp_data.h"
#include "me6eapp_ProxyNdp_data.h"
#include "me6eapp_pr_struct.h"

////////////////////////////////////////////////////////////////////////////////
// マクロ定義
////////////////////////////////////////////////////////////////////////////////
//!IPv6のMTU最小値
#define IPV6_MIN_MTU    1280
//!IPv6のプレフィックス最大値
#define IPV6_PREFIX_MAX 128
//!IPv4のプレフィックス最大値
#define IPV4_PREFIX_MAX 32
//! EtherIPのプロトコル番号
#define ME6E_IPPROTO_ETHERIP   97
//! MACアドレスの文字列長
#define MAC_ADDRSTRLEN  18
//! 受信イベント(ディスクリプタ)の最大登録数
#define RECV_NEVENT_NUM 4


////////////////////////////////////////////////////////////////////////////////
// 外部構造体定義
////////////////////////////////////////////////////////////////////////////////
//! ME6Eアプリケーションハンドラ
struct me6e_handler_t
{
    me6e_config_t*      conf;                      ///< 設定情報
    me6e_statistics_t*  stat_info;                 ///< 統計情報
    me6e_proxy_arp_t    *proxy_arp_handler;        ///< Proxy ARP テーブル管理ハンドラー
    me6e_proxy_ndp_t    *proxy_ndp_handler;        ///< Proxy NDP テーブル管理ハンドラー
    me6e_hashtable_t    *mac_manager_static_entry; ///< MAC管理静的エントリ
    me6e_pr_table_t*    pr_handler;                ///< ME6E-PR情報管理
    me6e_list           instance_list;             ///< 各機能のインスタンスを登録するリスト
    struct in6_addr     unicast_prefix;            ///< ME6E ユニキャストプレフィックス
    struct in6_addr     multicast_prefix;          ///< ME6E マルチキャストプレフィックス
    // L2MC-L3UC機能 start
    struct in6_addr     me6e_own_v6addr;           ///< BackboneNWのME6E 自サーバアドレス
    // L2MC-L3UC機能 end
    int                 signalfd;                  ///< シグナル受信用ディスクリプタ
    sigset_t            oldsigmask;                ///< プロセス起動時のシグナルマスク
};

#endif // __ME6EAPP_H__

