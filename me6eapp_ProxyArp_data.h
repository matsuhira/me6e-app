/******************************************************************************/
/* ファイル名 : me6eapp_ProxyArp_data.h                                       */
/* 機能概要   : ProxyARP データ定義ヘッダファイル                             */
/* 修正履歴   : 2013.01.15 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef ___ME6EAPP_PROXYARP_DATA_H__
#define ___ME6EAPP_PROXYARP_DATA_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <net/ethernet.h>

#include "me6eapp_timer.h"


///////////////////////////////////////////////////////////////////////////////
//! Proxy ARP情報保持タイプ
///////////////////////////////////////////////////////////////////////////////
enum me6e_proxy_arp_type
{
    ME6E_PROXY_ARP_TYPE_STATIC,    ///< 静的に登録したエントリー
    ME6E_PROXY_ARP_TYPE_DYNAMIC    ///< 動的に登録されたエントリー
};
typedef enum me6e_proxy_arp_type me6e_proxy_arp_type;

////////////////////////////////////////////////////////////////////////////////
// Proxy ARP管理用 内部構造体
////////////////////////////////////////////////////////////////////////////////
//! Proxy ARPテーブルに登録するデータ構造体
struct proxy_arp_data_t
{
    struct ether_addr       ethr_addr;              ///< MACアドレス
    me6e_proxy_arp_type    type;                   ///< エントリーモード(静的/動的)
    timer_t                 timerid;                ///< 対応するタイマID
};
typedef struct proxy_arp_data_t  proxy_arp_data_t;


//! Proxy ARP管理構造体
struct me6e_proxy_arp_t
{
    me6e_config_proxy_arp_t*   conf;             ///< Proxy ARP関連設定
    pthread_mutex_t             mutex;            ///< Proxy ARP用mutex
    me6e_hashtable_t*          table;            ///< Proxy ARPデータ保存テーブル
    me6e_timer_t*              timer_handler;    ///< Proxy ARP管理用タイマハンドラ
};
typedef struct me6e_proxy_arp_t me6e_proxy_arp_t;


//! Proxy ARPタイマT.Oコールバックデータ
struct proxy_arp_timer_cb_data_t
{
    me6e_proxy_arp_t*  handler;                    ///< Proxy ARP管理
    char                dst_addr[INET_ADDRSTRLEN]; ///< T.Oしたデータの送信先アドレス
};
typedef struct proxy_arp_timer_cb_data_t proxy_arp_timer_cb_data_t;

//! Proxy ARPテーブル表示用データ
struct proxy_arp_print_data
{
    me6e_timer_t* timer_handler;  ///< タイマハンドラ
    int            fd;             ///< 表示データ書き込み先ディスクリプタ
};
typedef struct proxy_arp_print_data proxy_arp_print_data;

#endif // ___ME6EAPP_PROXYARP_DATA_H__

