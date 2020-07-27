/******************************************************************************/
/* ファイル名 : me6eapp_config.c                                              */
/* 機能概要   : 設定情報管理 ソースファイル                                   */
/* 修正履歴   : 2013.01.08 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*            : 2016.06.03 S.Anai PR機能追加                                  */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <regex.h>
#include <netinet/ether.h>
#include <netinet/ip6.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/if_tun.h>
#include <linux/if_link.h>

#include "me6eapp.h"
#include "me6eapp_config.h"
#include "me6eapp_log.h"
#include "me6eapp_pr.h"


//! 設定ファイルを読込む場合の１行あたりの最大文字数
#define CONFIG_LINE_MAX 256

// 設定ファイルのbool値
#define CONFIG_BOOL_TRUE  "yes"
#define CONFIG_BOOL_FALSE "no"

// 設定ファイルの動作モード種別
#define CONFIG_TUNNEL_MODE_FP     0
#define CONFIG_TUNNEL_MODE_PR     1

// 設定ファイルの数値関連の最小値と最大値
#define CONFIG_TUNNEL_MODE_MIN CONFIG_TUNNEL_MODE_FP
#define CONFIG_TUNNEL_MODE_MAX CONFIG_TUNNEL_MODE_PR

#define CONFIG_IPV4_NETMASK_MIN 1
#define CONFIG_IPV4_NETMASK_MAX 32
#define CONFIG_IPV4_NETMASK_PR_MIN 0

#define CONFIG_IPV6_PREFIX_MIN 1
#define CONFIG_IPV6_PREFIX_MAX 128

#define CONFIG_DEV_NAME_MAX 15
#define CONFIG_PLANE_NAME_MAX 16

#define CONFIG_HOP_LIMIT_MIN 1
#define CONFIG_HOP_LIMIT_MAX 255
#define CONFIG_HOP_LIMIT_DEFAULT 64

#define CONFIG_MTU_MIN 1280
#define CONFIG_MTU_MAX 65521
#define CONFIG_MTU_DEFAULT 1500

#define CONFIG_DEVICE_MTU_MIN 548
#define CONFIG_DEVICE_MTU_MAX 65521

#ifndef DEBUG
#define CONFIG_ARP_EXPIRE_TIME_MIN 301
#else
#define CONFIG_ARP_EXPIRE_TIME_MIN 1
#endif
#define CONFIG_ARP_EXPIRE_TIME_MAX 65535
#define CONFIG_ARP_EXPIRE_TIME_DEFAULT 600

#ifndef DEBUG
#define CONFIG_NDP_EXPIRE_TIME_MIN 301
#else
#define CONFIG_NDP_EXPIRE_TIME_MIN 1
#endif
#define CONFIG_NDP_EXPIRE_TIME_MAX 65535
#define CONFIG_NDP_EXPIRE_TIME_DEFAULT 600

#ifndef DEBUG
#define CONFIG_MAC_EXPIRE_TIME_MIN 301
#else
#define CONFIG_MAC_EXPIRE_TIME_MIN 1
#endif
#define CONFIG_MAC_EXPIRE_TIME_MAX 65535
#define CONFIG_MAC_EXPIRE_TIME_DEFAULT 600

#define CONFIG_HOST_ENTRY_MIN 1
#define CONFIG_HOST_ENTRY_MAX 65535

#define CONFIG_ARP_ENTRY_MIN 1
#define CONFIG_ARP_ENTRY_MAX 65535

#define CONFIG_NDP_ENTRY_MIN 1
#define CONFIG_NDP_ENTRY_MAX 65535

#define CONFIG_MAC_ENTRY_MIN 1
#define CONFIG_MAC_ENTRY_MAX 65535

#define CONFIG_PLANE_PREFIX_MAX    80

// 設定ファイルのセクション名とキー名
// 共通設定
#define SECTION_COMMON                      "common"
#define SECTION_COMMON_PLANE_NAME           "plane_name"
#define SECTION_COMMON_DAEMON               "daemon"
#define SECTION_COMMON_DEBUG_LOG            "debug_log"
#define SECTION_COMMON_STARTUP_SCRIPT       "startup_script"
#define SECTION_COMMON_TUNNEL_MODE          "tunnel_mode"


// カプセリング固有の設定
#define SECTION_CAPSULING                   "capsuling"
#define SECTION_CAPSULING_UNICAST_PREFIX    "me6e_address_prefix"
#define SECTION_CAPSULING_MULTICAST_PREFIX  "me6e_multicast_prefix"
#define SECTION_CAPSULING_PLANE_ID          "plane_id"
#define SECTION_CAPSULING_HOP_LIMIT         "hop_limit"
#define SECTION_CAPSULING_BB_PHY_DEV        "backbone_physical_dev"
#define SECTION_CAPSULING_SB_PHY_DEV        "stub_physical_dev"
#define SECTION_CAPSULING_TUN_NAME          "tunnel_name"
#define SECTION_CAPSULING_TUN_MTU           "tunnel_mtu"
#define SECTION_CAPSULING_TUN_HWADDR        "tunnel_hwaddr"
#define SECTION_CAPSULING_BRG_NAME          "bridge_name"
#define SECTION_CAPSULING_BRG_HWADDR        "bridge_hwaddr"		// MACフィルタ対応 2016/09/12 add
#define SECTION_CAPSULING_L2MULTI_L3UNI     "l2multi_l3uni"
#define SECTION_CAPSULING_HOST_ADDRESS      "me6e_host_address"
#define SECTION_CAPSULING_PR_UNICAST_PREFIX "me6e_pr_unicast_prefix"


// 代理ARP固有の設定
#define SECTION_PROXY_ARP                   "proxy_arp"
#define SECTION_PROXY_ARP_ENABLE            "arp_enable"
#define SECTION_PROXY_ARP_ENTRY_UPDATE      "arp_entry_update"
#define SECTION_PROXY_ARP_AGING_TIME        "arp_aging_time"
#define SECTION_PROXY_ARP_ENTRY_MAX         "arp_entry_max"

// 代理NDP固有の設定
#define SECTION_PROXY_NDP                   "proxy_ndp"
#define SECTION_PROXY_NDP_ENABLE            "ndp_enable"
#define SECTION_PROXY_NDP_ENTRY_UPDATE      "ndp_entry_update"
#define SECTION_PROXY_NDP_AGING_TIME        "ndp_aging_time"
#define SECTION_PROXY_NDP_ENTRY_MAX         "ndp_entry_max"

// MAC管理固有の設定
#define SECTION_MNG_MACADDR                 "mng_macaddr"
#define SECTION_MNG_MACADDR_ENABLE          "mng_macaddr_enable"
#define SECTION_MNG_MACADDR_UPDATE          "mac_entry_update"
#define SECTION_MNG_MACADDR_ENTRY_MAX       "mac_entry_max"
#define SECTION_MNG_MACADDR_VALID_TIME      "mac_vaild_lifetime"
#define SECTION_MNG_MACADDR_HOSTHW_ADDR     "hosthw_addr"

// ME6E-PR固有の設定
#define SECTION_ME6E_PR                      "me6e_pr"
#define SECTION_ME6E_PR_MACADDR              "macaddr"
#define SECTION_ME6E_PR_PR_PREFIX            "pr_prefix"

///////////////////////////////////////////////////////////////////////////////
//! セクション種別
///////////////////////////////////////////////////////////////////////////////
typedef enum _config_section {
    CONFIG_SECTION_NONE      =  0, ///< セクション以外の行
    CONFIG_SECTION_COMMON,         ///< [common]セクション
    CONFIG_SECTION_CAPSULING,      ///< [capsuling]セクション
    CONFIG_SECTION_PROXY_ARP,      ///< [proxy_arp]セクション
    CONFIG_SECTION_PROXY_NDP,      ///< [proxy_ndp]セクション
    CONFIG_SECTION_MNG_MACADDR,    ///< [mng_macaddr]セクション
    CONFIG_SECTION_ME6E_PR,        ///< [me6e_pr]セクション
    CONFIG_SECTION_UNKNOWN   = -1  ///< 不明なセクション
} config_section;

///////////////////////////////////////////////////////////////////////////////
//! KEY/VALUE格納用構造体
///////////////////////////////////////////////////////////////////////////////
typedef struct _config_keyvalue_t {
    char key[CONFIG_LINE_MAX];   ///< KEYの値
    char value[CONFIG_LINE_MAX]; ///< VALUEの値
} config_keyvalue_t;

//! セクション行判定用正規表現 ([で始まって、]で終わる行)
#define SECTION_LINE_REGEX "^\\[(.*)\\]$"
//! KEY VALUE行判定用正規表現 (# 以外で始まって、KEY = VALUE 形式になっている行)
#define KV_REGEX "[ \t]*([^#][^ \t]*)[ \t]*=[ \t]*([^ \t].*)"

// PlanIDをIPv6アドレスに変換した場合で、32bitを超えていないかチェックする
#define CHECK_PLANEID_LEN(a) \
        (((__const uint32_t *) (a))[0] == 0                                   \
         && ((__const uint32_t *) (a))[1] == 0                                \
         && ((__const uint32_t *) (a))[2] == 0)

///////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ
///////////////////////////////////////////////////////////////////////////////
static void config_init(me6e_config_t* config);
static bool config_validate_all(me6e_config_t* config);
static bool config_init_common(me6e_config_t* config);
static void config_destruct_common(me6e_config_common_t* common);
static bool config_parse_common(const config_keyvalue_t* kv, me6e_config_t* config);
static bool config_validate_common(me6e_config_t* config);
static bool config_init_capsuling(me6e_config_t* config);
static void config_destruct_capsuling(me6e_config_capsuling_t* capsuling);
static bool config_parse_capsuling(const config_keyvalue_t* kv, me6e_config_t* config);
static bool config_validate_capsuling(me6e_config_t* config);
static bool config_init_proxy_arp(me6e_config_t* config);
static void config_destruct_proxy_arp(me6e_config_proxy_arp_t* proxy_arp);
static bool config_parse_proxy_arp(const config_keyvalue_t* kv, me6e_config_t* config);
static bool config_validate_proxy_arp(me6e_config_t* config);
static bool config_init_proxy_ndp(me6e_config_t* config);
static void config_destruct_proxy_ndp(me6e_config_proxy_ndp_t* proxy_ndp);
static bool config_parse_proxy_ndp(const config_keyvalue_t* kv, me6e_config_t* config);
static bool config_validate_proxy_ndp(me6e_config_t* config);
static bool config_init_mng_macaddr(me6e_config_t* config);
static void config_destruct_mng_macaddr(me6e_config_mng_macaddr_t* mng_macaddr);
static bool config_parse_mng_macaddr(const config_keyvalue_t* kv, me6e_config_t* config);
static bool config_validate_mng_macaddr(me6e_config_t* config);

static bool config_init_me6e_pr(me6e_config_t* config);
static void config_destruct_me6e_pr(me6e_pr_config_entry_t* pr_config_entry);
static bool config_parse_me6e_pr(const config_keyvalue_t* kv, me6e_config_t* config);
static bool config_validate_me6e_pr(me6e_config_t* config);

static bool config_is_section(const char* str, config_section* section);
static bool config_is_keyvalue(const char* line_str, config_keyvalue_t* kv);
static bool parse_bool(const char* str, bool* output);
static bool parse_int(const char* str, int* output, const int min, const int max);

///////////////////////////////////////////////////////////////////////////////
//! 設定ファイル解析用テーブル
///////////////////////////////////////////////////////////////////////////////
static struct {
    char* name;
    bool  (*init_func)(me6e_config_t* config);
    bool  (*parse_func)(const config_keyvalue_t* kv, me6e_config_t* config);
    bool  (*validate_func)(me6e_config_t* config);
} config_table[] = {
   { "none",                      NULL,                     NULL,                      NULL                       },
   { SECTION_COMMON,       config_init_common,       config_parse_common,       config_validate_common     },
   { SECTION_CAPSULING,    config_init_capsuling,    config_parse_capsuling,    config_validate_capsuling  },
   { SECTION_PROXY_ARP,    config_init_proxy_arp,    config_parse_proxy_arp,    config_validate_proxy_arp  },
   { SECTION_PROXY_NDP,    config_init_proxy_ndp,    config_parse_proxy_ndp,    config_validate_proxy_ndp  },
   { SECTION_MNG_MACADDR,  config_init_mng_macaddr,  config_parse_mng_macaddr,  config_validate_mng_macaddr},
   { SECTION_ME6E_PR,      config_init_me6e_pr,      config_parse_me6e_pr,      config_validate_me6e_pr    },
};

///////////////////////////////////////////////////////////////////////////////
//! @brief 設定ファイル読込み関数
//!
//! 引数で指定されたファイルの内容を読み込み、構造体に格納する。
//! <b>戻り値の構造体は本関数内でallocするので、呼出元で解放すること。</b>
//! <b>解放にはfreeではなく、必ずme6e_config_destruct()関数を使用すること。</b>
//!
//! @param [in] filename 設定ファイル名
//!
//! @return 設定を格納した構造体のポインタ
///////////////////////////////////////////////////////////////////////////////
me6e_config_t* me6e_config_load(const char* filename)
{
    // ローカル変数宣言
    me6e_config_t*           config = NULL;
    FILE*                     fp = NULL;
    char                      line[CONFIG_LINE_MAX] = { 0 };
    config_section            current_section = CONFIG_SECTION_NONE;
    config_section            next_section = CONFIG_SECTION_NONE;
    uint32_t                  line_cnt = 0;
    bool                      result = true;
    config_keyvalue_t         kv;


    // 引数チェック
    if(filename == NULL || strlen(filename) == 0){
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_config_load).");
        return NULL;
    }

    // 設定ファイルオープン
    fp = fopen(filename, "r");
    if(fp == NULL){
        return NULL;
    }

    // 設定保持用構造体alloc
    config = (me6e_config_t*)malloc(sizeof(me6e_config_t));
    if(config == NULL){
        fclose(fp);
        me6e_logging(LOG_ERR, "fail to malloc for me6e config.");
        return NULL;
    }

    // 設定保持用構造体初期化
    config_init(config);

    while(fgets(line, sizeof(line), fp) != NULL) {
        // ラインカウンタをインクリメント
        line_cnt++;

        // 改行文字を終端文字に置き換える
        line[strlen(line)-1] = '\0';

        // コメント行と空行はスキップ
        if((line[0] == '#') || (strlen(line) == 0)){
            continue;
        }

        if(config_is_section(line, &next_section)){
            // セクション行の場合
            // 現在のセクションの整合性チェック
            if(config_table[current_section].validate_func != NULL){
                result = config_table[current_section].validate_func(config);
                if(!result){
                    me6e_logging(LOG_ERR, "Section Validation Error [%s].", config_table[current_section].name);
                    break;
                }
            }
            // 次セクションが不明の場合は処理中断
            if(next_section == CONFIG_SECTION_UNKNOWN){
                // 処理結果を異常に設定
                result = false;
                break;
            }
            // 次セクションの初期化関数をコール(セクション重複チェックなどはここでおこなう)
            if(config_table[next_section].init_func != NULL){
                result = config_table[next_section].init_func(config);
                if(!result){
                    me6e_logging(LOG_ERR, "Section Initialize Error [%s].", config_table[next_section].name);
                    break;
                }
            }

            current_section = next_section;
        }
        else if(config_is_keyvalue(line, &kv)){
             // キーバリュー行の場合、セクションごとの解析関数をコール
             if(config_table[current_section].parse_func != NULL){
                 result = config_table[current_section].parse_func(&kv, config);
                 if(!result){
                     me6e_logging(LOG_ERR, "Parse Error [%s] line %d : %s.",
                             config_table[current_section].name, line_cnt, line);
                     break;
                 }
             }
        }
        else{
            // なにもしない
        }
    }
    fclose(fp);

    if (result) {
        // 最後のセクションの整合性チェック
        if(config_table[current_section].validate_func != NULL){
            result = config_table[current_section].validate_func(config);
            if(!result){
                me6e_logging(LOG_ERR, "Section Validation Error [%s].",
                        config_table[current_section].name);
            }
        }

        if(result){
            // 全体の整合性チェック
            result = config_validate_all(config);
            if(!result){
                me6e_logging(LOG_ERR, "Config Validation Error.");
            }
        }
    }

    if(!result){
        // 処理結果が異常の場合、後始末をしてNULLを返す
        me6e_config_destruct(config);
        config = NULL;
    }
    else{
        // 読み込んだ設定ファイルのフルパスを格納
        config->filename = realpath(filename, NULL);
    }

    return config;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 設定ファイル構造体解放関数
//!
//! 引数で指定された設定ファイル構造体を解放する。
//!
//! @param [in,out] config 解放する設定ファイル構造体へのポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void me6e_config_destruct(me6e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_config_destruct).");
        return;
    }

    free(config->filename);

    if(config->common != NULL){
        config_destruct_common(config->common);
        free(config->common);
    }

    if(config->capsuling != NULL){
        config_destruct_capsuling(config->capsuling);
        free(config->capsuling);
    }

    if(config->arp != NULL){
        config_destruct_proxy_arp(config->arp);
        free(config->arp);
    }

    if(config->ndp != NULL){
        config_destruct_proxy_ndp(config->ndp);
        free(config->ndp);
    }

    if(config->mac != NULL){
        config_destruct_mng_macaddr(config->mac);
        free(config->mac);
    }

    if(config->pr_conf_table != NULL){
        while(!me6e_list_empty(&config->pr_conf_table->entry_list)){
            me6e_list* node = config->pr_conf_table->entry_list.next;
            me6e_pr_config_entry_t* pr_config_entry = node->data;
            config_destruct_me6e_pr(pr_config_entry);
            free(pr_config_entry);
            me6e_list_del(node);
            free(node);
        }
        free(config->pr_conf_table);
    }

    // 設定情報を解放
    free(config);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ハッシュテーブル内部情報出力関数
//!
//! 設定情報のハッシュテーブルを出力する
//!
//! @param [in] key         ハッシュのキー
//! @param [in] value       ハッシュのバリュー
//! @param [in] userdata    ユーザデータ(ファイルディスクリプタ)
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void conf_print_hash_table(const char* key, const void* value, void* userdata)
{
    int fd;

    // 引数チェック
    if((key == NULL) || (value == NULL) || (userdata == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(conf_print_hash_table).");
        return;
    }

    fd = *(int *)(userdata);

    dprintf(fd, "    %-40s|%-20s\n", key, (char *)value);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 設定ファイル構造体ダンプ関数
//!
//! 引数で指定された設定ファイル構造体の内容をダンプする。
//!
//! @param [in] config ダンプする設定ファイル構造体へのポインタ
//! @param [in] fd     出力先のファイルディスクリプタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void me6e_config_dump(const me6e_config_t* config, int fd)
{
    // ローカル変数宣言
    char address[INET6_ADDRSTRLEN] = { 0 };
    char macaddrstr[MAC_ADDRSTRLEN] = { 0 };
    char* strbool[] = { CONFIG_BOOL_FALSE, CONFIG_BOOL_TRUE };

    // 引数チェック
    if(config == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_config_dump).");
        return;
    }

    // ローカル変数初期化

    dprintf(fd, "-------------------------------------------------------------------\n");
    dprintf(fd, "  Configuration information                                        \n");
    dprintf(fd, "-------------------------------------------------------------------\n");

    // 設定ファイル名
    if(config->filename != NULL){
        dprintf(fd, "Config file name = %s\n", config->filename);
        dprintf(fd, "\n");
    }

    // 共通設定
    if(config->common != NULL){
        dprintf(fd, "[%s]\n", SECTION_COMMON);
        if (config->common->plane_name != NULL) {
            dprintf(fd, "    %s = %s\n", SECTION_COMMON_PLANE_NAME, config->common->plane_name);
        }
        dprintf(fd, "    %s = %d\n", SECTION_COMMON_TUNNEL_MODE, config->common->tunnel_mode);
        dprintf(fd, "    %s = %s\n", SECTION_COMMON_DEBUG_LOG, strbool[config->common->debug_log]);
        dprintf(fd, "    %s = %s\n", SECTION_COMMON_DAEMON, strbool[config->common->daemon]);
        if (config->common->startup_script != NULL) {
            dprintf(fd, "    %s = %s\n", SECTION_COMMON_STARTUP_SCRIPT, config->common->startup_script);
        }
        dprintf(fd, "\n");
    }

    // カプセリング固有の設定
    if(config->capsuling != NULL){
        dprintf(fd, "[%s]\n", SECTION_CAPSULING);
        dprintf(fd, "    %s = %s/%d\n",
            SECTION_CAPSULING_UNICAST_PREFIX,
            inet_ntop(AF_INET6, config->capsuling->me6e_address_prefix, address, sizeof(address)),
            config->capsuling->unicast_prefixlen
        );
        dprintf(fd, "    %s = %s\n",
            SECTION_CAPSULING_MULTICAST_PREFIX,
            inet_ntop(AF_INET6, config->capsuling->me6e_multicast_prefix, address, sizeof(address)));
        if(config->capsuling->plane_id != NULL){
            dprintf(fd, "    %s = %s\n", SECTION_CAPSULING_PLANE_ID, config->capsuling->plane_id);
        }
        dprintf(fd, "    %s = %d\n", SECTION_CAPSULING_HOP_LIMIT, config->capsuling->hop_limit);
        dprintf(fd, "    %s = %s\n", SECTION_CAPSULING_BB_PHY_DEV, config->capsuling->backbone_physical_dev);
        dprintf(fd, "    %s = %s\n", SECTION_CAPSULING_SB_PHY_DEV, config->capsuling->stub_physical_dev);
        dprintf(fd, "    %s = %s\n", SECTION_CAPSULING_TUN_NAME, config->capsuling->tunnel_device.name);
        dprintf(fd, "    %s = %d\n", SECTION_CAPSULING_TUN_MTU, config->capsuling->tunnel_device.mtu);
        if(config->capsuling->tunnel_device.hwaddr != NULL){
            dprintf(fd, "    %s = %s\n", SECTION_CAPSULING_TUN_HWADDR, ether_ntoa_r(
                                    config->capsuling->tunnel_device.hwaddr, macaddrstr));
        }
        dprintf(fd, "    %s = %s\n", SECTION_CAPSULING_BRG_NAME, config->capsuling->bridge_name);
        // MACフィルタ対応　2016/09/12 add start
        if( config->capsuling->bridge_hwaddr != NULL ){
            dprintf(fd, "    %s = %s\n", SECTION_CAPSULING_BRG_HWADDR, ether_ntoa_r(
                                                                       config->capsuling->bridge_hwaddr, macaddrstr));
		}
        // MACフィルタ対応　2016/09/12 add end
        dprintf(fd, "    %s = %s\n", SECTION_CAPSULING_L2MULTI_L3UNI, strbool[config->capsuling->l2multi_l3uni]);
        me6e_list* iter;
        me6e_list_for_each(iter, &(config->capsuling->me6e_host_address_list)){
            struct in6_addr* host = iter->data;
            if(host != NULL){
                dprintf(fd, "    %s = %s\n",
                SECTION_CAPSULING_HOST_ADDRESS,
                inet_ntop(AF_INET6, host, address, sizeof(address)));
            }
        }
        if(config->common->tunnel_mode == ME6E_TUNNEL_MODE_PR){
            dprintf(fd, "    %s = %s/%d\n",
                SECTION_CAPSULING_PR_UNICAST_PREFIX,
                inet_ntop(AF_INET6, config->capsuling->me6e_pr_unicast_prefix, address, sizeof(address)),
                config->capsuling->pr_unicat_prefixlen
            );
        }
        dprintf(fd, "\n");
    }

    // 代理ARP固有の設定
    if(config->arp != NULL){
        dprintf(fd, "[%s]\n", SECTION_PROXY_ARP);
        dprintf(fd, "    %s = %s\n", SECTION_PROXY_NDP_ENABLE, strbool[config->arp->arp_enable]);
        dprintf(fd, "    %s = %s\n", SECTION_PROXY_NDP_ENTRY_UPDATE, strbool[config->arp->arp_entry_update]);
        dprintf(fd, "    %s = %d\n", SECTION_PROXY_ARP_AGING_TIME, config->arp->arp_aging_time);
        dprintf(fd, "    %s = %d\n", SECTION_PROXY_ARP_ENTRY_MAX, config->arp->arp_entry_max);
        dprintf(fd, "                IPv4 Address                |     MAC Addr         \n");
        dprintf(fd, "    ----------------------------------------+----------------------\n");
        me6e_hashtable_foreach(config->arp->arp_static_entry, conf_print_hash_table, &fd);
        dprintf(fd, "\n");
    }


    // 代理NDP固有の設定
    if(config->ndp != NULL){
        dprintf(fd, "[%s]\n", SECTION_PROXY_NDP);
        dprintf(fd, "    %s = %s\n", SECTION_PROXY_NDP_ENABLE, strbool[config->ndp->ndp_enable]);
        dprintf(fd, "    %s = %s\n", SECTION_PROXY_NDP_ENTRY_UPDATE, strbool[config->ndp->ndp_entry_update]);
        dprintf(fd, "    %s = %d\n", SECTION_PROXY_NDP_AGING_TIME, config->ndp->ndp_aging_time);
        dprintf(fd, "    %s = %d\n", SECTION_PROXY_NDP_ENTRY_MAX, config->ndp->ndp_entry_max);
        dprintf(fd, "                IPv6 Address                |     MAC Addr         \n");
        dprintf(fd, "    ----------------------------------------+----------------------\n");
        me6e_hashtable_foreach(config->ndp->ndp_static_entry, conf_print_hash_table, &fd);
        dprintf(fd, "\n");
    }

    // MAC管理固有の設定
    if(config->ndp != NULL){
        dprintf(fd, "[%s]\n", SECTION_MNG_MACADDR);
        dprintf(fd, "    %s = %s\n", SECTION_MNG_MACADDR_ENABLE, strbool[config->mac->mng_macaddr_enable]);
        dprintf(fd, "    %s = %s\n", SECTION_MNG_MACADDR_UPDATE, strbool[config->mac->mac_entry_update]);
        dprintf(fd, "    %s = %d\n", SECTION_MNG_MACADDR_VALID_TIME, config->mac->mac_vaild_lifetime);
        dprintf(fd, "    %s = %d\n", SECTION_MNG_MACADDR_ENTRY_MAX, config->mac->mac_entry_max);


        me6e_list* iter;
        me6e_list_for_each(iter, &(config->mac->hosthw_addr_list)){
            struct ether_addr* addr = iter->data;
            if(addr != NULL){
                dprintf(fd, "    %s = %s\n", SECTION_MNG_MACADDR_HOSTHW_ADDR, ether_ntoa_r(addr, macaddrstr));
            }
        }

        dprintf(fd, "\n");
    }

    // トンネルモードがPRの場合には表示する
    if(config->common->tunnel_mode == ME6E_TUNNEL_MODE_PR){
        // ME6E-PR固有の設定
        if(config->pr_conf_table != NULL){
            dprintf(fd, "[%s]\n", SECTION_ME6E_PR);
    
            me6e_list* iter;
            me6e_list_for_each(iter, &(config->pr_conf_table->entry_list)){
                me6e_pr_config_entry_t* pr_config_entry = iter->data;
                if(pr_config_entry != NULL){
                    if((pr_config_entry->macaddr != NULL) && (pr_config_entry->pr_prefix != NULL)){
                        dprintf(fd, "    %s = %s\n",
                            SECTION_ME6E_PR_MACADDR,
                            ether_ntoa_r(pr_config_entry->macaddr, macaddrstr)
                        );
                        dprintf(fd, "    %s = %s\n",
                            SECTION_ME6E_PR_PR_PREFIX,
                            inet_ntop(AF_INET6, pr_config_entry->pr_prefix, address, sizeof(address))
                        );
                    }
                }
            }
            dprintf(fd, "\n");
        }
    }

    dprintf(fd, "-------------------------------------------------------------------\n");

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 設定情報格納用構造体初期化関数
//!
//! 設定情報格納用構造体を初期化する。
//!
//! @param [in,out] config   設定情報格納用構造体へのポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void config_init(me6e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_init).");
        return;
    }

    // 設定ファイル名
    config->filename    = NULL;
    // 共通設定
    config->common      = NULL;
    // カプセリング固有の設定
    config->capsuling   = NULL;
    // 代理ARP固有の設定
    config->arp         = NULL;
    // 代理NDP固有の設定
    config->ndp         = NULL;
    // MAC管理固有の設定
    config->mac         = NULL;

    // ME6E-PR Config Table
    config->pr_conf_table = malloc(sizeof(me6e_pr_config_table_t));
    if(config->pr_conf_table == NULL){
        return;
    }
    config->pr_conf_table->num = 0;
    me6e_list_init(&config->pr_conf_table->entry_list);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 設定情報整合性チェック関数
//!
//! 設定情報全体での整合性をチェックする。
//!
//! @param [in] config  設定情報格納用構造体へのポインタ
//!
//! @retval true   整合性OK
//! @retval false  整合性NG
///////////////////////////////////////////////////////////////////////////////
static bool config_validate_all(me6e_config_t* config)
{

    // 引数チェック
    if(config == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_validate_all).");
        return false;
    }

    // 必須情報が設定されているかチェック
    if(config->common == NULL){
        me6e_logging(LOG_ERR, "[%s] section is not found", SECTION_COMMON);
        return false;
    }
    if(config->capsuling == NULL){
        me6e_logging(LOG_ERR, "[%s] section is not found", SECTION_CAPSULING);
        return false;
    }

    if(config->arp == NULL){
        me6e_logging(LOG_ERR, "[%s] section is not found", SECTION_PROXY_ARP);
        return false;
    }

    if(config->ndp == NULL){
        me6e_logging(LOG_ERR, "[%s] section is not found", SECTION_PROXY_NDP);
        return false;
    }

    if(config->mac == NULL){
        me6e_logging(LOG_ERR, "[%s] section is not found", SECTION_MNG_MACADDR);
        return false;
    }

    if(config->common->tunnel_mode == ME6E_TUNNEL_MODE_PR){
        // トンネルモードがPRの場合にのみチェック
        if(config->pr_conf_table == NULL){
            me6e_logging(LOG_ERR, "[%s] section is not found", SECTION_ME6E_PR);
            return false;
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 共通設定格納用構造体初期化関数
//!
//! 共通設定格納用構造体を初期化する。
//!
//! @param [in,out] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool config_init_common(me6e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_init_common).");
        return false;
    }

    if(config->common != NULL){
        // 既に共通設定が格納済みの場合はNGを返す。
        me6e_logging(LOG_ERR, "[%s] section is already loaded. check duplicate.", SECTION_COMMON);
        return false;
    }

    config->common = malloc(sizeof(me6e_config_common_t));
    if(config->common == NULL){
        me6e_logging(LOG_ERR, "fail to malloc for me6e config common.");
        return false;
    }

    // 設定初期化
    config->common->plane_name          = NULL;
    config->common->tunnel_mode         = ME6E_TUNNEL_MODE_NONE;
    config->common->debug_log           = false;
    config->common->daemon              = true;
    config->common->startup_script      = NULL;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 共通設定格納用構造体解放関数
//!
//! 共通設定格納用構造体を解放する。
//!
//! @param [in,out] common   共通設定格納用構造体へのポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void config_destruct_common(me6e_config_common_t* common)
{

    // 引数チェック
    if(common == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_destruct_common).");
        return;
    }

    free(common->plane_name);
    free(common->startup_script);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 共通設定の解析関数
//!
//! 引数で指定されたKEY/VALUEを解析し、
//! 設定値を共通設定構造体に格納する。
//!
//! @param [in]  kv      設定ファイルから読込んだ一行情報
//! @param [out] config  設定情報格納先の構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了(不正な値がKEY/VALUEに設定されていた場合)
///////////////////////////////////////////////////////////////////////////////
static bool config_parse_common(
    const config_keyvalue_t*  kv,
          me6e_config_t*     config
)
{
    // ローカル変数宣言
    bool result;


    // 引数チェック
    if((kv == NULL) || (config == NULL) || (config->common == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_parse_common).");
        return false;
    }

    // ローカル変数初期化
    result = true;

    if(!strcasecmp(SECTION_COMMON_PLANE_NAME, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_COMMON_PLANE_NAME);
        if(config->common->plane_name == NULL){
            if(strlen(kv->value) > CONFIG_PLANE_NAME_MAX) {
                result = false;
            } else {
                config->common->plane_name = strdup(kv->value);
            }
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_COMMON_TUNNEL_MODE, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_COMMON_TUNNEL_MODE);
        if(config->common->tunnel_mode == ME6E_TUNNEL_MODE_NONE){
            int  tmp;
            result = parse_int(kv->value, &tmp, CONFIG_TUNNEL_MODE_MIN, CONFIG_TUNNEL_MODE_MAX);
            if(result){
                switch(tmp){
                case CONFIG_TUNNEL_MODE_FP:
                    config->common->tunnel_mode = ME6E_TUNNEL_MODE_FP;
                    break;
                case CONFIG_TUNNEL_MODE_PR:
                    config->common->tunnel_mode = ME6E_TUNNEL_MODE_PR;
                    break;
                default:
                    result = false;
                    break;
                }
            }
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_COMMON_DAEMON, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_COMMON_DAEMON);
        result = parse_bool(kv->value, &config->common->daemon);
    }
    else if(!strcasecmp(SECTION_COMMON_DEBUG_LOG, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_COMMON_DEBUG_LOG);
        result = parse_bool(kv->value, &config->common->debug_log);
    }
    else if(!strcasecmp(SECTION_COMMON_STARTUP_SCRIPT, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_COMMON_STARTUP_SCRIPT);
        if(config->common->startup_script == NULL){
            config->common->startup_script = realpath(kv->value, NULL);
            if (config->common->startup_script == NULL) {
                result = false;
            }
        }
        else{
            result = false;
        }
    }
    else{
        // 不明なキーなのでスキップ
        me6e_logging(LOG_WARNING, "Ignore unknown key : %s\n", kv->key);
    }


    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 共通設定の整合性チェック関数
//!
//! 共通設定内部での整合性をチェックする
//!
//! @param [in] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  整合性OK
//! @retval false 整合性NG
///////////////////////////////////////////////////////////////////////////////
static bool config_validate_common(me6e_config_t* config)
{

    // 引数チェック
    if(config == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_validate_common).");
        return false;;
    }

    if(config->common == NULL){
        return false;
    }

    // 必須情報が設定されているかをチェック
    if(config->common->plane_name == NULL){
        me6e_logging(LOG_ERR, "[%s] is not found", SECTION_COMMON_PLANE_NAME);
        return false;
    }

    switch(config->common->tunnel_mode){
    case ME6E_TUNNEL_MODE_FP:
    case ME6E_TUNNEL_MODE_PR:
        break;
    default:
        me6e_logging(LOG_ERR, "[%s] is not found", SECTION_COMMON_TUNNEL_MODE);
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief カプセリング固有設定格納用構造体初期化関数
//!
//! カプセリング固有設定格納用構造体を初期化する。
//!
//! @param [in,out] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool config_init_capsuling(me6e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_init_capsuling).");
        return false;
    }

    if(config->capsuling != NULL){
        // 既にカプセリング固有の設定が格納済みの場合はNGを返す。
        me6e_logging(LOG_ERR, "[%s] section is already loaded. check duplicate.", SECTION_CAPSULING);
        return false;
    }

    config->capsuling = malloc(sizeof(me6e_config_capsuling_t));
    if(config->capsuling == NULL){
        me6e_logging(LOG_ERR, "fail to malloc for me6e config capsuling.");
        return false;
    }

    // 設定初期化
    config->capsuling->me6e_address_prefix              = NULL;
    config->capsuling->unicast_prefixlen                = -1;
    config->capsuling->me6e_multicast_prefix            = NULL;
    config->capsuling->plane_id                         = NULL;
    config->capsuling->hop_limit                        = CONFIG_HOP_LIMIT_DEFAULT;
    config->capsuling->backbone_physical_dev            = NULL;
    config->capsuling->bb_fd                            = -1;
    config->capsuling->stub_physical_dev                = NULL;
    config->capsuling->me6e_pr_unicast_prefix           = NULL;
    config->capsuling->pr_unicat_prefixlen              = -1;
    config->capsuling->pr_unicast_prefixplaneid         = NULL;

    config->capsuling->tunnel_device.type               = ME6E_DEVICE_TYPE_TUNNEL_IPV4;
    config->capsuling->tunnel_device.name               = NULL;
    config->capsuling->tunnel_device.physical_name      = NULL; // 未使用
    config->capsuling->tunnel_device.ipv4_address       = NULL; // 未使用
    config->capsuling->tunnel_device.ipv4_netmask       = -1;   // 未使用
    config->capsuling->tunnel_device.ipv4_gateway       = NULL; // 未使用
    config->capsuling->tunnel_device.ipv6_address       = NULL; // 未使用
    config->capsuling->tunnel_device.ipv6_prefixlen     = -1;   // 未使用
    config->capsuling->tunnel_device.mtu                = -1;
    config->capsuling->tunnel_device.hwaddr             = NULL;
    config->capsuling->tunnel_device.ifindex            = -1;
    config->capsuling->tunnel_device.option.tunnel.mode = IFF_TAP;
    config->capsuling->tunnel_device.option.tunnel.fd   = -1;

    config->capsuling->bridge_name                      = NULL;
    config->capsuling->bridge_hwaddr                    = NULL;  // MACフィルタ対応　2016/09/12 add
    config->capsuling->l2multi_l3uni                    = false;
    me6e_list_init(&(config->capsuling->me6e_host_address_list));

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief カプセリング固有設定定格納用構造体解放関数
//!
//! カプセリング固有設定格納用構造体を解放する。
//!
//! @param [in,out] capsuling   カプセリング固有設定格納用構造体へのポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void config_destruct_capsuling(me6e_config_capsuling_t* capsuling)
{

    // 引数チェック
    if(capsuling == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_destruct_capsuling).");
        return;
    }

    free(capsuling->me6e_address_prefix);
    free(capsuling->me6e_multicast_prefix);
    free(capsuling->plane_id);
    free(capsuling->backbone_physical_dev);
    free(capsuling->stub_physical_dev);
    free(capsuling->tunnel_device.name);
    free(capsuling->tunnel_device.hwaddr);
    free(capsuling->bridge_name);
    free(capsuling->bridge_hwaddr);			// MACフィルタ対応　2016/09/12 add
    free(capsuling->me6e_pr_unicast_prefix);
    free(capsuling->pr_unicast_prefixplaneid);

    while(!me6e_list_empty(&capsuling->me6e_host_address_list)){
        me6e_list* node = capsuling->me6e_host_address_list.next;
        free(node->data);
        me6e_list_del(node);
        free(node);
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief カプセリング固有設定の解析関数
//!
//! 引数で指定されたKEY/VALUEを解析し、
//! 設定値をカプセリング固有設定構造体に格納する。
//!
//! @param [in]  kv       設定ファイルから読込んだ一行情報
//! @param [out] config   設定情報格納先の構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了(不正な値がKEY/VALUEに設定されていた場合)
///////////////////////////////////////////////////////////////////////////////
static bool config_parse_capsuling(
        const config_keyvalue_t*  kv,
        me6e_config_t*     config
)
{
    // ローカル変数宣言
    bool result;

    // 引数チェック
    if((kv == NULL) || (config == NULL) || (config->capsuling == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_parse_capsuling).");
        return false;
    }

    // ローカル変数初期化
    result = true;

    if(!strcasecmp(SECTION_CAPSULING_UNICAST_PREFIX, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_CAPSULING_UNICAST_PREFIX);
        if(config->capsuling->me6e_address_prefix == NULL){
            config->capsuling->me6e_address_prefix = malloc(sizeof(struct in6_addr));
            if(config->capsuling->me6e_address_prefix == NULL){
                me6e_logging(LOG_ERR, "fail to malloc for %s.", SECTION_CAPSULING_UNICAST_PREFIX);
                return false;
            }
            result = parse_ipv6address(
                kv->value,
                config->capsuling->me6e_address_prefix,
                &config->capsuling->unicast_prefixlen
            );
            if(result){
                // ユニキャストアドレスかどうかチェック
                result = !IN6_IS_ADDR_MULTICAST(config->capsuling->me6e_address_prefix);
            }
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_CAPSULING_MULTICAST_PREFIX, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_CAPSULING_MULTICAST_PREFIX);
        if(config->capsuling->me6e_multicast_prefix == NULL){
            config->capsuling->me6e_multicast_prefix = malloc(sizeof(struct in6_addr));
            if(config->capsuling->me6e_multicast_prefix == NULL){
                me6e_logging(LOG_ERR, "fail to malloc for %s.", SECTION_CAPSULING_MULTICAST_PREFIX);
                return false;
            }
            result = parse_ipv6address(
                kv->value,
                config->capsuling->me6e_multicast_prefix,
                NULL
            );
            if(result){
                // マルチキャストアドレスかどうかチェック
                result = IN6_IS_ADDR_MULTICAST(config->capsuling->me6e_multicast_prefix);
            }
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_CAPSULING_PLANE_ID, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_CAPSULING_PLANE_ID);
        if(config->capsuling->plane_id == NULL){
            char address[INET6_ADDRSTRLEN] = { 0 };
            strcat(address, "::");
            strcat(address, kv->value);
            struct in6_addr tmp;
            if(!parse_ipv6address(address, &tmp, NULL)){
                result = false;
            } else{
                // 32bit以内かチェック
                if (CHECK_PLANEID_LEN(&tmp)) {
                    config->capsuling->plane_id = strdup(kv->value);
                } else {
                    result = false;
                }
            }
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_CAPSULING_HOP_LIMIT, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_CAPSULING_HOP_LIMIT);
        result = parse_int(kv->value, &config->capsuling->hop_limit,
            CONFIG_HOP_LIMIT_MIN, CONFIG_HOP_LIMIT_MAX);
    }
    else if(!strcasecmp(SECTION_CAPSULING_BB_PHY_DEV, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_CAPSULING_BB_PHY_DEV);
        if(config->capsuling->backbone_physical_dev == NULL){
            if(strlen(kv->value) > CONFIG_DEV_NAME_MAX) {
                result = false;
            } else {
                config->capsuling->backbone_physical_dev = strdup(kv->value);
            }
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_CAPSULING_SB_PHY_DEV, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_CAPSULING_SB_PHY_DEV);
        if(config->capsuling->stub_physical_dev == NULL){
            if(strlen(kv->value) > CONFIG_DEV_NAME_MAX) {
                result = false;
            } else {
                config->capsuling->stub_physical_dev = strdup(kv->value);
            }
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_CAPSULING_TUN_NAME, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_CAPSULING_TUN_NAME);
        if(config->capsuling->tunnel_device.name == NULL){
            if(strlen(kv->value) > CONFIG_DEV_NAME_MAX) {
                result = false;
            } else {
                config->capsuling->tunnel_device.name = strdup(kv->value);
            }
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_CAPSULING_TUN_MTU, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_CAPSULING_TUN_MTU);
        result = parse_int(kv->value, &config->capsuling->tunnel_device.mtu, CONFIG_MTU_MIN, CONFIG_MTU_MAX);
    }
    else if(!strcasecmp(SECTION_CAPSULING_TUN_HWADDR, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_CAPSULING_TUN_HWADDR);
        if(config->capsuling->tunnel_device.hwaddr == NULL){
            config->capsuling->tunnel_device.hwaddr = malloc(sizeof(struct ether_addr));
            if(config->capsuling->tunnel_device.hwaddr == NULL){
                me6e_logging(LOG_ERR, "fail to malloc for %s.", SECTION_CAPSULING_TUN_HWADDR);
                return false;
            }
            result = parse_macaddress(kv->value, config->capsuling->tunnel_device.hwaddr);
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_CAPSULING_BRG_NAME, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_CAPSULING_BRG_NAME);
        if(config->capsuling->bridge_name == NULL){
            if(strlen(kv->value) > CONFIG_DEV_NAME_MAX) {
                result = false;
            } else {
                config->capsuling->bridge_name = strdup(kv->value);
            }
        }
        else{
            result = false;
        }
    }
	// MACフィルタ対応 2016/09/12 add start
    else if(!strcasecmp(SECTION_CAPSULING_BRG_HWADDR, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_CAPSULING_BRG_HWADDR);
        if(config->capsuling->bridge_hwaddr == NULL){
            config->capsuling->bridge_hwaddr = malloc(sizeof(struct ether_addr));
            if(config->capsuling->bridge_hwaddr == NULL){
                me6e_logging(LOG_ERR, "fail to malloc for %s.", SECTION_CAPSULING_BRG_HWADDR);
                return false;
            }
            result = parse_macaddress(kv->value, config->capsuling->bridge_hwaddr);
        }
        else{
            result = false;
        }
    }
	// MACフィルタ対応 2016/09/12 add end
    else if(!strcasecmp(SECTION_CAPSULING_L2MULTI_L3UNI, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_CAPSULING_L2MULTI_L3UNI);
        result = parse_bool(kv->value, &config->capsuling->l2multi_l3uni);
    }
    else if(!strcasecmp(SECTION_CAPSULING_HOST_ADDRESS, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_CAPSULING_HOST_ADDRESS);

        me6e_list* node = malloc(sizeof(me6e_list));
        if(node == NULL){
            me6e_logging(LOG_ERR, "fail to malloc for %s.", SECTION_CAPSULING_HOST_ADDRESS);
            return false;
        }

        struct in6_addr* host = malloc(sizeof(struct in6_addr));
        if(host == NULL){
            me6e_logging(LOG_ERR, "fail to malloc for %s.", SECTION_CAPSULING_HOST_ADDRESS);
            free(node);
            return false;
        }

        result = parse_ipv6address(kv->value, host, NULL);

        // アドレス情報ををノードに設定して、ノードをME6Eサーバアドレスのリストの最後に追加
        me6e_list_init(node);
        me6e_list_add_data(node, host);
        me6e_list_add_tail(&config->capsuling->me6e_host_address_list, node);

        if(result){
            // ユニキャストアドレスかどうかチェック
            result = !IN6_IS_ADDR_MULTICAST(host);
        }
    }
    // PRモードの場合、SECTION_CAPSULING_PR_UNICAST_PREFIXをチェック
    else if(config->common->tunnel_mode == ME6E_TUNNEL_MODE_PR){
        if(!strcasecmp(SECTION_CAPSULING_PR_UNICAST_PREFIX, kv->key)){
            DEBUG_LOG("Match %s.\n", SECTION_CAPSULING_PR_UNICAST_PREFIX);
        
            if(config->capsuling->me6e_pr_unicast_prefix == NULL){
                config->capsuling->me6e_pr_unicast_prefix = malloc(sizeof(struct in6_addr));
                if(config->capsuling->me6e_pr_unicast_prefix == NULL){
                    me6e_logging(LOG_ERR, "fail to malloc for %s.", SECTION_CAPSULING_PR_UNICAST_PREFIX);
                    return false;
                }
            
                result = parse_ipv6address(
                    kv->value,
                    config->capsuling->me6e_pr_unicast_prefix,
                    &config->capsuling->pr_unicat_prefixlen
                    );
                if(result){
                    char   v6addr[INET6_ADDRSTRLEN] = { 0 };
                    DEBUG_LOG("PR_Prefix: %s\n",
                          inet_ntop(AF_INET6,
                          config->capsuling->me6e_pr_unicast_prefix,
                          v6addr,
                          INET6_ADDRSTRLEN)
                         );
                    DEBUG_LOG("Prefix Length: %d\n", config->capsuling->pr_unicat_prefixlen);
                
                    // ユニキャストアドレスかどうかチェック
                    result = !IN6_IS_ADDR_MULTICAST(config->capsuling->me6e_pr_unicast_prefix);

                    if(result){
                        config->capsuling->pr_unicast_prefixplaneid = malloc(sizeof(struct in6_addr));
                        if(config->capsuling->pr_unicast_prefixplaneid == NULL){
                            me6e_logging(LOG_ERR, "fail to malloc for pr_unicast_prefixplaneid.");
                            return false;
                        }
                        DEBUG_LOG("Get PR Prefix+Plane ID.\n");
                        result = me6e_pr_plane_prefix(
                                    config->capsuling->me6e_pr_unicast_prefix,
                                    config->capsuling->pr_unicat_prefixlen,
                                    config->capsuling->plane_id,
                                    config->capsuling->pr_unicast_prefixplaneid
                                    );
                    }
                }
            }
            else{
                result = false;
            }
        }
    }
    else{
        // 不明なキーなのでスキップ
        me6e_logging(LOG_WARNING, "Ignore unknown key : %s\n", kv->key);
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief カプセリング固有設定の整合性チェック関数
//!
//! カプセリング固有設定内部での整合性をチェックする
//!
//! @param [in] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  整合性OK
//! @retval false 整合性NG
///////////////////////////////////////////////////////////////////////////////
static bool config_validate_capsuling(me6e_config_t* config)
{
    // 引数チェック
    if((config == NULL) || (config->capsuling == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_validate_capsuling).");
        return false;
    }

    // 必須情報が設定されているかをチェック

    if(config->capsuling->me6e_address_prefix == NULL){
        me6e_logging(LOG_ERR, "[%s] is not found", SECTION_CAPSULING_UNICAST_PREFIX);
        return false;
    }

    if(config->capsuling->unicast_prefixlen == -1){
        me6e_logging(LOG_ERR, "[%s] is not found", SECTION_CAPSULING_UNICAST_PREFIX);
        return false;
    }

    if(config->capsuling->me6e_multicast_prefix == NULL){
        me6e_logging(LOG_ERR, "[%s] is not found", SECTION_CAPSULING_MULTICAST_PREFIX);
        return false;
    }

    if(config->capsuling->backbone_physical_dev == NULL){
        me6e_logging(LOG_ERR, "[%s] is not found", SECTION_CAPSULING_BB_PHY_DEV);
        return false;
    }

    if(config->capsuling->stub_physical_dev == NULL){
        me6e_logging(LOG_ERR, "[%s] is not found", SECTION_CAPSULING_SB_PHY_DEV);
        return false;
    }

    if(config->capsuling->tunnel_device.name == NULL){
        me6e_logging(LOG_ERR, "[%s] is not found", SECTION_CAPSULING_TUN_NAME);
        return false;
    }

    if(config->capsuling->bridge_name == NULL){
        me6e_logging(LOG_ERR, "[%s] is not found", SECTION_CAPSULING_BRG_NAME);
        return false;
    }

    if (config->capsuling->l2multi_l3uni) {
        if(me6e_list_empty(&(config->capsuling->me6e_host_address_list))){
            me6e_logging(LOG_ERR, "[%s] is not found", SECTION_CAPSULING_HOST_ADDRESS);
            return false;
        }
    }

    // トンネルモードがPRの場合、SECTION_CAPSULING_PR_UNICAST_PREFIX は必須
    if(config->common->tunnel_mode == ME6E_TUNNEL_MODE_PR){
        if(config->capsuling->me6e_pr_unicast_prefix == NULL){
            me6e_logging(LOG_ERR, "[%s] is not found", SECTION_CAPSULING_PR_UNICAST_PREFIX);
            return false;
        }
        if(config->capsuling->pr_unicat_prefixlen == -1){
            me6e_logging(LOG_ERR, "[%s] is not found", SECTION_CAPSULING_PR_UNICAST_PREFIX);
            return false;
        }
    }

    // プレフィックス長の整合性チェック
    if(config->capsuling->unicast_prefixlen > CONFIG_PLANE_PREFIX_MAX){
        me6e_logging(LOG_ERR, "me6e unicast prefix len too long.");
        return false;
    }

    if(config->capsuling->pr_unicat_prefixlen > CONFIG_PLANE_PREFIX_MAX){
        me6e_logging(LOG_ERR, "me6e pr unicast prefix len too long.");
        return false;
    }

    // Plane IDが指定されている場合、MAXと等しいプレフィックスは不正とみなす
    // (Plane IDの入る余地が無い為)
    if(config->capsuling->plane_id != NULL){
        if(config->capsuling->unicast_prefixlen == CONFIG_PLANE_PREFIX_MAX){
            me6e_logging(LOG_ERR, "me6e unicast prefix len too long.");
            return false;
        }
        else if(config->capsuling->pr_unicat_prefixlen == CONFIG_PLANE_PREFIX_MAX){
            me6e_logging(LOG_ERR,"me6e pr unicast prefix len too long.");
            return false;
        }
    }

    // backbone_physical_devの存在チェック
    // stub_physical_devの存在チェックは、ブリッジへの接続時に確認する
    if(if_nametoindex(config->capsuling->backbone_physical_dev) == 0){
        me6e_logging(LOG_ERR, "%s device is no exists.",
                config->capsuling->backbone_physical_dev);
        return false;
    }

    return true;

}


///////////////////////////////////////////////////////////////////////////////
//! @brief 代理ARP固有設定格納用構造体初期化関数
//!
//! Path 代理ARP固有設定格納用構造体を初期化する。
//!
//! @param [in,out] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool config_init_proxy_arp(me6e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_init_proxy_arp).");
        return false;;
    }

    if(config->arp != NULL){
        // 既に代理ARP固有の設定が格納済みの場合はNGを返す。
        me6e_logging(LOG_ERR, "[%s] section is already loaded. check duplicate.", SECTION_PROXY_ARP);
        return false;
    }

    config->arp = malloc(sizeof(me6e_config_proxy_arp_t));
    if(config->arp == NULL){
        me6e_logging(LOG_ERR, "fail to malloc for me6e config proxy arp.");
        return false;
    }

    // 共通設定
    config->arp->arp_enable         = true;
    config->arp->arp_entry_update   = true;
    config->arp->arp_aging_time     = CONFIG_ARP_EXPIRE_TIME_DEFAULT;
    config->arp->arp_entry_max      = -1;
    config->arp->arp_static_entry   = NULL;
    return true;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief 代理ARP固有設定格納用構造体解放関数
//!
//! 代理ARP固有設定格納用構造体を解放する。
//!
//! @param [in,out] proxy_arp   代理ARP固有設定格納用構造体へのポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void config_destruct_proxy_arp(me6e_config_proxy_arp_t* proxy_arp)
{
    // 引数チェック
    if(proxy_arp == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_destruct_proxy_arp).");
        return;
    }

    me6e_hashtable_delete(proxy_arp->arp_static_entry);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 代理ARP固有設定の解析関数
//!
//! 引数で指定されたKEY/VALUEを解析し、
//! 設定値をPath MTU Discovery関連設定構造体に格納する。
//!
//! @param [in]  kv      設定ファイルから読込んだ一行情報
//! @param [out] config  設定情報格納先の構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了(不正な値がKEY/VALUEに設定されていた場合)
///////////////////////////////////////////////////////////////////////////////
static bool config_parse_proxy_arp(
    const config_keyvalue_t*  kv,
          me6e_config_t*     config
)
{
    // ローカル変数宣言
    bool result;

    // 引数チェック
    if((kv == NULL) || (config == NULL) || (config->arp == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_parse_proxy_arp).");
        return false;
    }

    // ローカル変数初期化
    result = true;

    if(!strcasecmp(SECTION_PROXY_ARP_ENABLE, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_PROXY_ARP_ENABLE);
        result = parse_bool(kv->value, &config->arp->arp_enable);
    }
    else if(!strcasecmp(SECTION_PROXY_ARP_ENTRY_UPDATE, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_PROXY_ARP_ENTRY_UPDATE);
        result = parse_bool(kv->value, &config->arp->arp_entry_update);
    }
    else if(!strcasecmp(SECTION_PROXY_ARP_AGING_TIME, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_PROXY_ARP_AGING_TIME);
        result = parse_int(kv->value, &config->arp->arp_aging_time,
            CONFIG_ARP_EXPIRE_TIME_MIN, CONFIG_ARP_EXPIRE_TIME_MAX);
    }
    else if(!strcasecmp(SECTION_PROXY_ARP_ENTRY_MAX, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_PROXY_ARP_ENTRY_MAX);
        if(config->arp->arp_entry_max == -1){
            result = parse_int(kv->value, &config->arp->arp_entry_max,
                            CONFIG_ARP_ENTRY_MIN, CONFIG_ARP_ENTRY_MAX);
            if (result) {
                    config->arp->arp_static_entry =
                            me6e_hashtable_create(config->arp->arp_entry_max);
                    if (config->arp->arp_static_entry == NULL) {
                        result = false;
                    }
            }
        }
        else{
            result = false;
        }
    }
    else{
        struct in_addr    v4_addr;
        struct ether_addr eth_addr;
        char address[INET_ADDRSTRLEN] = { 0 };
        char macaddrstr[MAC_ADDRSTRLEN] = { 0 };
        if(parse_ipv4address(kv->key, &v4_addr, NULL) && parse_macaddress(kv->value, &eth_addr)){
            DEBUG_LOG("Match IPv4 Address && MAC Address ARP Entry add\n");
            if (config->arp->arp_static_entry != NULL) {
                result = me6e_hashtable_add(
                        config->arp->arp_static_entry,
                        inet_ntop(AF_INET, &v4_addr, address, sizeof(address)),
                        (void*)(ether_ntoa_r(&eth_addr, macaddrstr)),
                        strlen(kv->value) + 1,
                        false,
                        NULL,
                        NULL
                        );
            }
        }
        else{
            // 不明なキーなのでスキップ
            me6e_logging(LOG_WARNING, "Ignore unknown key : %s.", kv->key);
        }
    }

    return result;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief 代理ARP固有設定の整合性チェック関数
//!
//! 代理ARP固有設定内部での整合性をチェックする
//!
//! @param [in] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  整合性OK
//! @retval false 整合性NG
///////////////////////////////////////////////////////////////////////////////
static bool config_validate_proxy_arp(me6e_config_t* config)
{
    // 引数チェック
    if((config == NULL) || (config->arp == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_validate_proxy_arp).");
        return false;
    }

    // 必須情報が設定されているかをチェック
    if(config->arp->arp_entry_max == -1) {
        me6e_logging(LOG_ERR, "[%s] is not found", SECTION_PROXY_ARP_ENTRY_MAX);
        return false;
    }

    return true;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief 代理NDP固有設定格納用構造体初期化関数
//!
//! 代理NDP固有設定格納用構造体を初期化する。
//!
//! @param [in,out] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool config_init_proxy_ndp(me6e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_init_proxy_ndp).");
        return false;
    }

    if(config->ndp != NULL){
        // 既に代理NDP固有の設定が格納済みの場合はNGを返す。
        me6e_logging(LOG_ERR, "[%s] section is already loaded. check duplicate.", SECTION_PROXY_NDP);
        return false;
    }

    config->ndp = malloc(sizeof(me6e_config_proxy_ndp_t));
    if(config->ndp == NULL){
        me6e_logging(LOG_ERR, "fail to malloc for me6e config proxy ndp.");
        return false;
    }

    // 共通設定
    config->ndp->ndp_enable         = true;
    config->ndp->ndp_entry_update   = true;
    config->ndp->ndp_aging_time     = CONFIG_NDP_EXPIRE_TIME_DEFAULT;
    config->ndp->ndp_entry_max      = -1;
    config->ndp->ndp_static_entry   = NULL;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 代理NDP固有設定格納用構造体解放関数
//!
//! 代理NDP固有設定格納用構造体を解放する。
//!
//! @param [in,out] proxy_ndp   代理NDP固有設定格納用構造体へのポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void config_destruct_proxy_ndp(me6e_config_proxy_ndp_t* proxy_ndp)
{
    // 引数チェック
    if(proxy_ndp == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_destruct_proxy_ndp).");
        return;
    }

    me6e_hashtable_delete(proxy_ndp->ndp_static_entry);
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 代理NDP固有設定の解析関数
//!
//! 引数で指定されたKEY/VALUEを解析し、
//! 設定値をトンネルデバイス設定構造体に格納する。
//!
//! @param [in]  kv      設定ファイルから読込んだ一行情報
//! @param [out] config  設定情報格納先の構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了(不正な値がKEY/VALUEに設定されていた場合)
///////////////////////////////////////////////////////////////////////////////
static bool config_parse_proxy_ndp(
    const config_keyvalue_t*  kv,
          me6e_config_t*     config
)
{
    // ローカル変数宣言
    bool result;

    // 引数チェック
    if((kv == NULL) || (config == NULL) || (config->ndp == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_parse_proxy_ndp).");
        return false;
    }

    // ローカル変数初期化
    result = true;

    if(!strcasecmp(SECTION_PROXY_NDP_ENABLE, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_PROXY_NDP_ENABLE);
        result = parse_bool(kv->value, &config->ndp->ndp_enable);
    }
    else if(!strcasecmp(SECTION_PROXY_NDP_ENTRY_UPDATE, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_PROXY_NDP_ENTRY_UPDATE);
        result = parse_bool(kv->value, &config->ndp->ndp_entry_update);
    }
    else if(!strcasecmp(SECTION_PROXY_NDP_AGING_TIME, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_PROXY_NDP_AGING_TIME);
        result = parse_int(kv->value, &config->ndp->ndp_aging_time,
            CONFIG_NDP_EXPIRE_TIME_MIN, CONFIG_NDP_EXPIRE_TIME_MAX);
    }
    else if(!strcasecmp(SECTION_PROXY_NDP_ENTRY_MAX, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_PROXY_NDP_ENTRY_MAX);
        if(config->ndp->ndp_entry_max == -1){
            result = parse_int(kv->value, &config->ndp->ndp_entry_max,
                            CONFIG_NDP_ENTRY_MIN, CONFIG_NDP_ENTRY_MAX);
            if (result) {
                    config->ndp->ndp_static_entry =
                            me6e_hashtable_create(config->ndp->ndp_entry_max);
                    if (config->ndp->ndp_static_entry == NULL) {
                        result = false;
                    }
            }
        }
        else{
            result = false;
        }
    }
    else{
        struct in6_addr    v6_addr;
        struct ether_addr eth_addr;
        char address[INET6_ADDRSTRLEN] = { 0 };
        char macaddrstr[MAC_ADDRSTRLEN] = { 0 };
        if(parse_ipv6address(kv->key, &v6_addr, NULL) && parse_macaddress(kv->value, &eth_addr)){
            DEBUG_LOG("Match IPv6 Address && MAC Address NDP Entry add\n");
            if (config->ndp->ndp_static_entry != NULL) {
                result = me6e_hashtable_add(
                        config->ndp->ndp_static_entry,
                        inet_ntop(AF_INET6, &v6_addr, address, sizeof(address)),
                        (void*)(ether_ntoa_r(&eth_addr, macaddrstr)),
                        strlen(kv->value) + 1,
                        false,
                        NULL,
                        NULL
                        );
            }
        }
        else{
            // 不明なキーなのでスキップ
            me6e_logging(LOG_WARNING, "Ignore unknown key : %s.", kv->key);
        }
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 代理NDP固有設定の整合性チェック関数
//!
//! 代理NDP固有設定内部での整合性をチェックする
//!
//! @param [in] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  整合性OK
//! @retval false 整合性NG
///////////////////////////////////////////////////////////////////////////////
static bool config_validate_proxy_ndp(me6e_config_t* config)
{
    // 引数チェック
    if( (config == NULL) || (config->ndp == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_validate_proxy_ndp).");
        return false;;
    }

    // 必須情報が設定されているかをチェック
    if(config->ndp->ndp_entry_max == -1) {
        me6e_logging(LOG_ERR, "[%s] is not found", SECTION_PROXY_NDP_ENTRY_MAX);
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MAC管理固有設定格納用構造体初期化関数
//!
//! MAC管理固有設定格納用構造体を初期化する。
//!
//! @param [in,out] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool config_init_mng_macaddr(me6e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_init_mng_macaddr).");
        return false;;
    }

    if(config->mac != NULL){
        // 既にMAC管理固有の設定が格納済みの場合はNGを返す。
        me6e_logging(LOG_ERR, "[%s] section is already loaded. check duplicate.", SECTION_MNG_MACADDR);
        return false;
    }

    config->mac = malloc(sizeof(me6e_config_mng_macaddr_t));
    if(config->mac == NULL){
        me6e_logging(LOG_ERR, "fail to malloc for me6e config mng macaddr.");
        return false;
    }

        // 設定初期化
    config->mac->mng_macaddr_enable   = true;
    config->mac->mac_entry_update     = true;
    config->mac->mac_entry_max        = -1;
    config->mac->mac_vaild_lifetime   = CONFIG_MAC_EXPIRE_TIME_DEFAULT;
    me6e_list_init(&(config->mac->hosthw_addr_list));

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MAC管理固有設定格納用構造体解放関数
//!
//! MAC管理固有設定格納用構造体を解放する。
//!
//! @param [in,out] device   デバイス設定格納用構造体へのポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void config_destruct_mng_macaddr(me6e_config_mng_macaddr_t* mng_macaddr)
{

    // 引数チェック
    if(mng_macaddr == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_destruct_mng_macaddr).");
        return;
    }

    while(!me6e_list_empty(&mng_macaddr->hosthw_addr_list)){
        me6e_list* node = mng_macaddr->hosthw_addr_list.next;
        free(node->data);
        me6e_list_del(node);
        free(node);
    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MAC管理固有設定の解析関数
//!
//! 引数で指定されたKEY/VALUEを解析し、
//! 設定値をMAC管理固有設定構造体に格納する。
//!
//! @param [in]  kv      設定ファイルから読込んだ一行情報
//! @param [out] config  設定情報格納先の構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了(不正な値がKEY/VALUEに設定されていた場合)
///////////////////////////////////////////////////////////////////////////////
static bool config_parse_mng_macaddr(
    const config_keyvalue_t*  kv,
          me6e_config_t*     config
)
{
    // ローカル変数宣言
    bool result;

    // 引数チェック
    if((kv == NULL) || (config == NULL) || (config->mac == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_parse_mng_macaddr).");
        return false;
    }

    // ローカル変数初期化
    result = true;

    if(!strcasecmp(SECTION_MNG_MACADDR_ENABLE, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_MNG_MACADDR_ENABLE);
        result = parse_bool(kv->value, &config->mac->mng_macaddr_enable);
    }
    else if(!strcasecmp(SECTION_MNG_MACADDR_UPDATE, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_MNG_MACADDR_UPDATE);
        result = parse_bool(kv->value, &config->mac->mac_entry_update);
    }
    else if(!strcasecmp(SECTION_MNG_MACADDR_ENTRY_MAX, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_MNG_MACADDR_ENTRY_MAX);
        if(config->mac->mac_entry_max == -1){
            result = parse_int(kv->value, &config->mac->mac_entry_max,
                            CONFIG_MAC_ENTRY_MIN, CONFIG_MAC_ENTRY_MAX);
        }
    }
    else if(!strcasecmp(SECTION_MNG_MACADDR_VALID_TIME, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_MNG_MACADDR_VALID_TIME);
        result = parse_int(kv->value, &config->mac->mac_vaild_lifetime,
            CONFIG_MAC_EXPIRE_TIME_MIN, CONFIG_MAC_EXPIRE_TIME_MAX);
    }
    else if(!strcasecmp(SECTION_MNG_MACADDR_HOSTHW_ADDR, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_MNG_MACADDR_HOSTHW_ADDR);

        me6e_list* node = malloc(sizeof(me6e_list));
        if(node == NULL){
            me6e_logging(LOG_ERR, "fail to malloc for %s.", SECTION_MNG_MACADDR_HOSTHW_ADDR);
            return false;
        }

        struct ether_addr* addr = malloc(sizeof(struct ether_addr));
        if(addr == NULL){
            free(node);
            me6e_logging(LOG_ERR, "fail to malloc for %s.", SECTION_MNG_MACADDR_HOSTHW_ADDR);
            return false;
        }

        result = parse_macaddress(kv->value, addr);
        if (result) {
            // アドレス情報ををノードに設定して、ノードをME6Eアドレスのリストの最後に追加
            me6e_list_init(node);
            me6e_list_add_data(node, addr);
            me6e_list_add_tail(&config->mac->hosthw_addr_list, node);
        } else {
            me6e_logging(LOG_WARNING, "Ignore unknown key : %s.", kv->key);
        }

    }
    else{
        // 不明なキーなのでスキップ
        me6e_logging(LOG_WARNING, "Ignore unknown key : %s.", kv->key);
    }

    return result;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief MAC管理固有設定の整合性チェック関数
//!
//! MAC管理固有設定内部での整合性をチェックする
//!
//! @param [in] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  整合性OK
//! @retval false 整合性NG
///////////////////////////////////////////////////////////////////////////////
static bool config_validate_mng_macaddr(me6e_config_t* config)
{

    // 引数チェック
    if( (config == NULL) || (config->mac == NULL) ){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_validate_mng_macaddr).");
        return false;
    }

    // 必須情報が設定されているかをチェック
    if(config->mac->mac_entry_max == -1) {
        me6e_logging(LOG_ERR, "[%s] is not found", SECTION_MNG_MACADDR_ENTRY_MAX);
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Config 情報格納用構造体初期化関数
//!
//! ME6E-PR Config 情報格納用構造体を初期化する。
//!
//! @param [in,out] config   ME6E-PR Config 情報格納用構造体へのポインタ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool config_init_me6e_pr(me6e_config_t* config)
{
    // 引数チェック
    if(config == NULL){
        return false;
    }

    if(config->pr_conf_table == NULL){
        return false;
    }

    me6e_list* node = malloc(sizeof(me6e_list));
    if(node == NULL){
        return false;
    }

    me6e_pr_config_entry_t* pr_config_entry = malloc(sizeof(me6e_pr_config_entry_t));
    if(pr_config_entry == NULL){
        free(node);
        return false;
    }

    // ME6E-PR Config 情報
    pr_config_entry->enable = true;
    pr_config_entry->macaddr = NULL;
    pr_config_entry->pr_prefix = NULL;
    pr_config_entry->v6cidr = -1;

    // ME6E-PR Config 情報をノードに設定して、
    // ノードをME6E-PR Config Entry listの最後に追加
    me6e_list_init(node);
    me6e_list_add_data(node, pr_config_entry);
    me6e_list_add_tail(&config->pr_conf_table->entry_list, node);
    config->pr_conf_table->num++;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Config 情報格納用構造体解放関数
//!
//! ME6E-PR Config 情報格納用構造体を解放する。
//!
//! @param [in,out] pr_config_entry   ME6E-PR Config 情報格納用構造体へのポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
static void config_destruct_me6e_pr(me6e_pr_config_entry_t* pr_config_entry)
{
    // 引数チェック
    if(pr_config_entry == NULL){
        return;
    }

    // ME6E-PR Config 情報
    free(pr_config_entry->macaddr);
    free(pr_config_entry->pr_prefix);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Config 情報格納用構造体の解析関数
//!
//! 引数で指定されたKEY/VALUEを解析し、
//! 設定値をME6E-PR Config 情報格納用構造体に格納する。
//!
//! @param [in]  kv      設定ファイルから読込んだ一行情報
//! @param [out] config  設定情報格納先の構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了(不正な値がKEY/VALUEに設定されていた場合)
///////////////////////////////////////////////////////////////////////////////
static bool config_parse_me6e_pr(const config_keyvalue_t* kv, me6e_config_t* config)
{
    // ローカル変数宣言
    bool result;

    // 引数チェック
    if((kv == NULL) || (config == NULL)){
        return false;
    }

   // ME6E-PR以外のトンネルモードであれば以降の処理をスキップ
   if(config->common->tunnel_mode != ME6E_TUNNEL_MODE_PR){
        return true;
   }

    me6e_pr_config_entry_t* pr_config_entry = me6e_list_last_data(&config->pr_conf_table->entry_list);
    if(pr_config_entry == NULL){
        return false;
    }

    // ローカル変数初期化
    result = true;
    struct ether_addr* tmp_addr = NULL;

    if(!strcasecmp(SECTION_ME6E_PR_MACADDR, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_ME6E_PR_MACADDR);
        if(pr_config_entry->macaddr == NULL){
            pr_config_entry->macaddr = malloc(sizeof(struct in_addr));
            tmp_addr = malloc(sizeof(struct in_addr));
            result = parse_macaddress(kv->value, tmp_addr);

            // 同一エントリー有無検索
            if(me6e_search_pr_config_table(config->pr_conf_table, tmp_addr) != NULL) {
                me6e_logging(LOG_ERR, "This entry is already exists.");
                return false;
            }
            pr_config_entry->macaddr = tmp_addr;
        }
        else{
            result = false;
        }
    }
    else if(!strcasecmp(SECTION_ME6E_PR_PR_PREFIX, kv->key)){
        DEBUG_LOG("Match %s.\n", SECTION_ME6E_PR_PR_PREFIX);
        if(pr_config_entry->pr_prefix == NULL){
            pr_config_entry->pr_prefix = malloc(sizeof(struct in6_addr));
            result = parse_ipv6address(kv->value, pr_config_entry->pr_prefix, &pr_config_entry->v6cidr);
        }
        else{
            result = false;
        }
        if(pr_config_entry->v6cidr < 0 || pr_config_entry->v6cidr > 128){
            result = false;
        }
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Config 情報の整合性チェック関数
//!
//! ME6E-PR Config 情報の設定内部での整合性をチェックする
//!
//! @param [in] config   設定情報格納用構造体へのポインタ
//!
//! @retval true  整合性OK
//! @retval false 整合性NG
///////////////////////////////////////////////////////////////////////////////
static bool config_validate_me6e_pr(me6e_config_t* config)
{
    // 引数チェック
    if((config == NULL) || (config->pr_conf_table == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_validate_me6e_pr).");
        return false;
    }

   // ME6E-PR以外のトンネルモードであれば以降の処理をスキップ
   if(config->common->tunnel_mode != ME6E_TUNNEL_MODE_PR){
        return true;
   }

    me6e_pr_config_entry_t* pr_config_entry = me6e_list_last_data(&config->pr_conf_table->entry_list);
    if(pr_config_entry == NULL){
        return false;
    }

    // 必須情報が設定されているかをチェック
    // 以下は、必須ではないためスキップ
    /*
    if(pr_config_entry->macaddr == NULL){
        me6e_logging(LOG_ERR, "MAC address is not specified");
        return false;
    }

    if(pr_config_entry->pr_prefix == NULL){
        me6e_logging(LOG_ERR, "ME6E-PR address is not specified");
        return false;
    }

    if(pr_config_entry->v6cidr == -1){
        me6e_logging(LOG_ERR, "ME6E-PR address prefix length is invalid");
        return false;
    }
    */
    return true;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief セクション行判定関数
//!
//! 引数で指定された文字列がセクション行に該当するかどうかをチェックし、
//! セクション行の場合は、出力パラメータにセクション種別を格納する。
//!
//! @param [in]  line_str  設定ファイルから読込んだ一行文字列
//! @param [out] section   セクション種別格納先ポインタ
//!
//! @retval true  引数の文字列がセクション行である
//! @retval false 引数の文字列がセクション行でない
///////////////////////////////////////////////////////////////////////////////
static bool config_is_section(const char* line_str, config_section* section)
{
    // ローカル変数宣言
    regex_t    preg;
    size_t     nmatch = 2;
    regmatch_t pmatch[nmatch];
    bool       result;

    // 引数チェック
    if(line_str == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_is_section).");
        return false;
    }

    if (regcomp(&preg, SECTION_LINE_REGEX, REG_EXTENDED|REG_NEWLINE) != 0) {
        DEBUG_LOG("regex compile failed.\n");
        return false;
    }

    DEBUG_LOG("String = %s\n", line_str);

    if (regexec(&preg, line_str, nmatch, pmatch, 0) == REG_NOMATCH) {
        result = false;
    }
    else {
        result = true;
        // セクションのOUTパラメータが指定されている場合はセクション名チェック
        if((section != NULL) && (pmatch[1].rm_so >= 0)){
            if(!strncmp(SECTION_COMMON, &line_str[pmatch[1].rm_so], (pmatch[1].rm_eo-pmatch[1].rm_so))){
                DEBUG_LOG("Match %s.\n", SECTION_COMMON);
                *section = CONFIG_SECTION_COMMON;
            }
            else if(!strncmp(SECTION_CAPSULING, &line_str[pmatch[1].rm_so], (pmatch[1].rm_eo-pmatch[1].rm_so))){
                DEBUG_LOG("Match %s.\n", SECTION_CAPSULING);
                *section = CONFIG_SECTION_CAPSULING;
            }
            else if(!strncmp(SECTION_PROXY_ARP, &line_str[pmatch[1].rm_so], (pmatch[1].rm_eo-pmatch[1].rm_so))){
                DEBUG_LOG("Match %s.\n", SECTION_PROXY_ARP);
                *section = CONFIG_SECTION_PROXY_ARP;
            }
            else if(!strncmp(SECTION_PROXY_NDP, &line_str[pmatch[1].rm_so], (pmatch[1].rm_eo-pmatch[1].rm_so))){
                DEBUG_LOG("Match %s.\n", SECTION_PROXY_NDP);
                *section = CONFIG_SECTION_PROXY_NDP;
            }
            else if(!strncmp(SECTION_MNG_MACADDR, &line_str[pmatch[1].rm_so], (pmatch[1].rm_eo-pmatch[1].rm_so))){
                DEBUG_LOG("Match %s.\n", SECTION_MNG_MACADDR);
                *section = CONFIG_SECTION_MNG_MACADDR;
            }
            else if(!strncmp(SECTION_ME6E_PR, &line_str[pmatch[1].rm_so], (pmatch[1].rm_eo-pmatch[1].rm_so))){
                DEBUG_LOG("Match %s.\n", SECTION_ME6E_PR);
                *section = CONFIG_SECTION_ME6E_PR;
            }
            else{
                me6e_logging(LOG_ERR, "unknown section(%.*s)\n", (pmatch[1].rm_eo-pmatch[1].rm_so), &line_str[pmatch[1].rm_so]);
                *section = CONFIG_SECTION_UNKNOWN;
            }
        }
    }

    regfree(&preg);

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief KEY/VALUE行判定関数
//!
//! 引数で指定された文字列がKEY/VALUE行に該当するかどうかをチェックし、
//! KEY/VALUE行の場合は、出力パラメータにKEY/VALUE値を格納する。
//!
//! @param [in]  line_str  設定ファイルから読込んだ一行文字列
//! @param [out] kv        KEY/VALUE値格納先ポインタ
//!
//! @retval true  引数の文字列がKEY/VALUE行である
//! @retval false 引数の文字列がKEY/VALUE行でない
///////////////////////////////////////////////////////////////////////////////
static bool config_is_keyvalue(const char* line_str, config_keyvalue_t* kv)
{
    // ローカル変数宣言
    regex_t    preg;
    size_t     nmatch = 3;
    regmatch_t pmatch[nmatch];
    bool       result;

    // 引数チェック
    if((line_str == NULL) || (kv == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(config_is_keyvalue).");
        return false;
    }

    // ローカル変数初期化
    result = true;

    if (regcomp(&preg, KV_REGEX, REG_EXTENDED|REG_NEWLINE) != 0) {
        DEBUG_LOG("regex compile failed.\n");
        return false;
    }

    if (regexec(&preg, line_str, nmatch, pmatch, 0) == REG_NOMATCH) {
        DEBUG_LOG("No match.\n");
        result = false;
    }
    else {
        sprintf(kv->key,   "%.*s", (pmatch[1].rm_eo-pmatch[1].rm_so), &line_str[pmatch[1].rm_so]);
        sprintf(kv->value, "%.*s", (pmatch[2].rm_eo-pmatch[2].rm_so), &line_str[pmatch[2].rm_so]);
        DEBUG_LOG("Match. key=\"%s\", value=\"%s\"\n", kv->key, kv->value);
    }

    regfree(&preg);

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 文字列をbool値に変換する
//!
//! 引数で指定された文字列がyes or noの場合に、yesならばtrueに、
//! noならばfalseに変換して出力パラメータに格納する。
//!
//! @param [in]  str     変換対象の文字列
//! @param [out] output  変換結果の出力先ポインタ
//!
//! @retval true  変換成功
//! @retval false 変換失敗 (引数の文字列がbool値でない)
///////////////////////////////////////////////////////////////////////////////
static bool parse_bool(const char* str, bool* output)
{
    // ローカル変数定義
    bool result;

    // 引数チェック
    if((str == NULL) || (output == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(parse_bool).");
        return false;
    }

    // ローカル変数初期化
    result = true;

    if(!strcasecmp(str, CONFIG_BOOL_TRUE)){
        *output = true;
    }
    else if(!strcasecmp(str, CONFIG_BOOL_FALSE)){
        *output = false;
    }
    else{
        result = false;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 文字列を整数値に変換する
//!
//! 引数で指定された文字列が整数値で、且つ最小値と最大値の範囲に
//! 収まっている場合に、数値型に変換して出力パラメータに格納する。
//!
//! @param [in]  str     変換対象の文字列
//! @param [out] output  変換結果の出力先ポインタ
//! @param [in]  min     変換後の数値が許容する最小値
//! @param [in]  max     変換後の数値が許容する最大値
//!
//! @retval true  変換成功
//! @retval false 変換失敗 (引数の文字列が整数値でない)
///////////////////////////////////////////////////////////////////////////////
static bool parse_int(const char* str, int* output, const int min, const int max)
{
    // ローカル変数定義
    bool  result;
    int   tmp;
    char* endptr;

    // 引数チェック
    if((str == NULL) || (output == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(parse_int).");
        return false;
    }

    // ローカル変数初期化
    result = true;
    tmp    = 0;
    endptr = NULL;

    tmp = strtol(str, &endptr, 10);

    if((tmp == LONG_MIN || tmp == LONG_MAX) && (errno != 0)){
        // strtol内でエラーがあったのでエラー
        result = false;
    }
    else if(endptr == str){
        // strtol内でエラーがあったのでエラー
        result = false;
    }
    else if(tmp > max){
        // 最大値よりも大きいのでエラー
        result = false;
    }
    else if(tmp < min) {
        // 最小値よりも小さいのでエラー
        result = false;
    }
    else if (*endptr != '\0') {
        // 最終ポインタが終端文字でない(=文字列の途中で変換が終了した)のでエラー
        result = false;
    }
    else {
        // ここまでくれば正常に変換できたので、出力変数に格納
        *output = tmp;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 文字列をIPv4アドレス型に変換する
//!
//! 引数で指定された文字列がIPv4アドレスのフォーマットの場合に、
//! IPv4アドレス型に変換して出力パラメータに格納する。
//!
//! @param [in]  str        変換対象の文字列
//! @param [out] output     変換結果の出力先ポインタ
//! @param [out] prefixlen  プレフィックス長出力先ポインタ
//!
//! @retval true  変換成功
//! @retval false 変換失敗 (引数の文字列がIPv4アドレス形式でない)
///////////////////////////////////////////////////////////////////////////////
bool parse_ipv4address(const char* str, struct in_addr* output, int* prefixlen)
{
    // ローカル変数定義
    bool  result;
    char* tmp;
    char* token;

    // 引数チェック
    if((str == NULL) || (output == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(parse_ipv4address).");
        return false;
    }

    // ローカル変数初期化
    result = true;
    tmp    = strdup(str);

    if(tmp == NULL){
        return false;
    }

    token = strtok(tmp, "/");
    if(tmp){
        if(inet_pton(AF_INET, token, output) <= 0){
            result = false;
        }
        else{
            result = true;
        }
    }

    if(result && (prefixlen != NULL)){
        token = strtok(NULL, "/");
        if(token == NULL){
            *prefixlen = 0;
        }
        else{
            result = parse_int(token, prefixlen, CONFIG_IPV4_NETMASK_MIN, CONFIG_IPV4_NETMASK_MAX);
        }
    }

    free(tmp);

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 文字列をIPv6アドレス型に変換する
//!
//! 引数で指定された文字列がIPv6アドレスのフォーマットの場合に、
//! IPv6アドレス型に変換して出力パラメータに格納する。
//!
//! @param [in]  str        変換対象の文字列
//! @param [out] output     変換結果の出力先ポインタ
//! @param [out] prefixlen  プレフィックス長出力先ポインタ
//!
//! @retval true  変換成功
//! @retval false 変換失敗 (引数の文字列がIPv6アドレス形式でない)
///////////////////////////////////////////////////////////////////////////////
bool parse_ipv6address(const char* str, struct in6_addr* output, int* prefixlen)
{
    // ローカル変数定義
    bool  result;
    char* tmp;
    char* token;


    // 引数チェック
    if((str == NULL) || (output == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(parse_ipv6address).");
        return false;
    }

    // ローカル変数初期化
    result = true;
    tmp    = strdup(str);

    if(tmp == NULL){
        return false;
    }

    token = strtok(tmp, "/");
    if(tmp){
        if(inet_pton(AF_INET6, token, output) <= 0){
            result = false;
        }
        else{
            result = true;
        }
    }

    if(result && (prefixlen != NULL)){
        token = strtok(NULL, "/");
        if(token == NULL){
            *prefixlen = 0;
        }
        else{
            result = parse_int(token, prefixlen, CONFIG_IPV6_PREFIX_MIN, CONFIG_IPV6_PREFIX_MAX);
        }
    }

    free(tmp);

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 文字列をMACアドレス型に変換する
//!
//! 引数で指定された文字列がMACアドレスのフォーマットの場合に、
//! MACアドレス型に変換して出力パラメータに格納する。
//!
//! @param [in]  str        変換対象の文字列
//! @param [out] output     変換結果の出力先ポインタ
//!
//! @retval true  変換成功
//! @retval false 変換失敗 (引数の文字列がMACアドレス形式でない)
///////////////////////////////////////////////////////////////////////////////
bool parse_macaddress(const char* str, struct ether_addr* output)
{
    // ローカル変数定義
    bool result;

    // 引数チェック
    if((str == NULL) || (output == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(parse_macaddress).");
        return false;
    }

    if(ether_aton_r(str, output) == NULL){
        result = false;
    }
    else{
        result = true;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Config テーブル検索関数
//!
//! ME6E-PR Configテーブルから検索MACアドレスと同一のエントリを検索する。
//!
//! @param [in] table     ME6E-PR Config Table
//! @param [in] addr      MAC アドレス
//!
//! @return me6e_pr_config_entry_tアドレス 検索成功
//!                                        (マッチした ME6E-PR Config Entry情報)
//! @return NULL                           検索失敗
//////////////////////////////////////////////////////////////////////////////
me6e_pr_config_entry_t* me6e_search_pr_config_table(
        me6e_pr_config_table_t* table,
        struct ether_addr* addr
)
{
    me6e_pr_config_entry_t* entry = NULL;

    // 引数チェック
    if((table == NULL) || (addr == NULL)){
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_search_pr_config_table).");
        return NULL;
    }

    if(table->num > 0){
        me6e_list* iter;
        me6e_list_for_each(iter, &table->entry_list){
            me6e_pr_config_entry_t* tmp = iter->data;

            // MACアドレスが一致するエントリーを検索
            if(addr == tmp->macaddr){
                entry = tmp;

                char macaddrstr[MAC_ADDRSTRLEN] = { 0 };
                DEBUG_LOG("Match ME6E-PR Table address = %s\n",
                    ether_ntoa_r(tmp->macaddr, macaddrstr));

                break;
            }
        }
    }else{
        DEBUG_LOG("ME6E-PR table is no-entry. num = %d\n", table->num);
    }

    return entry;

}

