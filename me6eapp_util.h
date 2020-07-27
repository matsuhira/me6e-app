/******************************************************************************/
/* ファイル名 : me6eapp_util.h                                                */
/* 機能概要   : 共通関数 ヘッダファイル                                       */
/* 修正履歴   : 2013.01.10 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __ME6EAPP_UTIL_H__
#define __ME6EAPP_UTIL_H__

#include <stdbool.h>
#include <sys/uio.h>
#include <netinet/in.h>


////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
unsigned short me6e_util_checksum(unsigned short *buf, int size);
unsigned short me6e_util_checksumv(struct iovec vec[], int vec_size);
unsigned short me6e_util_pseudo_checksumv(
            const int family, const struct iovec* vec, const int vec_size);

///////////////////////////////////////////////////////////////////////////////
//! @brief MACブロードキャストチェック関数
//!
//! MACアドレスがブロードキャストアドレス(全てFF)かどうかをチェックする。
//!
//! @param [in]  mac_addr  MACアドレス
//!
//! @retval true  MACアドレスがブロードキャストアドレスの場合
//! @retval false MACアドレスがブロードキャストアドレスではない場合
///////////////////////////////////////////////////////////////////////////////
inline bool me6e_util_is_broadcast_mac(const unsigned char* mac_addr)
{
    // ローカル変数宣言
    int  i;
    bool result;

    // ローカル変数初期化
    i      = 0;
    result = true;

    // 引数チェック
    if(mac_addr == NULL){
        return false;
    }

    for(i=0; i<ETH_ALEN; i++){
        if(mac_addr[i] != 0xff){
           // アドレスが一つでも0xffでなければ結果をfalseにしてbreak
           result = false;
           break;
        }
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MACマルチキャストチェック関数
//!
//! MACアドレスがマルチキャストアドレスかどうかをチェックする。
//!
//! @param [in]  mac_addr  MACアドレス
//!
//! @retval true  MACアドレスがマルチキャストアドレスの場合
//! @retval false MACアドレスがマルチキャストアドレスではない場合
///////////////////////////////////////////////////////////////////////////////
inline bool me6e_util_is_multicast_mac(const unsigned char* mac_addr)
{
    // ローカル変数宣言
    bool result;

    // ローカル変数初期化
    result = true;

    // 引数チェック
    if(mac_addr == NULL){
        return false;
    }

    // 第1オクテットの最下位ビットが1の場合、マルチキャストになる。
    if((mac_addr[0] & 0x01) != 0){
        result = true;
    }
    else{
        result = false;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6Eアドレス生成関数
//!
//! ME6EプレフックとMACアドレスからME6Eアドレスを生成する。
//!
//! @param [in]     prefix      ME6Eアドレスのプレフィックス
//! @param [in]     macaddr     MACアドレス
//! @param [out]    addr        ME6Eアドレス
//!
//! @retval NULL以外    正常終了
//! @retval NULL        異常終了
///////////////////////////////////////////////////////////////////////////////
inline struct in6_addr* me6e_create_me6eaddr(
                const struct in6_addr* prefix,
                const struct ether_addr* macaddr,
                struct in6_addr* addr)
{
    if ((addr == NULL) || (prefix == NULL) || (macaddr == NULL)) {
        return NULL;
    }

    addr->s6_addr32[0]  = prefix->s6_addr32[0];
    addr->s6_addr32[1]  = prefix->s6_addr32[1];
    addr->s6_addr16[4]  = prefix->s6_addr16[4];
    addr->s6_addr[10]   = macaddr->ether_addr_octet[0];
    addr->s6_addr[11]   = macaddr->ether_addr_octet[1];
    addr->s6_addr[12]   = macaddr->ether_addr_octet[2];
    addr->s6_addr[13]   = macaddr->ether_addr_octet[3];
    addr->s6_addr[14]   = macaddr->ether_addr_octet[4];
    addr->s6_addr[15]   = macaddr->ether_addr_octet[5];

    return addr;
}


#endif // __ME6EAPP_UTIL_H__

