/******************************************************************************/
/* ファイル名 : me6eapp_EherIP.h                                              */
/* 機能概要   : EtherIP ヘッダファイル                                        */
/* 修正履歴   : 2013.01.18 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __ME6EAPP_ETHERIP_H__
#define __ME6EAPP_ETHERIP_H__

#include <asm/byteorder.h>


///////////////////////////////////////////////////////////////////////////////
//! EtherIP バージョン
///////////////////////////////////////////////////////////////////////////////
#define ETHERIP_VERSION 3

///////////////////////////////////////////////////////////////////////////////
//! EtherIPヘッダ定義
///////////////////////////////////////////////////////////////////////////////
struct etheriphdr
{
#if defined(__LITTLE_ENDIAN_BITFIELD)
    __u16   reserved:4,
            version:4,
            reserved2:8;
#elif defined (__BIG_ENDIAN_BITFIELD)
    __u16   version:4,
            reserved:4,
            reserved2:8;
#else
    #error  "Adjust your <asm/byteorder.h> defines"
#endif

};

#endif // __ME6EAPP_ETHERIP_H__

