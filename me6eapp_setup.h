/******************************************************************************/
/* ファイル名 : me6eapp_setup.h                                               */
/* 機能概要   : ネットワーク設定クラス ヘッダファイル                         */
/* 修正履歴   : 2013.01.10 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __ME6EAPP_SETUP_H__
#define __ME6EAPP_SETUP_H__

#include "me6eapp.h"

////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
int me6e_setup_uni_plane_prefix(struct me6e_handler_t* handler);
int me6e_setup_multi_plane_prefix(struct me6e_handler_t* handler);
int me6e_create_tunnel_device(struct me6e_handler_t* handler);
int me6e_delete_tunnel_device(struct me6e_handler_t* handler);
int me6e_create_bridge_device(struct me6e_handler_t* handler);
int me6e_delete_bridge_device(struct me6e_handler_t* handler);
int me6e_attach_bridge(struct me6e_handler_t* handler);
int me6e_detach_bridge(struct me6e_handler_t* handler);
int me6e_set_signal(struct me6e_handler_t* handler);
int me6e_start_stub_network(struct me6e_handler_t* handler);
int me6e_setup_backbone_network(struct me6e_handler_t* handler);
int me6e_close_backbone_network(struct me6e_handler_t* handler);
int run_startup_script(struct me6e_handler_t* handler);

#endif // __ME6EAPP_SETUP_H__

