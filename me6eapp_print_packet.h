/******************************************************************************/
/* ファイル名 : me6eapp_print_packet.h                                        */
/* 機能概要   : デバッグ用パケット表示関数 ヘッダファイル                     */
/* 修正履歴   : 2013.01.10 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __ME6EAPP_PRINT_PACKET_H__
#define __ME6EAPP_PRINT_PACKET_H__

////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
void me6e_print_packet(char* packet);
void me6eapp_hex_dump(const char *buf, size_t bufsize);


#endif // __ME6EAPP_PRINT_PACKET_H__

