/******************************************************************************/
/* ファイル名 : me6eapp_pr.h                                                  */
/* 機能概要   : ME6E Prefix Resolutionヘッダファイル                          */
/* 修正履歴   : 2016.06.01 S.Anai     新規作成                                */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __ME6EAPP_PR_H__
#define __ME6EAPP_PR_H__

#include <stdbool.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <arpa/inet.h>
#include <pthread.h>


#include "me6eapp_list.h"
#include "me6eapp_pr_struct.h"
#include "me6eapp_command.h"

//! ME6E PR Table 最大エントリー数
#define PR_MAX_ENTRY_NUM    4096

//! CIDR2(プレフィックス)をサブネットマスク(xxx.xxx.xxx.xxx)へ変換
#define PR_CIDR2SUBNETMASK(cidr, mask) mask.s_addr = (cidr == 0 ? 0 : htonl(0xFFFFFFFF << (32 - cidr)))

// ME6E prefix + PlaneID判定
#define IS_EQUAL_ME6E_PREFIX(a, b) \
        (((__const uint32_t *) (a))[0] == ((__const uint32_t *) (b))[0]     \
         && ((__const uint32_t *) (a))[1] == ((__const uint32_t *) (b))[1]  \
         && ((__const uint32_t *) (a))[2] == ((__const uint32_t *) (b))[2])

// ME6E-AS prefix + PlaneID判定
#define IS_EQUAL_ME6E_AS_PREFIX(a, b) \
        (((__const uint32_t *) (a))[0] == ((__const uint32_t *) (b))[0]     \
         && ((__const uint32_t *) (a))[1] == ((__const uint32_t *) (b))[1]  \
         && ((__const uint16_t *) (a))[4] == ((__const uint16_t *) (b))[4])

// ME6E-PR prefix + PlaneID判定
#define IS_EQUAL_ME6E_PR_PREFIX(a, b) IS_EQUAL_ME6E_PREFIX(a, b)

//ME6E-PRコマンドエラーコード
enum me6e_pr_command_error_code
{
    ME6E_PR_COMMAND_NONE,
    ME6E_PR_COMMAND_MODE_ERROR,      ///<動作モードエラー
    ME6E_PR_COMMAND_EXEC_FAILURE,    ///<コマンド実行エラー
    ME6E_PR_COMMAND_ENTRY_FOUND,     ///<エントリ登録有り
    ME6E_PR_COMMAND_ENTRY_NOTFOUND,  ///<エントリ登録無し
    ME6E_PR_COMMAND_MAX
};

///////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ
///////////////////////////////////////////////////////////////////////////////
me6e_pr_table_t* me6e_pr_init_pr_table(struct me6e_handler_t* handler);
void me6e_pr_destruct_pr_table(me6e_pr_table_t* pr_handler);
bool me6e_pr_add_config_entry(me6e_pr_config_table_t* table, me6e_pr_config_entry_t* entry);
bool me6e_pr_del_config_entry(me6e_pr_config_table_t* table, struct in_addr* addr, int mask);
bool me6e_pr_set_config_enable(me6e_pr_config_table_t* table, struct in_addr* addr, int mask, bool enable);
me6e_pr_config_entry_t* me6e_search_pr_config_table( me6e_pr_config_table_t* table, struct ether_addr* addr);

void me6e_pr_config_table_dump(const me6e_pr_config_table_t* table);

bool me6e_pr_add_entry(me6e_pr_table_t* table, me6e_pr_entry_t* entry);
bool me6e_pr_del_entry(me6e_pr_table_t* table, struct ether_addr* addr);
bool me6e_pr_set_enable(me6e_pr_table_t* table, struct ether_addr* addr, bool enable);
me6e_pr_entry_t* me6e_search_pr_table(me6e_pr_table_t* table, struct ether_addr* addr);
void me6e_pr_table_dump(const me6e_pr_table_t* table);

me6e_pr_entry_t* me6e_pr_entry_search_stub(me6e_pr_table_t* table, struct ether_addr* addr);
//bool me6e_pr_prefix_check( me6e_pr_table_t* table, struct in6_addr* addr);

bool me6e_pr_plane_prefix(struct in6_addr* inaddr, int cidr, char* plane_id, struct in6_addr* outaddr);
me6e_pr_entry_t* me6e_pr_conf2entry(struct me6e_handler_t* handler, me6e_pr_config_entry_t* conf);
me6e_pr_entry_t* me6e_pr_command2entry( struct me6e_handler_t* handler, struct me6e_command_pr_data* data);
me6e_pr_config_entry_t* me6e_pr_command2conf(struct me6e_handler_t* handler, struct me6e_command_pr_data* data);

bool me6eapp_pr_check_network_addr(struct in_addr *addr, int cidr);
bool me6eapp_pr_convert_network_addr(struct in_addr *inaddr, int cidr, struct in_addr *outaddr);

bool me6e_pr_add_entry_pr_table(struct me6e_handler_t* handler, struct me6e_command_request_data* req);
bool me6e_pr_del_entry_pr_table(struct me6e_handler_t* handler, struct me6e_command_request_data* req);
bool me6e_pr_delall_entry_pr_table(struct me6e_handler_t* handler, struct me6e_command_request_data* req);
bool me6e_pr_enable_entry_pr_table(struct me6e_handler_t* handler, struct me6e_command_request_data* req);
bool me6e_pr_disable_entry_pr_table(struct me6e_handler_t* handler, struct me6e_command_request_data* req);
void me6e_pr_show_entry_pr_table(me6e_pr_table_t* pr_handler, int fd, char *plane_id);
void me6e_pr_print_error(int fd, enum me6e_pr_command_error_code error_code);
int me6e_pr_setup_uni_plane_prefix(struct me6e_handler_t* handler);

#endif // __ME6EAPP_PR_H__

