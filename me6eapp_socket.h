/******************************************************************************/
/* ファイル名 : me6eapp_socket.h                                              */
/* 機能概要   : ソケット送受信クラス ヘッダファイル                           */
/* 修正履歴   : 2013.01.24 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __ME6EAPP_SOCKET_H__
#define __ME6EAPP_SOCKET_H__

#include "me6eapp_command_data.h"

////////////////////////////////////////////////////////////////////////////////
// 関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
int me6e_socket_send(int sockfd, enum me6e_command_code command, void* data, size_t size, int fd);
int me6e_socket_recv(int sockfd, enum me6e_command_code* command, void* data, size_t size, int* fd);
int me6e_socket_send_cred(int sockfd, enum me6e_command_code command, void* data, size_t size);
int me6e_socket_recv_cred(int sockfd, enum me6e_command_code* command, void* data, size_t size);

#endif // __ME6EAPP_SOCKET_H__

