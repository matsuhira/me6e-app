/******************************************************************************/
/* ファイル名 : me6eapp_util.c                                                */
/* 機能概要   : 共通関数 ソースファイル                                       */
/* 修正履歴   : 2013.01.10 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ether.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/ip6.h>

#include "me6eapp_util.h"
#include "me6eapp_log.h"


// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

///////////////////////////////////////////////////////////////////////////////
//! IPv6疑似ヘッダ
///////////////////////////////////////////////////////////////////////////////
struct pseudo_ipv6 {
    struct in6_addr   ipv6_src;             ///< 送信元IPv6アドレス
    struct in6_addr   ipv6_dst;             ///< 送信先IPv6アドレス
    uint32_t          ipv6_plen;            ///< パケットサイズ
    uint16_t          dmy1;                 ///< dummy
    uint8_t           dmy2;                 ///< dummy
    uint8_t           ipv6_nxt;             ///< Next ヘッダ
};
typedef struct pseudo_ipv6 pseudo_ipv6;

///////////////////////////////////////////////////////////////////////////////
//! IPv4疑似ヘッダ
///////////////////////////////////////////////////////////////////////////////
struct pseudo_ip{
    struct in_addr  ip_src;                 ///< 送信元IPv4アドレス
    struct in_addr  ip_dst;                 ///< 送信先IPv4アドレス
    uint8_t         dummy;                  ///< dummy
    uint8_t         ip_p;                   ///< プロトコル
    uint16_t        ip_len;                 ///< パケットサイズ
};
typedef struct pseudo_ipv4 pseudo_ipv4;


///////////////////////////////////////////////////////////////////////////////
//! @brief チェックサム計算関数
//!
//! 引数で指定されたコードとサイズからチェックサム値を計算して返す。
//!
//! @param [in]  buf  チェックサムを計算するコードの先頭アドレス
//! @param [in]  size チェックサムを計算するコードのサイズ
//!
//! @return 計算したチェックサム値
///////////////////////////////////////////////////////////////////////////////
unsigned short me6e_util_checksum(unsigned short *buf, int size)
{
    unsigned long sum = 0;

    while (size > 1) {
        sum += *buf++;
        size -= 2;
    }
    if (size){
        sum += *(u_int8_t *)buf;
    }

    sum  = (sum & 0xffff) + (sum >> 16);    /* add overflow counts */
    sum  = (sum & 0xffff) + (sum >> 16);    /* once again */

    return ~sum;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief チェックサム計算関数(複数ブロック対応)
//!
//! 引数で指定されたコードとサイズからチェックサム値を計算して返す。
//!
//! @param [in]  vec      チェックサムを計算するコード配列の先頭アドレス
//! @param [in]  vec_size チェックサムを計算するコード配列の要素数
//!
//! @return 計算したチェックサム値
///////////////////////////////////////////////////////////////////////////////
unsigned short me6e_util_checksumv(struct iovec* vec, int vec_size)
{
    int i;
    unsigned long sum = 0;

    for(i=0; i<vec_size; i++){
        unsigned short* buf  = (unsigned short*)vec[i].iov_base;
        int             size = vec[i].iov_len;

        while (size > 1) {
            sum += *buf++;
            size -= 2;
        }
        if (size){
            sum += *(u_int8_t *)buf;
        }
    }

    sum  = (sum & 0xffff) + (sum >> 16);    /* add overflow counts */
    sum  = (sum & 0xffff) + (sum >> 16);    /* once again */

    return ~sum;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief チェックサム計算関数(複数ブロック、IPv4、IPv6擬似ヘッダ対応)
//!
//! 引数で指定されたコードとサイズからチェックサム値を計算して返す。
//! vec[0]に、IPv4ヘッダ or IPv6ヘッダが設定されている必要がある。
//!
//! @param [in]  family   擬似ヘッダの種別(AF_INET or AF_INET6) 
//! @param [in]  vec      チェックサムを計算するコード配列の先頭アドレス
//! @param [in]  vec_size チェックサムを計算するコード配列の要素数
//!
//! @return 0 異常
//! @return 計算したチェックサム値
///////////////////////////////////////////////////////////////////////////////
unsigned short me6e_util_pseudo_checksumv(
        const int family,
        const struct iovec* vec,
        const int vec_size)
{
    struct iovec cksum[vec_size];
    int i;

    // 引数チェック
    if ((vec == NULL) || (vec_size == 0)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_util_pseudo_checksumv).");
        return 0;
    }

    if (family == AF_INET6) {
        pseudo_ipv6     pseudo;
        struct ip6_hdr*  ip6_header = (struct ip6_hdr*)(vec[0].iov_base);

        memset(&pseudo, 0, sizeof(pseudo));
        pseudo.ipv6_plen = ip6_header->ip6_plen;
        pseudo.ipv6_nxt  = ip6_header->ip6_nxt;
        memcpy(&(pseudo.ipv6_dst), &(ip6_header->ip6_dst), sizeof(pseudo.ipv6_dst));
        memcpy(&(pseudo.ipv6_src), &(ip6_header->ip6_src), sizeof(pseudo.ipv6_src));
        cksum[0].iov_base = &pseudo;
        cksum[0].iov_len  = sizeof(pseudo);

    } else if (family == AF_INET) {
        // IPv4は現在未実装
        // 異常値0を返す
        return 0;
    } else {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_util_pseudo_checksumv).");
        return 0;
    }

    for (i = 1; i < vec_size; i++) {
        cksum[i].iov_base = vec[i].iov_base;
        cksum[i].iov_len  = vec[i].iov_len;
    }

    return me6e_util_checksumv(cksum, vec_size);
}

