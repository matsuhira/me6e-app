/******************************************************************************/
/* ファイル名 : me6eapp_timer.h                                               */
/* 機能概要   : タイマ管理ヘッダファイル                                      */
/* 修正履歴   : 2013.01.10 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __ME6EAPP_TIMER_H__
#define __ME6EAPP_TIMER_H__

#include <time.h>

////////////////////////////////////////////////////////////////////////////////
// Timer管理構造体
////////////////////////////////////////////////////////////////////////////////
typedef struct me6e_timer_t me6e_timer_t;

//! タイムアウト時コールバック関数
typedef void (*timer_cbfunc)(const timer_t timerid, void* data);

///////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ
///////////////////////////////////////////////////////////////////////////////
// タイマ初期化関数
me6e_timer_t* me6e_init_timer(void);

// タイマ終了関数
void me6e_end_timer(me6e_timer_t* timer_handler);

// タイマ登録関数
int me6e_timer_register(me6e_timer_t* timer_handler, const time_t expire_sec, timer_cbfunc cb, void* data, timer_t* timerid);

// タイマ停止関数
int me6e_timer_cancel(me6e_timer_t* timer_handler, const timer_t timerid, void** data);

// タイマ再設定関数
int me6e_timer_reset(me6e_timer_t* timer_handler, const timer_t timerid, const long time);

// タイマ情報取得関数
int me6e_timer_get(me6e_timer_t* timer_handler, const timer_t timerid, struct itimerspec* curr_value);

#endif // __ME6EAPP_TIMER_H__

