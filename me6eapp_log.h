/******************************************************************************/
/* ファイル名 : me6eapp_log.h                                                 */
/* 機能概要   : ログ管理 ヘッダファイル                                       */
/* 修正履歴   : 2013.01.08 Y.Shibata  新規作成                                */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __ME6EAPP_LOG_H__
#define __ME6EAPP_LOG_H__

#include <syslog.h>
#include <stdbool.h>
#include <stdarg.h>

// デバッグログ用マクロ
// デバッグログではファイル名とライン数をメッセージの前に自動的に表示する
#define   DEBUG_LOG(...) _DEBUG_LOG(__FILE__, __LINE__, __VA_ARGS__)
#define  _DEBUG_LOG(FILE, LINE, ...) __DEBUG_LOG(FILE, LINE, __VA_ARGS__)
#define __DEBUG_LOG(FILE, LINE, ...) me6e_logging(LOG_DEBUG, FILE "(" #LINE ") " __VA_ARGS__)

///////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ
///////////////////////////////////////////////////////////////////////////////
void me6e_initial_log(const char* name, const bool debuglog);
void me6e_logging(const int priority, const char *message, ...);

#endif // __ME6EAPP_LOG_H__

