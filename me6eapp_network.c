/******************************************************************************/
/* ファイル名 : me6eapp_network.c                                             */
/* 機能概要   : ネットワーク関連関数 ソースファイル                           */
/* 修正履歴   : 2013.01.10 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <arpa/inet.h>
#include <asm/types.h>
#include <linux/if_tun.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_bridge.h>
#include <linux/sockios.h>

#include "me6eapp_network.h"
#include "me6eapp_config.h"
#include "me6eapp_log.h"
#include "me6eapp_netlink.h"


///////////////////////////////////////////////////////////////////////////////
//! @brief トンネルデバイス生成関数
//!
//! トンネルデバイスを生成する。
//!
//! @param [in]     name       生成するデバイス名
//! @param [in,out] tunnel_dev デバイス構造体
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_network_create_tap(const char* name, struct me6e_device_t* tunnel_dev)
{
    // ローカル変数宣言
    struct ifreq ifr;
    int          result;

    // 引数チェック
    if(tunnel_dev == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG1(me6e_network_create_tap).");
        return -1;
    }

    if((name == NULL) || (strlen(name) == 0)){
        // デバイス名が未設定の場合はエラー
        me6e_logging(LOG_ERR, "Parameter Check NG2(me6e_network_create_tap).");
        return -1;
    }

    if((tunnel_dev->type != ME6E_DEVICE_TYPE_TUNNEL_IPV4) &&
       (tunnel_dev->type != ME6E_DEVICE_TYPE_TUNNEL_IPV6)){
        // typeがトンネルデバイス以外の場合はエラー
        me6e_logging(LOG_ERR, "Parameter Check NG3(me6e_network_create_tap).");
        return -1;
    }
    if((tunnel_dev->option.tunnel.mode != IFF_TUN) &&
       (tunnel_dev->option.tunnel.mode != IFF_TAP)){
        // modeがTUN/TAP以外の場合はエラー
        me6e_logging(LOG_ERR, "Parameter Check NG4(me6e_network_create_tap).");
        return -1;
    }

    // ローカル変数初期化
    memset(&ifr, 0, sizeof(ifr));
    result   = -1;

    // 仮想デバイスオープン
    result = open("/dev/net/tun", O_RDWR);
    if(result < 0){
        me6e_logging(LOG_ERR, "tun device open error : %s.", strerror(errno));
        return result;
    }
    else{
        // ファイルディスクリプタを構造体に格納
        tunnel_dev->option.tunnel.fd = result;
        // close-on-exec フラグを設定
        fcntl(tunnel_dev->option.tunnel.fd, F_SETFD, FD_CLOEXEC);
    }

    strncpy(ifr.ifr_name, name, IFNAMSIZ-1);

    // Flag: IFF_TUN   - TUN device ( no ether header )
    //       IFF_TAP   - TAP device
    //       IFF_NO_PI - no packet information
    ifr.ifr_flags = tunnel_dev->option.tunnel.mode | IFF_NO_PI;
    // 仮想デバイス生成
    result = ioctl(tunnel_dev->option.tunnel.fd, TUNSETIFF, &ifr);
    if(result < 0){
        me6e_logging(LOG_ERR, "ioctl(TUNSETIFF) error : %s.", strerror(errno));
        return result;
    }

    // デバイス名が変わっているかもしれないので、設定後のデバイス名を再取得
    //strcpy(tunnel_dev->name, ifr.ifr_name);

    // デバイスのインデックス番号取得
    tunnel_dev->ifindex = if_nametoindex(ifr.ifr_name);

    // NOARPフラグを設定
    result = me6e_network_set_flags_by_name(ifr.ifr_name, IFF_NOARP);
    if(result != 0){
        me6e_logging(LOG_ERR, "%s fail to set noarp flags : %s.", tunnel_dev->name, strerror(result));
        return -1;
    }

    // MTUの設定
    if(tunnel_dev->mtu > 0){
        result = me6e_network_set_mtu_by_name(ifr.ifr_name, tunnel_dev->mtu);
    }
    else{
        result = me6e_network_get_mtu_by_name(ifr.ifr_name, &tunnel_dev->mtu);
    }
    if(result != 0){
        me6e_logging(LOG_WARNING, "%s configure mtu error : %s.", tunnel_dev->name, strerror(result));
    }

    // MACアドレスの設定
    if(tunnel_dev->hwaddr != NULL){
        result = me6e_network_set_hwaddr_by_name(ifr.ifr_name, tunnel_dev->hwaddr);
    }
    else{
        tunnel_dev->hwaddr = malloc(sizeof(struct ether_addr));
        result = me6e_network_get_hwaddr_by_name(ifr.ifr_name, tunnel_dev->hwaddr);
    }
    if(result != 0){
        me6e_logging(LOG_ERR, "%s configure hwaddr error : %s.", tunnel_dev->name, strerror(result));
        return -1;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Bridgeデバイス生成関数
//!
//! Bridgeデバイスを生成する。
//!
//! @param [in]     name       生成するデバイス名
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_network_create_bridge(const char* name)
{
    int ret;
    int fd;

    // 引数チェック
    if (name == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_network_create_bridge).");
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        me6e_logging(LOG_ERR, "fail to socket create bridge : %s.", strerror(errno));
        return -errno;
    }

#ifdef SIOCBRADDBR
    ret = ioctl(fd, SIOCBRADDBR, name);
    if (ret < 0)
#endif
    {
        char _br[IFNAMSIZ] = {0};
        unsigned long arg[3] = { BRCTL_ADD_BRIDGE, (unsigned long) _br };
        strncpy(_br, name, IFNAMSIZ);
        ret = ioctl(fd, SIOCSIFBR, arg);

        if (ret < 0) {
            me6e_logging(LOG_ERR, "fail to ioctl create bridge : %s.", strerror(errno));
            close(fd);
            return -errno;
        }
    }

    close(fd);
    return 0;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief Bridgeデバイス削除関数
//!
//! Bridgeデバイスを削除する。
//!
//! @param [in]     name       デバイス名
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_network_delete_bridge(const char* name)
{
    int ret;
    int fd;

    // 引数チェック
    if (name == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_network_delete_bridge).");
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        me6e_logging(LOG_ERR, "fail to socket del bridge: %s.", strerror(errno));
        return -errno;
    }

#ifdef SIOCBRDELBR
    ret = ioctl(fd, SIOCBRDELBR, name);
    if (ret < 0)
#endif
    {
        char _br[IFNAMSIZ];
        unsigned long arg[3] = { BRCTL_DEL_BRIDGE, (unsigned long) _br };
        strncpy(_br, name, IFNAMSIZ);
        ret = ioctl(fd, SIOCSIFBR, arg);

        if (ret < 0) {
            me6e_logging(LOG_ERR, "fail to ioctl del bridge: %s.", strerror(errno));
            close(fd);
            return -errno;
        }
    }

    close(fd);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief Bridgeデバイスへのアタッチ関数
//!
//! Bridgeデバイスへデバイスをアタッチする。
//!
//! @param [in]     bridge     Bridgeデバイス名
//! @param [in]     dev        アタッチするデバイス名
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_network_bridge_add_interface(const char *bridge, const char *dev)
{
    struct ifreq ifr;
    int ret;
    int ifindex = if_nametoindex(dev);
    int fd;

    // 引数チェック
    if ((bridge == NULL) || (dev == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_network_bridge_add_interface).");
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        me6e_logging(LOG_ERR, "fail to socket add interface to bridge: %s.", strerror(errno));
        return -errno;
    }

    if (ifindex == 0) {
        close(fd);
        return ENODEV;
    }

    strncpy(ifr.ifr_name, bridge, IFNAMSIZ);
#ifdef SIOCBRADDIF
    ifr.ifr_ifindex = ifindex;
    ret = ioctl(fd, SIOCBRADDIF, &ifr);
    if (ret < 0)
#endif
    {
        unsigned long args[4] = { BRCTL_ADD_IF, ifindex, 0, 0 };
        ifr.ifr_data = (char *) args;
        ret = ioctl(fd, SIOCDEVPRIVATE, &ifr);

        if (ret < 0) {
            me6e_logging(LOG_ERR, "fail to ioctl add interface to bridge: %s.", strerror(errno));
            close(fd);
            return -errno;
        }

    }

    close(fd);
    return 0;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief Bridgeデバイスへのデアタッチ関数
//!
//! Bridgeデバイスへデバイスをデアタッチする。
//!
//! @param [in]     bridge     Bridgeデバイス名
//! @param [in]     dev        デアタッチするデバイス名
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_network_bridge_del_interface(const char *bridge, const char *dev)
{
    struct ifreq ifr;
    int ret;
    int ifindex = if_nametoindex(dev);
    int fd;

    // 引数チェック
    if ((bridge == NULL) || (dev == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_network_bridge_del_interface).");
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        me6e_logging(LOG_ERR, "fail to socket delete interface to bridge: %s.", strerror(errno));
        return -errno;
    }

    if (ifindex == 0) {
        me6e_logging(LOG_ERR, "fail to if_nametoindex.");
        close(fd);
        return ENODEV;
    }

    strncpy(ifr.ifr_name, bridge, IFNAMSIZ);
#ifdef SIOCBRDELIF
    ifr.ifr_ifindex = ifindex;
    ret = ioctl(fd, SIOCBRDELIF, &ifr);
    if (ret< 0)
#endif
    {
        unsigned long args[4] = { BRCTL_DEL_IF, ifindex, 0, 0 };
        ifr.ifr_data = (char *) args;
        ret = ioctl(fd, SIOCDEVPRIVATE, &ifr);

        if (ret < 0) {
            me6e_logging(LOG_ERR, "fail to ioctl delete interface to bridge: %s.", strerror(errno));
            close(fd);
            return -errno;
        }

    }

    close(fd);
    return 0;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief デバイス削除関数
//!
//! インデックス番号に対応するデバイスを削除する。
//!
//! @param [in]  ifindex    削除するデバイスのインデックス番号
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_network_device_delete_by_index(const int ifindex)
{
    struct nlmsghdr*   nlmsg;
    struct ifinfomsg*  ifinfo;
    int                sock_fd;
    struct sockaddr_nl local;
    uint32_t           seq;
    int                ret;
    int                errcd;

    ret = me6e_netlink_open(0, &sock_fd, &local, &seq, &errcd);
    if(ret != RESULT_OK){
        // socket open error
        me6e_logging(LOG_ERR, "Netlink socket error errcd=%d.", errcd);
        return errcd;
    }

    nlmsg = malloc(NETLINK_SNDBUF); // 16kbyte
    if(nlmsg == NULL){
        me6e_logging(LOG_ERR, "Netlink send buffur malloc NG : %s.", strerror(errno));
        me6e_netlink_close(sock_fd);
        return errno;
    }

    memset(nlmsg, 0, NETLINK_SNDBUF);

    ifinfo = (struct ifinfomsg *)(((void*)nlmsg) + NLMSG_HDRLEN);
    ifinfo->ifi_family = AF_UNSPEC;
    ifinfo->ifi_index  = ifindex;

    nlmsg->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    nlmsg->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlmsg->nlmsg_type  = RTM_DELLINK;

    ret = me6e_netlink_transaction(sock_fd, &local, seq, nlmsg, &errcd);
    if(ret != RESULT_OK){
        me6e_netlink_close(sock_fd);
        free(nlmsg);
        return errcd;
    }

    me6e_netlink_close(sock_fd);
    free(nlmsg);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MTU長取得関数(デバイス名)
//!
//! デバイス名に対応するデバイスのMTU長を取得する。
//!
//! @param [in]  ifname    デバイス名
//! @param [out] mtu       取得したMTU長の格納先
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_network_get_mtu_by_name(const char* ifname, int* mtu)
{
    // ローカル変数宣言
    struct ifreq ifr;
    int          result;
    int          sock;

    // 引数チェック
    if(ifname == NULL){
        me6e_logging(LOG_ERR, "ifname is NULL.");
        return EINVAL;
    }
    if(mtu == NULL){
        me6e_logging(LOG_ERR, "mtu is NULL.");
        return EINVAL;
    }

    // ローカル変数初期化
    memset(&ifr, 0, sizeof(struct ifreq));
    result = 0;
    sock   = -1;

    result = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));
    if(result < 0){
        me6e_logging(LOG_ERR, "socket open error : %s.", strerror(errno));
        return errno;
    }
    else{
        sock = result;
    }

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);

    result = ioctl(sock, SIOCGIFMTU, &ifr);
    if(result != 0) {
        me6e_logging(LOG_ERR, "ioctl(SIOCGIFMTU) error : %s.", strerror(errno));
        close(sock);
        return errno;
    }
    close(sock);

    *mtu = ifr.ifr_mtu;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MTU長設定関数(デバイス名)
//!
//! デバイス名に対応するデバイスのMTU長を設定する。
//!
//! @param [in]  ifname    デバイス名
//! @param [in]  mtu       設定するMTU長
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_network_set_mtu_by_name(const char* ifname, const int mtu)
{
    // ローカル変数宣言
    struct ifreq ifr;
    int          result;
    int          sock;

    // 引数チェック
    if(ifname == NULL){
        me6e_logging(LOG_ERR, "ifname is NULL.");
        return EINVAL;
    }

    // ローカル変数初期化
    memset(&ifr, 0, sizeof(struct ifreq));
    result = 0;
    sock   = -1;

    result = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));
    if(result < 0){
        me6e_logging(LOG_ERR, "socket open error : %s.", strerror(errno));
        return errno;
    }
    else{
        sock = result;
    }

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
    ifr.ifr_mtu = mtu;

    result = ioctl(sock, SIOCSIFMTU, &ifr);
    if(result != 0) {
        me6e_logging(LOG_ERR, "ioctl(SIOCSIFMTU) error : %s.", strerror(errno));
        close(sock);
        return errno;
    }
    close(sock);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MACアドレス取得関数(デバイス名)
//!
//! デバイス名に対応するデバイスのMACアドレスを取得する。
//!
//! @param [in]  ifname    デバイス名
//! @param [out] hwaddr    取得したMACアドレスの格納先
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_network_get_hwaddr_by_name(const char* ifname, struct ether_addr* hwaddr)
{
    // ローカル変数宣言
    struct ifreq ifr;
    int          result;
    int          sock;

    // 引数チェック
    if(ifname == NULL){
        me6e_logging(LOG_ERR, "ifname is NULL.");
        return EINVAL;
    }
    if(hwaddr == NULL){
        me6e_logging(LOG_ERR, "hwaddr is NULL.");
        return EINVAL;
    }

    // ローカル変数初期化
    memset(&ifr, 0, sizeof(struct ifreq));
    result = 0;
    sock   = -1;

    result = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));
    if(result < 0){
        me6e_logging(LOG_ERR, "socket open error : %s.", strerror(errno));
        return errno;
    }
    else{
        sock = result;
    }

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);

    result = ioctl(sock, SIOCGIFHWADDR, &ifr);
    if(result != 0) {
        me6e_logging(LOG_ERR, "ioctl(SIOCGIFHWADDR) error : %s.", strerror(errno));
        close(sock);
        return errno;
    }
    close(sock);

    memcpy(hwaddr->ether_addr_octet, ifr.ifr_hwaddr.sa_data, ETH_ALEN);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MACアドレス設定関数(デバイス名)
//!
//! デバイス名に対応するデバイスのMACアドレスを設定する。
//!
//! @param [in]  ifname    デバイス名
//! @param [in]  hwaddr    設定するMACアドレス
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_network_set_hwaddr_by_name(const char* ifname, const struct ether_addr* hwaddr)
{
    // ローカル変数宣言
    struct ifreq ifr;
    int          result;
    int          sock;

    // 引数チェック
    if(ifname == NULL){
        me6e_logging(LOG_ERR, "ifname is NULL.");
        return EINVAL;
    }
    if(hwaddr == NULL){
        me6e_logging(LOG_ERR, "hwaddr is NULL.");
        return EINVAL;
    }

    // ローカル変数初期化
    memset(&ifr, 0, sizeof(struct ifreq));
    result = 0;
    sock   = -1;

    result = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));
    if(result < 0){
        me6e_logging(LOG_ERR, "socket open error : %s.", strerror(errno));
        return errno;
    }
    else{
        sock = result;
    }

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
    ifr.ifr_hwaddr.sa_family = ARPHRD_ETHER;
    memcpy(ifr.ifr_hwaddr.sa_data, hwaddr->ether_addr_octet, ETH_ALEN);

    result = ioctl(sock, SIOCSIFHWADDR, &ifr);
    if(result != 0) {
        me6e_logging(LOG_ERR, "ioctl(SIOCSIFHWADDR) error : %s.", strerror(errno));
        close(sock);
        return errno;
    }
    close(sock);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief フラグ取得関数(デバイス名)
//!
//! デバイス名に対応するデバイスのフラグを取得する。
//!
//! @param [in]  ifname    デバイス名
//! @param [out] flags     取得したフラグの格納先
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_network_get_flags_by_name(const char* ifname, short* flags)
{
    // ローカル変数宣言
    struct ifreq ifr;
    int          result;
    int          sock;

    // 引数チェック
    if(ifname == NULL){
        me6e_logging(LOG_ERR, "ifname is NULL.");
        return EINVAL;
    }
    if(flags == NULL){
        me6e_logging(LOG_ERR, "flags is NULL.");
        return EINVAL;
    }

    // ローカル変数初期化
    memset(&ifr, 0, sizeof(struct ifreq));
    result = 0;
    sock   = -1;

    result = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));
    if(result < 0){
        me6e_logging(LOG_ERR, "socket open error : %s.", strerror(errno));
        return errno;
    }
    else{
        sock = result;
    }

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);

    result = ioctl(sock, SIOCGIFFLAGS, &ifr);
    if(result != 0) {
        me6e_logging(LOG_ERR, "ioctl(SIOCGIFFLAGS) error : %s.", strerror(errno));
        close(sock);
        return errno;
    }
    close(sock);

    *flags = ifr.ifr_flags;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief フラグ設定関数(デバイス名)
//!
//! デバイス名に対応するデバイスのフラグを設定する。
//!
//! @param [in]  ifname    デバイス名
//! @param [in]  flags     設定するフラグ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_network_set_flags_by_name(const char* ifname, const short flags)
{
    // ローカル変数宣言
    struct ifreq ifr;
    int          result;
    int          sock;

    // 引数チェック
    if(ifname == NULL){
        me6e_logging(LOG_ERR, "ifname is NULL.");
        return EINVAL;
    }

    // ローカル変数初期化
    memset(&ifr, 0, sizeof(struct ifreq));
    result = 0;
    sock   = -1;

    result = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));
    if(result < 0){
        me6e_logging(LOG_ERR, "socket open error : %s.", strerror(errno));
        return errno;
    }
    else{
        sock = result;
    }

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);

    // 現在の状態を取得
    result = me6e_network_get_flags_by_name(ifr.ifr_name, &ifr.ifr_flags);
    if(result != 0){
        me6e_logging(LOG_ERR, "fail to get current flags : %s.", strerror(result));
        return result;
    }

    if(flags < 0){
        // 負値の場合はフラグを落とす
        ifr.ifr_flags &= ~(-flags);
    }
    else{
        // 正値の場合はフラグを上げる
        ifr.ifr_flags |= flags;
    }

    result = ioctl(sock, SIOCSIFFLAGS, &ifr);
    if(result != 0) {
        me6e_logging(LOG_ERR, "ioctl(SIOCSIFFLAGS) error : %s.", strerror(errno));
        close(sock);
        return errno;
    }
    close(sock);

    return 0;
}

//////////////////////////////////////////////////////////////////////////////
//! @brief フラグ設定関数(インデックス番号)
//!
//! インデックス番号に対応するデバイスのフラグを設定する。
//!
//! @param [in]  ifindex   デバイスのインデックス番号
//! @param [in]  flags     設定するフラグ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_network_set_flags_by_index(const int ifindex, const short flags)
{
    // ローカル変数宣言
    char ifname[IFNAMSIZ];

    // ローカル変数初期化
    memset(ifname, 0, sizeof(ifname));

    if(if_indextoname(ifindex, ifname) == NULL){
        me6e_logging(LOG_ERR, "if_indextoname error : %s.", strerror(errno));
        return errno;
    }
    return me6e_network_set_flags_by_name(ifname, flags);
}

//////////////////////////////////////////////////////////////////////////////
//! @brief IPアドレス設定関数
//!
//! インデックス番号に対応するデバイスのIPアドレスを設定する。
//!
//! @param [in]  family    設定するアドレス種別(AF_INET or AF_INET6)
//! @param [in]  ifindex   デバイスのインデックス番号
//! @param [in]  addr      設定するIPアドレス
//!                        (IPv4の場合はin_addr、IPv6の場合はin6_addr構造体)
//! @param [in]  prefixlen 設定するIPアドレスのプレフィックス長
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_network_add_ipaddr(
    const int   family,
    const int   ifindex,
    const void* addr,
    const int   prefixlen
)
{
    struct nlmsghdr*   nlmsg;
    struct ifaddrmsg*  ifaddr;
    int                sock_fd;
    struct sockaddr_nl local;
    uint32_t           seq;
    int                ret;
    int                errcd;

    ret = me6e_netlink_open(0, &sock_fd, &local, &seq, &errcd);
    if(ret != RESULT_OK){
        // socket open error
        me6e_logging(LOG_ERR, "Netlink socket error errcd=%d.", errcd);
        return errcd;
    }

    nlmsg = malloc(NETLINK_SNDBUF); // 16kbyte
    if(nlmsg == NULL){
        me6e_logging(LOG_ERR, "Netlink send buffur malloc NG : %s.", strerror(errno));
        me6e_netlink_close(sock_fd);
        return errno;
    }

    memset(nlmsg, 0, NETLINK_SNDBUF);

    ifaddr = (struct ifaddrmsg *)(((void*)nlmsg) + NLMSG_HDRLEN);
    ifaddr->ifa_family     = family;
    ifaddr->ifa_index      = ifindex;
    ifaddr->ifa_prefixlen  = prefixlen;
    ifaddr->ifa_scope      = 0;

    nlmsg->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    nlmsg->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
    nlmsg->nlmsg_type  = RTM_NEWADDR;

    int addrlen = (family == AF_INET) ? sizeof(struct in_addr) : sizeof(struct in6_addr);

    ret = me6e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFA_LOCAL, addr, addrlen);
    if(ret != RESULT_OK){
        me6e_logging(LOG_ERR, "Netlink add attrubute error.");
        me6e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    ret = me6e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFA_ADDRESS, addr, addrlen);
    if(ret != RESULT_OK){
        me6e_logging(LOG_ERR, "Netlink add attrubute error.");
        me6e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    if(family == AF_INET){
        struct in_addr baddr = *((struct in_addr*)addr);
        baddr.s_addr |= htonl(INADDR_BROADCAST >> prefixlen);
        ret = me6e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFA_BROADCAST, &baddr, addrlen);
        if(ret != RESULT_OK){
            me6e_logging(LOG_ERR, "Netlink add attrubute error.");
            me6e_netlink_close(sock_fd);
            free(nlmsg);
            return ENOMEM;
        }
    }

    ret = me6e_netlink_transaction(sock_fd, &local, seq, nlmsg, &errcd);
    if(ret != RESULT_OK){
        me6e_netlink_close(sock_fd);
        free(nlmsg);
        return errcd;
    }

    me6e_netlink_close(sock_fd);
    free(nlmsg);

    return 0;
}

// L2MC-L3UC機能 start
//////////////////////////////////////////////////////////////////////////////
//! @brief IPアドレス削除関数
//!
//! インデックス番号に対応するデバイスのIPアドレスを削除する。
//!
//! @param [in]  family    削除するアドレス種別(AF_INET or AF_INET6)
//! @param [in]  ifindex   デバイスのインデックス番号
//! @param [in]  addr      削除するIPアドレス
//!                        (IPv4の場合はin_addr、IPv6の場合はin6_addr構造体)
//! @param [in]  prefixlen 削除するIPアドレスのプレフィックス長
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_network_del_ipaddr(
    const int   family,
    const int   ifindex,
    const void* addr,
    const int   prefixlen
)
{
    struct nlmsghdr*   nlmsg;
    struct ifaddrmsg*  ifaddr;
    int                sock_fd;
    struct sockaddr_nl local;
    uint32_t           seq;
    int                ret;
    int                errcd;

    ret = me6e_netlink_open(0, &sock_fd, &local, &seq, &errcd);
    if(ret != RESULT_OK){
        // socket open error
        me6e_logging(LOG_ERR, "Netlink socket error errcd=%d.", errcd);
        return errcd;
    }

    nlmsg = malloc(NETLINK_SNDBUF); // 16kbyte
    if(nlmsg == NULL){
        me6e_logging(LOG_ERR, "Netlink send buffur malloc NG : %s.", strerror(errno));
        me6e_netlink_close(sock_fd);
        return errno;
    }

    memset(nlmsg, 0, NETLINK_SNDBUF);

    ifaddr = (struct ifaddrmsg *)(((void*)nlmsg) + NLMSG_HDRLEN);
    ifaddr->ifa_family     = family;
    ifaddr->ifa_index      = ifindex;
    ifaddr->ifa_prefixlen  = prefixlen;
    ifaddr->ifa_scope      = 0;

    nlmsg->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    nlmsg->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
    nlmsg->nlmsg_type  = RTM_DELADDR;

    int addrlen = (family == AF_INET) ? sizeof(struct in_addr) : sizeof(struct in6_addr);

    ret = me6e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFA_LOCAL, addr, addrlen);
    if(ret != RESULT_OK){
        me6e_logging(LOG_ERR, "Netlink add attrubute error.");
        me6e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    ret = me6e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFA_ADDRESS, addr, addrlen);
    if(ret != RESULT_OK){
        me6e_logging(LOG_ERR, "Netlink add attrubute error.");
        me6e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    if(family == AF_INET){
        struct in_addr baddr = *((struct in_addr*)addr);
        baddr.s_addr |= htonl(INADDR_BROADCAST >> prefixlen);
        ret = me6e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFA_BROADCAST, &baddr, addrlen);
        if(ret != RESULT_OK){
            me6e_logging(LOG_ERR, "Netlink add attrubute error.");
            me6e_netlink_close(sock_fd);
            free(nlmsg);
            return ENOMEM;
        }
    }

    ret = me6e_netlink_transaction(sock_fd, &local, seq, nlmsg, &errcd);
    if(ret != RESULT_OK){
        me6e_netlink_close(sock_fd);
        free(nlmsg);
        return errcd;
    }

    me6e_netlink_close(sock_fd);
    free(nlmsg);

    return 0;
}
// L2MC-L3UC機能 end

//////////////////////////////////////////////////////////////////////////////
//! @brief IPアドレス設定関数
//!
//! インデックス番号に対応するデバイスのIPアドレスと
//! preferred lifetimeとvalid lifetimeを設定する。
//! 既にアドレスが存在する場合は、更新する。
//!
//! @param [in]  family     設定するアドレス種別(AF_INET or AF_INET6)
//! @param [in]  ifindex    デバイスのインデックス番号
//! @param [in]  addr       設定するIPアドレス
//!                         (IPv4の場合はin_addr、IPv6の場合はin6_addr構造体)
//! @param [in]  prefixlen  設定するIPアドレスのプレフィックス長
//! @param [in]  ptime      設定するpreferred lifetime
//! @param [in]  vtime      設定するalid lifetime
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_network_add_ipaddr_with_vtime(
    const int   family,
    const int   ifindex,
    const void* addr,
    const int   prefixlen,
    const int   ptime,
    const int   vtime
)
{
    struct nlmsghdr*   nlmsg;
    struct ifaddrmsg*  ifaddr;
    int                sock_fd;
    struct sockaddr_nl local;
    uint32_t           seq;
    int                ret;
    int                errcd;
    struct ifa_cacheinfo cinfo;

    ret = me6e_netlink_open(0, &sock_fd, &local, &seq, &errcd);
    if(ret != RESULT_OK){
        return errcd;
    }

    nlmsg = malloc(NETLINK_SNDBUF); // 16kbyte
    if(nlmsg == NULL){
        me6e_netlink_close(sock_fd);
        return errno;
    }

    memset(nlmsg, 0, NETLINK_SNDBUF);

    ifaddr = (struct ifaddrmsg *)(((void*)nlmsg) + NLMSG_HDRLEN);
    ifaddr->ifa_family     = family;
    ifaddr->ifa_index      = ifindex;
    ifaddr->ifa_prefixlen  = prefixlen;
    ifaddr->ifa_scope      = 0;

    nlmsg->nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    nlmsg->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_REPLACE | NLM_F_ACK;
    nlmsg->nlmsg_type  = RTM_NEWADDR;

    int addrlen = (family == AF_INET) ? sizeof(struct in_addr) : sizeof(struct in6_addr);

    ret = me6e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFA_LOCAL, addr, addrlen);
    if(ret != RESULT_OK){
        me6e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    ret = me6e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFA_ADDRESS, addr, addrlen);
    if(ret != RESULT_OK){
        me6e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    memset(&cinfo, 0, sizeof(cinfo));
    cinfo.ifa_prefered = ptime;
    cinfo.ifa_valid = vtime;
    ret = me6e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFA_CACHEINFO, &cinfo, sizeof(cinfo));
    if(ret != RESULT_OK){
        me6e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    if(family == AF_INET){
        struct in_addr baddr = *((struct in_addr*)addr);
        baddr.s_addr |= htonl(INADDR_BROADCAST >> prefixlen);
        ret = me6e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, IFA_BROADCAST, &baddr, addrlen);
        if(ret != RESULT_OK){
            me6e_netlink_close(sock_fd);
            free(nlmsg);
            return ENOMEM;
        }
    }

    ret = me6e_netlink_transaction(sock_fd, &local, seq, nlmsg, &errcd);
    if(ret != RESULT_OK){
        me6e_netlink_close(sock_fd);
        free(nlmsg);
        return errcd;
    }

    me6e_netlink_close(sock_fd);
    free(nlmsg);

    return 0;
}

//////////////////////////////////////////////////////////////////////////////
//! @brief 経路設定関数
//!
//! インデックス番号に対応するデバイスを経由する経路を設定する。
//! デフォルトゲートウェイを追加する場合はdstをNULLしてgwを設定する。
//! connectedの経路を追加する場合は、gwをNULLに設定する。
//!
//! @param [in]  family    設定するアドレス種別(AF_INET or AF_INET6)
//! @param [in]  ifindex   デバイスのインデックス番号
//! @param [in]  dst       設定する経路の送信先アドレス
//!                        (IPv4の場合はin_addr、IPv6の場合はin6_addr構造体)
//! @param [in]  prefixlen 設定する経路のプレフィックス長
//! @param [in]  gw        設定する経路のゲートウェイアドレス
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_network_add_route(
    const int   family,
    const int   ifindex,
    const void* dst,
    const int   prefixlen,
    const void* gw
)
{
    struct nlmsghdr*   nlmsg;
    struct rtmsg*      rt;
    int                sock_fd;
    struct sockaddr_nl local;
    uint32_t           seq;
    int                ret;
    int                errcd;

    ret = me6e_netlink_open(0, &sock_fd, &local, &seq, &errcd);
    if(ret != RESULT_OK){
        // socket open error
        me6e_logging(LOG_ERR, "Netlink socket error errcd=%d", errcd);
        return errcd;
    }

    nlmsg = malloc(NETLINK_SNDBUF); // 16kbyte
    if(nlmsg == NULL){
        me6e_logging(LOG_ERR, "Netlink send buffur malloc NG : %s.", strerror(errno));
        me6e_netlink_close(sock_fd);
        return errno;
    }

    memset(nlmsg, 0, NETLINK_SNDBUF);

    rt = (struct rtmsg *)(((void*)nlmsg) + NLMSG_HDRLEN);
    rt->rtm_family   = family;
    rt->rtm_table    = RT_TABLE_MAIN;
    rt->rtm_scope    = RT_SCOPE_UNIVERSE;
    rt->rtm_protocol = RTPROT_STATIC;
    rt->rtm_type     = RTN_UNICAST;
    rt->rtm_dst_len  = prefixlen;

    nlmsg->nlmsg_len   = NLMSG_LENGTH(sizeof(struct rtmsg));
    nlmsg->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
    nlmsg->nlmsg_type  = RTM_NEWROUTE;

    int addrlen = (family == AF_INET) ? sizeof(struct in_addr) : sizeof(struct in6_addr);

    if(dst != NULL){
        ret = me6e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_DST, dst, addrlen);
        if(ret != RESULT_OK){
            me6e_logging(LOG_ERR, "Netlink add attrubute error");
            me6e_netlink_close(sock_fd);
            free(nlmsg);
            return ENOMEM;
        }
    }

    if(gw != NULL){
        ret = me6e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_GATEWAY, gw, addrlen);
        if(ret != RESULT_OK){
            me6e_logging(LOG_ERR, "Netlink add attrubute error");
            me6e_netlink_close(sock_fd);
            free(nlmsg);
            return ENOMEM;
        }
    }

    ret = me6e_netlink_addattr_l(nlmsg, NETLINK_SNDBUF, RTA_OIF, &ifindex, sizeof(ifindex));
    if(ret != RESULT_OK){
        me6e_logging(LOG_ERR, "Netlink add attrubute error");
        me6e_netlink_close(sock_fd);
        free(nlmsg);
        return ENOMEM;
    }

    ret = me6e_netlink_transaction(sock_fd, &local, seq, nlmsg, &errcd);
    if(ret != RESULT_OK){
        me6e_netlink_close(sock_fd);
        free(nlmsg);
        return errcd;
    }

    me6e_netlink_close(sock_fd);
    free(nlmsg);

    return 0;
}
