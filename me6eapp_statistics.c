/******************************************************************************/
/* ファイル名 : me6eapp_statistics.c                                          */
/* 機能概要   : 統計情報管理 ソースファイル                                   */
/* 修正履歴   : 2013.01.31 Y.Shibata  新規作成                                */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/shm.h>
#include <syslog.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>

#include "me6eapp_statistics.h"
#include "me6eapp_log.h"

///////////////////////////////////////////////////////////////////////////////
//! @brief 統計情報領域作成関数
//!
//! 共有メモリに統計情報用領域を確保する。
//!
//!
//! @return 作成した統計情報領域のポインタ
///////////////////////////////////////////////////////////////////////////////
me6e_statistics_t*  me6e_initial_statistics()
{
    me6e_statistics_t* statistics_info = NULL;

    statistics_info = (me6e_statistics_t*)malloc(sizeof(me6e_statistics_t));

    if (statistics_info == NULL) {
        me6e_logging(LOG_ERR, "fail to malloc for statistics.");
        return NULL;
    }

    memset(statistics_info, 0, sizeof(me6e_statistics_t));

    return statistics_info;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 統計情報領域解放関数
//!
//! 共有メモリに確保している統計情報用領域を解放する。
//!
//! @param [in] statistics_info 統計情報用領域のポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void  me6e_finish_statistics(me6e_statistics_t* statistics_info)
{
    // 引数チェック
    if(statistics_info == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_finish_statistics).");
        return;
    }

    free(statistics_info);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 統計情報出力関数
//!
//! 統計情報を引数で指定されたディスクリプタへ出力する。
//!
//! @param [in] statistics_info 統計情報用領域のポインタ
//! @param [in] fd              統計情報出力先のディスクリプタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void me6e_printf_statistics_info(me6e_statistics_t* statistics_info, int fd)
{
        // 引数チェック
    if(statistics_info == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_printf_statistics_info).");
        return;
    }

    // 統計情報をファイルへ出力する
    dprintf(fd, "-------------------------------------------------------------------\n");
    dprintf(fd, "  Statistics information\n");
    dprintf(fd, "-------------------------------------------------------------------\n");
    dprintf(fd, "【Capsuling】\n");
    dprintf(fd, "   Success count                     : %d \n", statistics_info->capsuling_success_count);
    dprintf(fd, "   Failure count                     : %d \n", statistics_info->capsuling_failure_count);
    dprintf(fd, "\n");
    dprintf(fd, "【DeCapsuling】\n");
    dprintf(fd, "   Success count                     : %d \n", statistics_info->decapsuling_success_count);
    dprintf(fd, "   Failure count(Unmatch EtherIP )   : %d \n", statistics_info->decapsuling_unmatch_header_count);
    dprintf(fd, "   Failure count(Send error)         : %d \n", statistics_info->decapsuling_failure_count);
    dprintf(fd, "\n");
    dprintf(fd, "【Proxy ARP】\n");
    dprintf(fd, "   Recieve ARP Request count         : %d \n", statistics_info->arp_request_recv_count);
    dprintf(fd, "   Send ARP Reply count              : %d \n", statistics_info->arp_reply_send_count);
    dprintf(fd, "   Recieve ARP Request\n");
    dprintf(fd, "      Type is not Ethernet or not IP : %d \n", statistics_info->disease_not_arp_request_recv_count);
    dprintf(fd, "   ARP Reply send error count        : %d \n", statistics_info->arp_reply_send_err_count);
    dprintf(fd, "\n");
    dprintf(fd, "【Proxy NDP】\n");
    dprintf(fd, "   Recieve NS count                  : %d \n", statistics_info->ns_recv_count);
    dprintf(fd, "   Send NA count                     : %d \n", statistics_info->na_send_count);
    dprintf(fd, "   NA send error count               : %d \n", statistics_info->na_send_err_count);
    dprintf(fd, "\n");

    return;
}

