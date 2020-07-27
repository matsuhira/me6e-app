/******************************************************************************/
/* ファイル名 : me6eapp_ProxyArp.h                                            */
/* 機能概要   : Proxy ARPクラス ヘッダファイル                                */
/* 修正履歴   : 2013.01.15 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef ___ME6EAPP_PROXYARP_H__
#define ___ME6EAPP_PROXYARP_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "me6eapp_IProcessor.h"

///////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
///////////////////////////////////////////////////////////////////////////////
IProcessor* ProxyArp_New();
void ProxyArp_print_table(me6e_proxy_arp_t* handler, int fd);
int ProxyArp_cmd_add_static_entry(me6e_proxy_arp_t* handler,
                const char* v4addr_str, const char* macaddr_str, int fd);
int ProxyArp_cmd_del_static_entry(me6e_proxy_arp_t* handler,
                const char* v4addr_str, int fd);

#endif // ___ME6EAPP_PROXYARP_H__

