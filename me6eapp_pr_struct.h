/******************************************************************************/
/* ファイル名 : me6eapp_pr_struct.h                                           */
/* 機能概要   : ME6E Prefix Resolution 構造体定義ヘッダファイル               */
/* 修正履歴   : 2016.06.01 S.Anai 新規作成                                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __ME6EAPP_PR_STRUCT_H__
#define __ME6EAPP_PR_STRUCT_H__

#include <stdbool.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "me6eapp_list.h"

////////////////////////////////////////////////////////////////////////////////
// 構造体
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//! ME6E-PR Entry 構造体
///////////////////////////////////////////////////////////////////////////////
typedef struct _me6e_pr_entry_t
{
    bool                    enable;             ///< エントリーか有効(true)/無効(false)かを表すフラグ
    struct ether_addr       macaddr;            ///< xx:xx:xx:xx:xx:xx形式のMACアドレス
    struct in6_addr         pr_prefix_planeid;  ///< ME6E-PR address prefixのIPv6アドレス+Plane ID
    struct in6_addr         pr_prefix;          ///< ME6E-PR address prefixのIPv6アドレス(表示用)
    int                     v6cidr;             ///< ME6E-PR address prefixのサブネットマスク長+IPv4サブネットマスク長(表示用)
} me6e_pr_entry_t;

///////////////////////////////////////////////////////////////////////////////
//! ME6E-PR Table 構造体
///////////////////////////////////////////////////////////////////////////////
typedef struct _me6e_pr_table_t
{
    pthread_mutex_t         mutex;          ///< 排他用のmutex
    int                     num;            ///< ME6E-PR Entry 数
    me6e_list               entry_list;     ///< ME6E-PR Entry list
} me6e_pr_table_t;

#endif // __ME6EAPP_PR_STRUCT_H__

