/******************************************************************************/
/* ファイル名 : me6eapp_Controller.h                                          */
/* 機能概要   : パケット制御クラスを制御するクラス ヘッダファイル             */
/* 修正履歴   : 2013.01.08 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __ME6EAPP_CONTROLLER_H__
#define __ME6EAPP_CONTROLLER_H__

////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
int me6e_construct_instances(struct me6e_handler_t* handler);
int me6e_construct_proxyarp(me6e_config_proxy_arp_t* conf, me6e_list* list);
bool me6e_init_instances(struct me6e_handler_t* handler);
void me6e_release_instances(struct me6e_handler_t* handler);
void me6e_destroy_instances(struct me6e_handler_t* handler);
void* me6e_tunnel_backbone_thread(void* arg);
void* me6e_tunnel_stub_thread(void* arg);

#endif // __ME6EAPP_CONTROLLER_H__

