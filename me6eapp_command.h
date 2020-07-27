/******************************************************************************/
/* ファイル名 : me6eapp_command.h                                             */
/* 機能概要   : コマンドクラス ヘッダファイル                                 */
/* 修正履歴   : 2013.01.24 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*            : 2016.06.03 H.Koganemaru PR機能の追加                          */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __ME6EAPP_COMMAND_H__
#define __ME6EAPP_COMMAND_H__

#include <stdbool.h>
#include "me6eapp_command_data.h"


//! IPv6 Prefix長最小値
#define CONFIG_IPV6_PREFIX_MIN 1

//! IPv6 Prefix長最大値
#define CONFIG_IPV6_PREFIX_MAX 128

//! コマンドラインの１行あたりの最大文字数
#define OPT_LINE_MAX 256

//! デバイス操作コマンド引数
#define DYNAMIC_OPE_ARGS_NUM_MAX    6
#define DYNAMIC_OPE_DEVICE_MIN_ARGS 6
#define DYNAMIC_OPE_DEVICE_MAX_ARGS 11

// 設定ファイルのbool値
#define OPT_BOOL_ON  "on"
#define OPT_BOOL_OFF "off"
#define OPT_BOOL_ENABLE  "enable"
#define OPT_BOOL_DISABLE "disable"
#define OPT_BOOL_NONE    ""

#define DELIMITER   " \t"

////////////////////////////////////////////////////////////////////////////////
// 関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
bool me6e_command_arp_set_option(
        const char * opt1, const char * opt2,
        struct me6e_command_t* command);

bool me6e_command_ndp_set_option(
        const char * opt1, const char * opt2,
        struct me6e_command_t* command);

bool me6e_command_pr_add_option(
        const char * opt1, const char * opt2, const char * opt3,
        struct me6e_command_t* command);

bool me6e_command_pr_del_option(
        const char * opt1,
        struct me6e_command_t* command);

bool me6e_command_pr_enable_option(
        const char * opt1,
        struct me6e_command_t* command);

bool me6e_command_pr_disable_option(
        const char * opt1,
        struct me6e_command_t* command);

bool me6e_command_pr_load_option(
        const char * opt1,
        struct me6e_command_t* command,
        char * opt2);

bool me6e_command_pr_send(
        struct me6e_command_t* command, 
        char* name);

bool me6e_command_parse_pr_file(
        char* line, 
        int* num, 
        char* cmd_opt[]);

#endif // __ME6EAPP_COMMAND_H__

