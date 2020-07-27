/******************************************************************************/
/* ファイル名 : me6eapp_ProxyNdp_data.h                                       */
/* 機能概要   : ProxyNDP データ定義ヘッダファイル                             */
/* 修正履歴   : 2013.01.15 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef ___ME6EAPP_PROXYNDP_DATA_H__
#define ___ME6EAPP_PROXYNDP_DATA_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <net/ethernet.h>

#include "me6eapp_timer.h"


///////////////////////////////////////////////////////////////////////////////
//! Proxy NDP情報保持タイプ
///////////////////////////////////////////////////////////////////////////////
enum me6e_proxy_ndp_type
{
    ME6E_PROXY_NDP_TYPE_STATIC,    ///< 静的に登録したエントリー
    ME6E_PROXY_NDP_TYPE_DYNAMIC    ///< 動的に登録されたエントリー
};
typedef enum me6e_proxy_ndp_type me6e_proxy_ndp_type;

////////////////////////////////////////////////////////////////////////////////
// Proxy NDP管理用 内部構造体
////////////////////////////////////////////////////////////////////////////////
//! Proxy NDPテーブルに登録するデータ構造体
struct proxy_ndp_data_t
{
    struct ether_addr       ethr_addr;              ///< MACアドレス
    me6e_proxy_ndp_type    type;                   ///< エントリーモード(静的/動的)
    timer_t                 timerid;                ///< 対応するタイマID
};
typedef struct proxy_ndp_data_t  proxy_ndp_data_t;


//! Proxy NDP管理構造体
struct me6e_proxy_ndp_t
{
    me6e_config_proxy_ndp_t*   conf;             ///< Proxy NDP関連設定
    pthread_mutex_t             mutex;            ///< Proxy NDP用mutex
    me6e_hashtable_t*          table;            ///< Proxy NDPデータ保存テーブル
    me6e_timer_t*              timer_handler;    ///< Proxy NDP管理用タイマハンドラ
    int                         sock;             ///< MLDv2 Report送信用ソケット
    struct in6_addr             solmulti_preifx;  ///< Solicited-nodeマルチキャストアドレスのプレフィックス
};
typedef struct me6e_proxy_ndp_t me6e_proxy_ndp_t;


//! Proxy NDPタイマT.Oコールバックデータ
struct proxy_ndp_timer_cb_data_t
{
    me6e_proxy_ndp_t*  handler;                    ///< Proxy NDP管理
    char                dst_addr[INET6_ADDRSTRLEN]; ///< T.Oしたデータの送信先アドレス
    char*               if_name;                    ///< Stubネットワーク網に接続される物理デバイス名

};
typedef struct proxy_ndp_timer_cb_data_t proxy_ndp_timer_cb_data_t;

//! Proxy NDPテーブル表示用データ
struct proxy_ndp_print_data
{
    me6e_timer_t* timer_handler;  ///< タイマハンドラ
    int            fd;             ///< 表示データ書き込み先ディスクリプタ
};
typedef struct proxy_ndp_print_data proxy_ndp_print_data;

#endif // ___ME6EAPP_PROXYNDP_DATA_H__

