/******************************************************************************/
/* ファイル名 : me6eapp_MacManager.h                                          */
/* 機能概要   : Mac Managerクラス ヘッダファイル                              */
/* 修正履歴   : 2013.01.15 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef ___ME6EAPP_MACMANAGER_H__
#define ___ME6EAPP_MACMANAGER_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "me6eapp_IProcessor.h"

////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
IProcessor* MacManager_New();

#endif // ___ME6EAPP_MACMANAGER_H__

