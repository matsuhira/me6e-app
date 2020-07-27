/******************************************************************************/
/* ファイル名 : me6eapp_ProxyNdp.h                                            */
/* 機能概要   : Proxy NDPクラス ヘッダファイル                                */
/* 修正履歴   : 2013.01.15 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef ___ME6EAPP_PROXYNDP_H__
#define ___ME6EAPP_PROXYNDP_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "me6eapp_IProcessor.h"

///////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
///////////////////////////////////////////////////////////////////////////////
IProcessor* ProxyNdp_New();
void ProxyNdp_print_table(me6e_proxy_ndp_t* handler, int fd);
int ProxyNdp_cmd_add_static_entry(me6e_proxy_ndp_t* handler,
                const char* v6addr_str, const char* macaddr_str, const char* if_name, int fd);
int ProxyNdp_cmd_del_static_entry(me6e_proxy_ndp_t* handler,
                const char* v6addr_str, const char* if_name, int fd);

#endif // ___ME6EAPP_PROXYNDP_H__

