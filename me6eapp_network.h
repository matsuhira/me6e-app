/******************************************************************************/
/* ファイル名 : me6eapp_network.h                                             */
/* 機能概要   : ネットワーク関連関数 ヘッダファイル                           */
/* 修正履歴   : 2013.01.10 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __ME6EAPP_NETWORK_H__
#define __ME6EAPP_NETWORK_H__

#include <unistd.h>
#include <netinet/ether.h>

#include "me6eapp_config.h"

///////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ
///////////////////////////////////////////////////////////////////////////////
int me6e_network_create_tap(const char* name, struct me6e_device_t* tunnel_dev);
int me6e_network_create_bridge(const char* name);
int me6e_network_delete_bridge(const char* name);
int me6e_network_device_delete_by_index(const int ifindex);
int me6e_network_bridge_add_interface(const char *bridge, const char *dev);
int me6e_network_bridge_del_interface(const char *bridge, const char *dev);
int me6e_network_get_mtu_by_name(const char* ifname, int* mtu);
int me6e_network_set_mtu_by_name(const char* ifname, const int mtu);
int me6e_network_get_hwaddr_by_name(const char* ifname, struct ether_addr* hwaddr);
int me6e_network_set_hwaddr_by_name(const char* ifname, const struct ether_addr* hwaddr);
int me6e_network_get_flags_by_name(const char* ifname, short* flags);
int me6e_network_set_flags_by_name(const char* ifname, const short flags);
int me6e_network_set_flags_by_index(const int ifindex, const short flags);
int me6e_network_add_ipaddr(const int family, const int ifindex, const void* addr, const int prefixlen);
int me6e_network_add_route(const int family, const int ifindex, const void* dst, const int prefixlen, const void* gw);
int me6e_network_add_ipaddr_with_vtime(const int family, const int ifindex, const void* addr,
                const int prefixlen, const int ptime, const int vtime);
// L2MC-L3UC機能 start
int me6e_network_del_ipaddr(const int family, const int ifindex, const void* addr, const int prefixlen);
// L2MC-L3UC機能 end

#endif // __ME6EAPP_NETWORK_H__

