/******************************************************************************/
/* ファイル名 : me6eapp_command_data.h                                        */
/* 機能概要   : コマンドデータ共通定義 ヘッダファイル                         */
/* 修正履歴   : 2013.01.24 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*            : 2016.06.01 H.Koganemaru PR機能の追加                          */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __ME6EAPP_COMMAND_DATA_H__
#define __ME6EAPP_COMMAND_DATA_H__

#include <netinet/in.h>

#include "me6eapp.h"

//! UNIXドメインソケット名
#define ME6E_COMMAND_SOCK_NAME  "/me6e/%s/command"

////////////////////////////////////////////////////////////////////////////////
//! コマンドコード
////////////////////////////////////////////////////////////////////////////////
enum me6e_command_code
{
    ME6E_COMMAND_NONE,
    ME6E_SHOW_CONF,            ///< 設定情報表示
    ME6E_SHOW_STATISTIC,       ///< 統計情報表示
    ME6E_SHOW_ARP,             ///< Proxy ARPテーブル表示
    ME6E_ADD_ARP,              ///< Proxy ARPテーブルへの追加
    ME6E_DEL_ARP,              ///< Proxy ARPテーブルからの削除
    ME6E_SHOW_NDP,             ///< Proxy NDPテーブル表示
    ME6E_ADD_NDP,              ///< Proxy NDPテーブルへの追加
    ME6E_DEL_NDP,              ///< Proxy NDPテーブルからの削除
    ME6E_ADD_PR,               ///< PRテーブルへのエントリ追加
    ME6E_DEL_PR,               ///< PRテーブルからのエントリ削除
    ME6E_DELALL_PR,            ///< PRテーブルからのエントリ全削除
    ME6E_ENABLE_PR,            ///< PRテーブルエントリの活性化
    ME6E_DISABLE_PR,           ///< PRテーブルエントリの非活性化
    ME6E_SHOW_PR,              ///< PRテーブルエントリの表示
    ME6E_LOAD_PR,              ///< PR-Commandファイル読み込み
    ME6E_SHUTDOWN,             ///< シャットダウン指示
    ME6E_COMMAND_MAX
};

////////////////////////////////////////////////////////////////////////////////
//! Proxy ARP コマンドコード
////////////////////////////////////////////////////////////////////////////////
enum me6e_command_arp_code
{
    ME6E_COMMAND_ARP_NONE,
    ME6E_COMMAND_ARP_ADD,      ///< ARP エントリーの追加
    ME6E_COMMAND_ARP_DEL,      ///< ARP エントリーの削除
    ME6E_COMMAND_ARP_MAX
};

////////////////////////////////////////////////////////////////////////////////
//! Proxy ARPテーブル エントリー追加/削除要求データ
////////////////////////////////////////////////////////////////////////////////
struct me6e_command_arp_data
{
    enum me6e_command_arp_code code;
    char ipv4addr[INET_ADDRSTRLEN];     ///< IPv4アドレス
    char macaddr[MAC_ADDRSTRLEN];       ///< MACアドレス
};

////////////////////////////////////////////////////////////////////////////////
//! Proxy NDP コマンドコード
////////////////////////////////////////////////////////////////////////////////
enum me6e_command_ndp_code
{
    ME6E_COMMAND_NDP_NONE,
    ME6E_COMMAND_NDP_ADD,      ///< NDP エントリーの追加
    ME6E_COMMAND_NDP_DEL,      ///< NDP エントリーの削除
    ME6E_COMMAND_NDP_MAX
};

////////////////////////////////////////////////////////////////////////////////
//! Proxy NDPテーブル エントリー追加/削除要求データ
////////////////////////////////////////////////////////////////////////////////
struct me6e_command_ndp_data
{
    enum me6e_command_ndp_code code;
    char ipv6addr[INET6_ADDRSTRLEN];    ///< IPv4アドレス
    char macaddr[MAC_ADDRSTRLEN];       ///< MACアドレス

};

////////////////////////////////////////////////////////////////////////////////
//! PR コマンドコード
////////////////////////////////////////////////////////////////////////////////
enum me6e_command_pr_code
{
    ME6E_COMMAND_PR_NONE,
    ME6E_COMMAND_PR_ADD,      ///< PR エントリーの追加
    ME6E_COMMAND_PR_DEL,      ///< PR エントリーの削除
    ME6E_COMMAND_PR_MAX
};

////////////////////////////////////////////////////////////////////////////////
//! PRテーブル エントリー追加/削除要求データ
////////////////////////////////////////////////////////////////////////////////
struct me6e_command_pr_data
{
    enum me6e_command_pr_code code;
    bool                    enable;                 ///< エントリーか有効/無効かを表すフラグ
    struct ether_addr       macaddr;                ///< MACアドレス 
    struct in6_addr         pr_prefix;              ///< ME6E-PR address prefix用のIPv6アドレス(表示用)
    int                     v6cidr;                 ///< ME6E-PR address prefixのサブネットマスク長(表示用)
    int                     fd;                     ///< 書き込み先のファイルディスクリプタ
};

////////////////////////////////////////////////////////////////////////////////
//! 要求データ構造体
////////////////////////////////////////////////////////////////////////////////
struct me6e_command_request_data
{
    union {
        struct me6e_command_arp_data   arp;    ///< Proxy ARP データ
        struct me6e_command_ndp_data   ndp;    ///< Proxy NDP データ
        struct me6e_command_pr_data    pr;     ///< PR データ
    };
};

////////////////////////////////////////////////////////////////////////////////
//! 応答データ構造体
////////////////////////////////////////////////////////////////////////////////
struct me6e_command_response_data
{
    int result;                                   ///< 処理結果
};

////////////////////////////////////////////////////////////////////////////////
//! コマンドデータ構造体
////////////////////////////////////////////////////////////////////////////////
struct me6e_command_t
{
    enum   me6e_command_code          code;  ///< コマンドコード
    struct me6e_command_request_data  req;   ///< 要求データ
    struct me6e_command_response_data res;   ///< 応答データ
};
typedef struct me6e_command_t me6e_command_t;

#endif // __ME6EAPP_COMMAND_DATA_H__

