/******************************************************************************/
/* ファイル名 : me6eapp_Capsuling.h                                           */
/* 機能概要   : ME6E カプセリング クラス ヘッダファイル                       */
/* 修正履歴   : 2013.01.15 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef ___ME6EAPP_CAPSULING_H__
#define ___ME6EAPP_CAPSULING_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "me6eapp_IProcessor.h"


////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
IProcessor* Capsuling_New();

#endif // ___ME6EAPP_CAPSULING_H__

