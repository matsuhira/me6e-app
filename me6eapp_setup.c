/******************************************************************************/
/* ファイル名 : me6eapp_setup.c                                               */
/* 機能概要   : ネットワーク設定クラス ソースファイル                         */
/* 修正履歴   : 2013.01.10 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <netinet/ip6.h>
#include <netinet/ether.h>
#include <net/if.h>
#include <arpa/inet.h>

#include "me6eapp.h"
#include "me6eapp_setup.h"
#include "me6eapp_log.h"
#include "me6eapp_network.h"
#include "me6eapp_util.h"

#define BB_SND_BUF_SIZE    262142
#define BB_RCV_BUF_SIZE    262142


///////////////////////////////////////////////////////////////////////////////
//! @brief ME6Eユニキャストプレーンプレフィックス生成関数
//!
//! 設定ファイルのME6EユニキャストプレフィックスとPlaneIDの値を元に
//! ME6Eユニキャストプレーンプレフィックスを生成する。
//!
//! @param [in]     handler      ME6Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_setup_uni_plane_prefix(struct me6e_handler_t* handler)
{
    me6e_config_t* conf;
    uint8_t*        src_addr;
    uint8_t*        dst_addr;
    int             prefixlen;
    char address[INET6_ADDRSTRLEN] = { 0 };

    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_setup_uni_plane_prefix).");
        return -1;
    }

    conf = handler->conf;
    if(conf->capsuling->plane_id != NULL){
        // Plane IDが指定されている場合は、Plane IDで初期化する。
        strcat(address, "::");
        strcat(address, conf->capsuling->plane_id);
        strcat(address, ":0:0:0");
        DEBUG_LOG("address =  %s\n", address);

        // 値の妥当性は設定読み込み時にチェックしているので戻り値は見ない
        inet_pton(AF_INET6, address, &handler->unicast_prefix);
    }
    else{
        // Plane IDが指定されていない場合はALL0で初期化する
        handler->unicast_prefix   = in6addr_any;
    }

    // plefix length分コピーする
    src_addr  = conf->capsuling->me6e_address_prefix->s6_addr;
    dst_addr  = handler->unicast_prefix.s6_addr;
    prefixlen = conf->capsuling->unicast_prefixlen;
    for(int i = 0; (i < 16 && prefixlen > 0); i++, prefixlen-=CHAR_BIT){
        if(prefixlen >= CHAR_BIT){
            dst_addr[i] = src_addr[i];
        }
        else{
            dst_addr[i] = (src_addr[i] & (0xff << (CHAR_BIT - prefixlen))) |
                            (dst_addr[i] & ~(0xff << (CHAR_BIT - prefixlen)));
            break;
        }
    }

    DEBUG_LOG("unicast_prefix = %s\n",
            inet_ntop(AF_INET6, &handler->unicast_prefix, address, sizeof(address)));
    return 0;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief ME6Eマルチキャストプレーンプレフィックス生成関数
//!
//! 設定ファイルのME6EマルチキャストプレフィックスとPlaneIDの値を元に
//! ME6Eマルチキャストプレーンプレフィックスを生成する。
//!
//! @param [in]     handler      ME6Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_setup_multi_plane_prefix(struct me6e_handler_t* handler)
{
    me6e_config_t* conf;
    uint8_t*        src_addr;
    uint8_t*        dst_addr;
    char address[INET6_ADDRSTRLEN] = { 0 };

    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_setup_multi_plane_prefix).");
        return -1;
    }

    conf = handler->conf;
    if(conf->capsuling->plane_id != NULL){
        // Plane IDが指定されている場合は、Plane IDで初期化する。
        strcat(address, "::");
        strcat(address, conf->capsuling->plane_id);
        DEBUG_LOG("address =  %s\n", address);

        // 値の妥当性は設定読み込み時にチェックしているので戻り値は見ない
        inet_pton(AF_INET6, address, &handler->multicast_prefix);
    }
    else{
        // Plane IDが指定されていない場合はALL0で初期化する
        handler->multicast_prefix   = in6addr_any;
    }

    // plefix length分コピーする
    src_addr  = conf->capsuling->me6e_multicast_prefix->s6_addr;
    dst_addr  = handler->multicast_prefix.s6_addr;
    for (int i = 0; i < 12; i++){
            dst_addr[i] = src_addr[i];
    }

    dst_addr[12] |= 0x80;

    DEBUG_LOG("multicast_prefix = %s\n",
            inet_ntop(AF_INET6, &handler->multicast_prefix, address, sizeof(address)));
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief トンネルデバイス生成関数
//!
//! 設定ファイルで指定されているトンネルデバイスを生成する。
//!
//! @param [in]     handler      ME6Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_create_tunnel_device(struct me6e_handler_t* handler)
{
    me6e_config_t* conf;

    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_create_tunnel_device).");
        return -1;
    }

    conf = handler->conf;
    // トンネルデバイス生成
    if(me6e_network_create_tap(conf->capsuling->tunnel_device.name,
                            &conf->capsuling->tunnel_device) != 0){
        me6e_logging(LOG_ERR, "fail to create tunnel device.");
        return -1;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief トンネルデバイス削除関数
//!
//! 設定ファイルで指定されているトンネルデバイスを削除する。
//!
//! @param [in]     handler      ME6Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_delete_tunnel_device(struct me6e_handler_t* handler)
{
    me6e_config_t* conf;
    me6e_device_t* device;
    int ret;

    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_delete_tunnel_device).");
        return -1;
    }

    conf = handler->conf;
    device = &(conf->capsuling->tunnel_device);
    if(device->ifindex == -1){
        return 0;
    }

    ret = me6e_network_device_delete_by_index(device->ifindex);
    if(ret != 0) {
        me6e_logging(LOG_ERR, "delete failed : %s.", strerror(ret));
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Bridgeデバイス生成関数
//!
//! 設定ファイルで指定されている名前で、Bridgeデバイスを生成する。
//!
//! @param [in]     handler      ME6Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_create_bridge_device(struct me6e_handler_t* handler)
{
    me6e_config_t* conf;

    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_create_bridge_device).");
        return -1;
    }

    conf = handler->conf;
    // Bridgeデバイス生成
    if(me6e_network_create_bridge(conf->capsuling->bridge_name) != 0){
        me6e_logging(LOG_ERR, "fail to create bridge device.");
        return -1;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Bridgeデバイス削除関数
//!
//! 設定ファイルで指定されている名前で、Bridgeデバイスを削除する。
//!
//! @param [in]     handler      ME6Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_delete_bridge_device(struct me6e_handler_t* handler)
{
    me6e_config_t* conf;
    int ret = 0;

    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_delete_bridge_device).");
        return -1;
    }

    conf = handler->conf;
    // ブリッジデバイスのDOWN
    ret = me6e_network_set_flags_by_name(conf->capsuling->bridge_name, -(IFF_UP | IFF_RUNNING));
    if(ret != 0) {
        me6e_logging(LOG_ERR, "fail to bridge device down : %s.", strerror(ret));
    }

    // ブリッジデバイス削除
    ret = me6e_network_delete_bridge(conf->capsuling->bridge_name);
    if(ret != 0) {
        me6e_logging(LOG_ERR, "fail to delete bridge device.");
        return -1;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Bridgeへのアタッチ関数
//!
//! デバイスをBridgeデバイスへアタッチする。
//!
//! @param [in]     handler      ME6Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_attach_bridge(struct me6e_handler_t* handler)
{
    me6e_config_t* conf;

    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_attach_bridge).");
        return -1;
    }

    conf = handler->conf;

    // トンネルデバイスをBridgeへアタッチ
    if(me6e_network_bridge_add_interface(conf->capsuling->bridge_name,
            (conf->capsuling->tunnel_device.name)) != 0){
        me6e_logging(LOG_ERR, "tunnel device fail to attach bridge device.");
        return -1;
    }

#if 0
/**/    // 物理デバイスの非活性化
/**/    int ret = me6e_network_set_flags_by_name(conf->capsuling->stub_physical_dev, -(IFF_UP | IFF_RUNNING));
/**/    if(ret != 0) {
/**/        me6e_logging(LOG_ERR, "fail to down %s device : %s.",
/**/                conf->capsuling->stub_physical_dev, strerror(ret));
/**/        return -1;
/**/    }
#endif

    // 物理デバイスをBridgeへアタッチ
    if(me6e_network_bridge_add_interface(conf->capsuling->bridge_name,
            (conf->capsuling->stub_physical_dev)) != 0){
        me6e_logging(LOG_ERR, "physical device fail to attach bridge device.");
        return -1;
    }


    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Bridgeからのデダッチ関数
//!
//! デバイスをBridgeデバイスからデタッチする。
//!
//! @param [in]     handler      ME6Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_detach_bridge(struct me6e_handler_t* handler)
{
    me6e_config_t* conf;

    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_detach_bridge).");
        return -1;
    }

    conf = handler->conf;

    // 物理デバイスの非活性化
    int ret = me6e_network_set_flags_by_name(conf->capsuling->stub_physical_dev, -(IFF_UP | IFF_RUNNING));
    if(ret != 0) {
        me6e_logging(LOG_ERR, "fail to down %s device : %s.",
                conf->capsuling->stub_physical_dev, strerror(ret));
        return -1;
    }

    // トンネルデバイスの非活性化
    ret = me6e_network_set_flags_by_index(conf->capsuling->tunnel_device.ifindex, -(IFF_UP | IFF_RUNNING));
    if(ret != 0) {
        me6e_logging(LOG_ERR, "fail to down %s device : %s.",
                conf->capsuling->tunnel_device.name, strerror(ret));
        return -1;
    }

    // 物理デバイスをBridgeからデダッチ
    if(me6e_network_bridge_del_interface(conf->capsuling->bridge_name,
            (conf->capsuling->stub_physical_dev)) != 0){
        me6e_logging(LOG_ERR, "%s device fail to dettach bridge device.",
                conf->capsuling->stub_physical_dev);
        return -1;
    }

    // 物理デバイスの活性化
    ret = me6e_network_set_flags_by_name(conf->capsuling->stub_physical_dev, (IFF_UP | IFF_RUNNING));
    if(ret != 0) {
        me6e_logging(LOG_ERR, "fail to up %s device : %s.",
                conf->capsuling->stub_physical_dev, strerror(ret));
        return -1;
    }

    // トンネルデバイスをBridgeからデダッチ
    if(me6e_network_bridge_del_interface(conf->capsuling->bridge_name,
            (conf->capsuling->tunnel_device.name)) != 0){
        me6e_logging(LOG_ERR, "tunnel device fail to dettach bridge device.");
        return -1;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Stubネットワーク起動関数
//!
//! Stubネットワークの起動をおこなう。具体的には
//!
//!   - BridgeデバイスのUP
//!   - IPv4トンネルデバイスのUP
//!   - multicast snoopingをOFF
//!
//!   をおこなう。
//!
//! @param [in]     handler      ME6Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_start_stub_network(struct me6e_handler_t* handler)
{
    int ret = 0;
    me6e_config_t* conf;

    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_start_stub_network).");
        return -1;
    }

    conf = handler->conf;

    // トンネルデバイスの活性化
    ret = me6e_network_set_flags_by_index(conf->capsuling->tunnel_device.ifindex, (IFF_UP | IFF_RUNNING));
    if(ret != 0) {
        me6e_logging(LOG_ERR, "fail to tunnel device up : %s.", strerror(ret));
        return -1;
    }

    // 物理デバイスの活性化
    ret = me6e_network_set_flags_by_name(conf->capsuling->stub_physical_dev, (IFF_UP | IFF_RUNNING));
    if(ret != 0) {
        me6e_logging(LOG_ERR, "fail to up %s device : %s.",
                conf->capsuling->stub_physical_dev, strerror(ret));
        return -1;
    }

    // ブリッジデバイスの活性化
    ret = me6e_network_set_flags_by_name(conf->capsuling->bridge_name, (IFF_UP | IFF_RUNNING));
    if(ret != 0) {
        me6e_logging(LOG_ERR, "fail to bridge device up : %s.", strerror(ret));
        return -1;
    }

    // multicast snoopingのoff
    char path[128] = { 0 };
    int result = snprintf(path, 128, "/sys/devices/virtual/net/%s/bridge/multicast_snooping",
            conf->capsuling->bridge_name);

    if(result > 0) {
        FILE* fp;
        if ((fp = fopen(path, "w")) != NULL) {
            fprintf(fp, "0");
            fclose(fp);
        }
        else{
            ret = -1;
            me6e_logging(LOG_ERR, "fail to set off multicast snooping.");
        }
    }else {
        ret = -1;
        me6e_logging(LOG_ERR, "fail to set off multicast snooping.");
    }

    // MACフィルタ対応 2016/09/09 add start
    if( conf->capsuling->bridge_hwaddr == NULL ){
    	// BridgeのMACアドレスがConfigに設定されていなければ、MACアドレスの取得を行う。
        conf->capsuling->bridge_hwaddr = malloc(sizeof(struct ether_addr));
        if(conf->capsuling->bridge_hwaddr == NULL){
            me6e_logging(LOG_WARNING, "fail to allocate bridge_hwaddr.\n");
            return -1;
        }
        if(me6e_network_get_hwaddr_by_name(conf->capsuling->bridge_name, conf->capsuling->bridge_hwaddr) != 0){
            me6e_logging(LOG_WARNING, "fail to get bridge hwaddr.\n");
            return -1;
        }
	}
	else{
    	// BridgeのMACアドレスがConfigに設定されていれば、MACアドレスの設定を行う。
        if(me6e_network_set_hwaddr_by_name(conf->capsuling->bridge_name, conf->capsuling->bridge_hwaddr) != 0){
            me6e_logging(LOG_WARNING, "fail to set bridge hwaddr.\n");
            return -1;
        }
    }
    // MACフィルタ対応 2016/09/09 add end

    return ret;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Backboneネットワーク設定関数
//!
//! Backboneネットワークの初期設定をおこなう。具体的には
//!
//!   - ME6E自サーバアドレスの設定
//!   - Packet Infoを有効
//!   - マルチキャスト HOP LIMITの設定
//!   - マルチキャスト ループをOFF
//!   - マルチキャストGroupへJoin
//!
//!   をおこなう。
//!
//! @param [in]     handler      ME6Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_setup_backbone_network(struct me6e_handler_t* handler)
{
    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_setup_backbone_network).");
        return -1;
    }

    // L2MC-L3UC機能 start
    if (handler->conf->capsuling->l2multi_l3uni) {
        char address[INET6_ADDRSTRLEN] = { 0 };
        int result;

        // BackboneNWのME6E 自サーバipv6アドレスの生成
        if (NULL == me6e_create_me6eaddr(&handler->unicast_prefix,
                handler->conf->capsuling->tunnel_device.hwaddr,
                &handler->me6e_own_v6addr)) {
            me6e_logging(LOG_ERR, "fail to create  me6e own server v6address.\n");
            return -1;
        }

        // トンネルデバイスに、IPv6アドレスを設定
        result = me6e_network_add_ipaddr(AF_INET6,
                        if_nametoindex(handler->conf->capsuling->tunnel_device.name),
                        &handler->me6e_own_v6addr, 128);

        if (result == EEXIST) {
            me6e_logging(LOG_ERR, "%s me6e own server address is already exists.",
                    inet_ntop(AF_INET6, &handler->me6e_own_v6addr, address, sizeof(address)));
            return -1;
        }
        else if(result != 0){
            me6e_logging(LOG_ERR, "%s set me6e own server address error : %s.",
                    handler->conf->capsuling->tunnel_device.name, strerror(result));
            return -1;
        }
    }
    // L2MC-L3UC機能 end

    int sock = socket(PF_INET6, SOCK_RAW, ME6E_IPPROTO_ETHERIP);
    if(sock < 0){
        me6e_logging(LOG_ERR, "socket open error : %s.", strerror(errno));
        return errno;
    }

    // 送信バッファサイズの設定
    int send_buf_size = BB_SND_BUF_SIZE;
    if( setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_buf_size, sizeof(send_buf_size)) < 0) {
        me6e_logging(LOG_ERR, "fail to set sockopt SO_SNDBUF : %s.", strerror(errno));
        return errno;
    }

    // 送信バッファサイズの設定確認
    socklen_t len = sizeof(socklen_t);
    send_buf_size = 0;
    if( getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_buf_size, &len) < 0) {
        me6e_logging(LOG_ERR, "fail to get sockopt SO_SNDBUF : %s.", strerror(errno));
        return errno;
    }
    me6e_logging(LOG_INFO, "set send buffer size : %d.", send_buf_size);

    // 受信バッファサイズの設定
    int rcv_buf_size = BB_RCV_BUF_SIZE;
    if( setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcv_buf_size, sizeof(rcv_buf_size)) < 0) {
        me6e_logging(LOG_ERR, "fail to set sockopt SO_SNDBUF : %s.", strerror(errno));
        return errno;
    }

    // 受信バッファサイズの設定確認
    rcv_buf_size = 0;
    if( getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcv_buf_size, &len) < 0) {
        me6e_logging(LOG_ERR, "fail to get sockopt SO_SNDBUF : %s.", strerror(errno));
        return errno;
    }
    me6e_logging(LOG_INFO, "set recieve buffer size : %d.", rcv_buf_size);

    // 送信元情報としてin6_packetinfoを使用
    int on = 1;
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on, sizeof(on))) {
        me6e_logging(LOG_ERR, "fail to set sockopt IPV6_RECVPKTINFO : %s.", strerror(errno));
        close(sock);
        return errno;
    }

    // マルチキャストパケットのhop limit数を設定
    int hops = handler->conf->capsuling->hop_limit;
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof(hops))) {
        me6e_logging(LOG_ERR, "fail to set sockopt IPV6_MULTICAST_HOPS : %s.", strerror(errno));
        close(sock);
        return errno;
    }

    // マルチキャストパケットで、自分で送信したパケットは受け取らない
    int loop = 0;
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof(loop))) {
        me6e_logging(LOG_ERR, "fail to set sockopt IPV6_MULTICAST_LOOP : %s.", strerror(errno));
        close(sock);
        return errno;
    }

    // ME6EマルチキャストアドレスでマルチキャストグループへJoin
    struct ipv6_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.ipv6mr_multiaddr =  handler->multicast_prefix;
    mreq.ipv6mr_interface = if_nametoindex(handler->conf->capsuling->backbone_physical_dev);
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq))) {
        me6e_logging(LOG_ERR, "fail to set sockopt IPV6_JOIN_GROUP : %s.", strerror(errno));
        return errno;
    }

    // ソケットの格納
    handler->conf->capsuling->bb_fd = sock;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Backboneネットワーク終了処理関数
//!
//! Backboneネットワークの終了処理をおこなう。具体的には
//!
//!   - ME6E自サーバアドレスの削除
//!   - Backboneネットワーク送受信用のソケットをクローズ
//!
//!   をおこなう。
//!
//! @param [in]     handler      ME6Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_close_backbone_network(struct me6e_handler_t* handler)
{
    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_close_backbone_network).");
        return -1;
    }

    struct ipv6_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.ipv6mr_multiaddr =  handler->multicast_prefix;
    mreq.ipv6mr_interface = if_nametoindex(handler->conf->capsuling->backbone_physical_dev);
    if (setsockopt(handler->conf->capsuling->bb_fd, IPPROTO_IPV6, IPV6_LEAVE_GROUP, &mreq, sizeof(mreq))) {
        me6e_logging(LOG_ERR, "fail to set sockopt IPV6_LEAVE_GROUP : %s.", strerror(errno));
        return errno;
    }

    // Backboneネットワーク送受信用のソケットにクローズ
    close(handler->conf->capsuling->bb_fd);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief スタートアップスクリプト実行関数
//!
//! @param [in]     handler      ME6Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int run_startup_script(
    struct me6e_handler_t* handler
)
{
    me6e_config_t* conf = handler->conf;

    if(conf->common->startup_script == NULL){
        // スクリプトが指定されていないので、何もせずにリターン
        return 0;
    }

    // コマンド長分のデータ領域確保
    char* command = NULL;

#if 0
/**/    char  uni_addr[INET6_ADDRSTRLEN]   = { 0 };
/**/    char  multi_addr[INET6_ADDRSTRLEN] = { 0 };
/**/
/**/    inet_ntop(AF_INET6, &handler->unicast_prefix,   uni_addr, sizeof(uni_addr));
/**/    inet_ntop(AF_INET6, &handler->multicast_prefix, multi_addr, sizeof(multi_addr));
/**/
/**/    int command_len = asprintf(&command, "%s %s %s %s %s %s 2>&1",
/**/        conf->common->startup_script,
/**/        conf->common->plane_name,
/**/        uni_addr,
/**/        multi_addr,
/**/        conf->capsuling->bridge_name,
/**/        conf->capsuling->tunnel_device.name
/**/    );
#endif

    int command_len = asprintf(&command, "%s 2>&1",
            conf->common->startup_script);

    if(command_len > 0){
        DEBUG_LOG("run startup script : %s\n", command);

        FILE* fp = popen(command, "r");
        if(fp != NULL){
            char buf[256];
            while(fgets(buf, sizeof(buf), fp) != NULL){
                DEBUG_LOG("script output : %s", buf);
            }
            pclose(fp);
        }
        else{
            me6e_logging(LOG_WARNING, "run script error : %s.", strerror(errno));
        }

        free(command);
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief シグナル登録処理関数
//!
//! シグナルを初期化し、登録する。
//!
//! @param [in]     handler      ME6Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_set_signal(struct me6e_handler_t* handler)
{
    // シグナルの登録
    sigset_t sigmask;

    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_set_signal).");
        return -1;
    }

    sigfillset(&sigmask);
    sigdelset(&sigmask, SIGILL);
    sigdelset(&sigmask, SIGSEGV);
    sigdelset(&sigmask, SIGBUS);
    sigprocmask(SIG_BLOCK, &sigmask, &(handler->oldsigmask));

    handler->signalfd = signalfd(-1, &sigmask, 0);
    fcntl(handler->signalfd, F_SETFD, FD_CLOEXEC);

    return 0;
}

