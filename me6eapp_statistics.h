/******************************************************************************/
/* ファイル名 : me6eapp_statistics.h                                          */
/* 機能概要   : 統計情報管理 ヘッダファイル                                   */
/* 修正履歴   : 2013.01.31 Y.Shibata  新規作成                                */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __ME6EAPP_STATISTICS_H__
#define __ME6EAPP_STATISTICS_H__

#include <stdint.h>

////////////////////////////////////////////////////////////////////////////////
//! 統計情報 構造体
////////////////////////////////////////////////////////////////////////////////
typedef struct _me6e_statistics_t
{
    ////////////////////////////////////////////////////////////////////////////
    // カプセル化
    ////////////////////////////////////////////////////////////////////////////
    //! カプセル化に成功したパケット
    uint32_t capsuling_success_count;
    //! カプセル化に成功したパケット
    uint32_t capsuling_failure_count;

    ////////////////////////////////////////////////////////////////////////////
    // デカプセル化
    ////////////////////////////////////////////////////////////////////////////
    //! カプセル化に成功したパケット
    uint32_t decapsuling_success_count;
    //! カプセル化に成功したパケット(Next Header 不一致)
    uint32_t decapsuling_unmatch_header_count;
    //! カプセル化に成功したパケット(送信エラー)
    uint32_t decapsuling_failure_count;

    ////////////////////////////////////////////////////////////////////////////
    // 代理ARP
    ////////////////////////////////////////////////////////////////////////////
    //! ARP Request受信数
    uint32_t arp_request_recv_count;
    //! ARP Reply送信数
    uint32_t arp_reply_send_count;
    //! 対象外ARP Request受信
    uint32_t disease_not_arp_request_recv_count;
    //! ARP Reply送信エラー数
    uint32_t arp_reply_send_err_count;

    ////////////////////////////////////////////////////////////////////////////
    // 代理NDP
    ////////////////////////////////////////////////////////////////////////////
    //! NSパケット受信数
    uint32_t ns_recv_count;
    //! NAパケット送信数
    uint32_t na_send_count;
    //! NAパケット送信エラー数
    uint32_t na_send_err_count;

} me6e_statistics_t;


///////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ
///////////////////////////////////////////////////////////////////////////////
me6e_statistics_t* me6e_initial_statistics();
void me6e_finish_statistics(me6e_statistics_t* statistics_info);
void me6e_printf_statistics_info(me6e_statistics_t* statistics, int fd);


///////////////////////////////////////////////////////////////////////////////
// カウントアップ用の関数はinlineで定義する
///////////////////////////////////////////////////////////////////////////////
inline void me6e_inc_capsuling_success_count(me6e_statistics_t* statistics)
{
    statistics->capsuling_success_count++;
};

inline void me6e_inc_capsuling_failure_count(me6e_statistics_t* statistics)
{
    statistics->capsuling_failure_count++;
};

inline void me6e_inc_decapsuling_success_count(me6e_statistics_t* statistics)
{
    statistics->decapsuling_success_count++;
};

inline void me6e_inc_decapsuling_unmatch_header_count(me6e_statistics_t* statistics)
{
    statistics->decapsuling_unmatch_header_count++;
};

inline void me6e_inc_decapsuling_failure_count(me6e_statistics_t* statistics)
{
    statistics->decapsuling_failure_count++;
};

inline void me6e_inc_arp_request_recv_count(me6e_statistics_t* statistics)
{
    statistics->arp_request_recv_count++;
};

inline void me6e_inc_arp_reply_send_count(me6e_statistics_t* statistics)
{
    statistics->arp_reply_send_count++;
};

inline void me6e_inc_disease_not_arp_request_recv_count(me6e_statistics_t* statistics)
{
    statistics->disease_not_arp_request_recv_count++;
};

inline void me6e_inc_arp_reply_send_err_count(me6e_statistics_t* statistics)
{
    statistics->arp_reply_send_err_count++;
};

inline void me6e_inc_ns_recv_count(me6e_statistics_t* statistics)
{
    statistics->ns_recv_count++;
};

inline void me6e_inc_na_send_count(me6e_statistics_t* statistics)
{
    statistics->na_send_count++;
};

inline void me6e_inc_na_send_err_count(me6e_statistics_t* statistics)
{
    statistics->na_send_err_count++;
};


#endif // __ME6EAPP_STATISTICS_H__

