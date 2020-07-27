/******************************************************************************/
/* ファイル名 : me6eapp_pr.c                                                  */
/* 機能概要   : ME6E Prefix Resolution ソースファイル                         */
/* 修正履歴   : 2016.06.03 S.Anai PR機能追加                                  */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <limits.h>

#include "me6eapp.h"
#include "me6eapp_list.h"
#include "me6eapp_pr.h"
#include "me6eapp_log.h"
#include "me6eapp_command.h"
#include "me6eapp_pr_struct.h"
#include "me6eapp_network.h"

// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

////////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Table生成
//!
//! ME6E-PR config情報からME6E-PR Tableの生成を行う。
//!
//! @param [in]  handler  ME6Eハンドラ
//!
//! @return 生成したME6E-PR Tableクラスへのポインタ
///////////////////////////////////////////////////////////////////////////////
me6e_pr_table_t* me6e_pr_init_pr_table(struct me6e_handler_t* handler)
{
    // 引数チェック
    if(handler == NULL){
        me6e_logging(LOG_ERR, "Parameter Check NG.");
        return NULL;
    }

    if(handler->conf == NULL){
        return NULL;
    }

    if(handler->conf->pr_conf_table == NULL){
        return NULL;
    }

    me6e_pr_table_t* pr_table = NULL;
    pr_table = malloc(sizeof(me6e_pr_table_t));
    if(pr_table == NULL){
        me6e_logging(LOG_WARNING, "fail to allocate ME6E-PR data.\n");
        return NULL;
    }

    me6e_list_init(&pr_table->entry_list);

    pr_table->num = 0;

    // 排他制御初期化
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init(&pr_table->mutex, &attr);

    // config情報TableからME6E-PR Tableへエントリーを追加
    me6e_list* iter;
    me6e_pr_entry_t* pr_entry;
    me6e_list_for_each(iter, &handler->conf->pr_conf_table->entry_list){
        me6e_pr_config_entry_t* pr_config_entry = iter->data;
        pr_entry = me6e_pr_conf2entry(handler, pr_config_entry);
        if(pr_entry != NULL){
            if(!me6e_pr_add_entry(pr_table, pr_entry)){
                return NULL;
            }
        }else{
            me6e_logging(LOG_ERR, "pr_entry is NULL.\n");
        }
    }

    return pr_table;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR情報管理(ME6E-PR Table)終了関数
//!
//! テーブルの解放を行う。
//!
//! @param [in]  pr_handler ME6E-PR情報管理
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void me6e_pr_destruct_pr_table(me6e_pr_table_t* pr_handler)
{
    // pr_handlerのNULLチェックは行わない。

    // 排他開始
    pthread_mutex_lock(&pr_handler->mutex);

    // ME6E-PR Entry削除
    while(!me6e_list_empty(&pr_handler->entry_list)){
        me6e_list* node = pr_handler->entry_list.next;
        me6e_pr_entry_t* pr_entry = node->data;
        free(pr_entry);
        me6e_list_del(node);
        free(node);
        pr_handler->num--;
    }

    // 排他解除
    pthread_mutex_unlock(&pr_handler->mutex);

    // 排他制御終了
    pthread_mutex_destroy(&pr_handler->mutex);

    free(pr_handler);

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Config Table出力関数（デバッグ用）
//!
//! ME6E-PR Config Tableを出力する。
//!
//! @param [in] table   出力するME6E-PR Config Table
//!
//! @return none
///////////////////////////////////////////////////////////////////////////////
void me6e_pr_config_table_dump(const me6e_pr_config_table_t* table)
{
    char macaddrstr[MAC_ADDRSTRLEN] = { 0 };
    char address[INET6_ADDRSTRLEN];
    char* strbool[] = { "disable", "enable"};

    // 引数チェック
    if (table == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_pr_config_table_dump).");
        return;
    }

    DEBUG_LOG("table num = %d", table->num);

    me6e_list* iter;
    me6e_list_for_each(iter, &table->entry_list){
        me6e_pr_config_entry_t* entry = iter->data;
        DEBUG_LOG("%s v4address = %s ",
                strbool[entry->enable],
                ether_ntoa_r(entry->macaddr, macaddrstr));
        DEBUG_LOG("v6address = %s/%d\n",
                inet_ntop(AF_INET6, entry->pr_prefix, address, sizeof(address)), entry->v6cidr);

    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Table出力関数（デバッグ用）
//!
//! ME6E-PR Tableを出力する。
//!
//! @param [in] table   出力するME6E-PR Table
//!
//! @return none
///////////////////////////////////////////////////////////////////////////////
void me6e_pr_table_dump(const me6e_pr_table_t* table)
{
    char macaddrstr[MAC_ADDRSTRLEN] = { 0 };
    char address[INET6_ADDRSTRLEN];
    char* strbool[] = { "disable", "enable"};

    // 引数チェック
    if (table == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_pr_table_dump).");
        return;
    }

    DEBUG_LOG("table num = %d", table->num);

    me6e_list* iter;
    me6e_list_for_each(iter, &table->entry_list){
        me6e_pr_entry_t* entry = iter->data;
        DEBUG_LOG("%s MAC4address = %s",
                strbool[entry->enable],
                ether_ntoa_r(&entry->macaddr, macaddrstr));
        DEBUG_LOG("pr_prefix = %s/%d",
                inet_ntop(AF_INET6, &entry->pr_prefix, address, sizeof(address)), entry->v6cidr);
        DEBUG_LOG("pr_prefix+PlaneID = %s\n",
                inet_ntop(AF_INET6, &entry->pr_prefix_planeid, address, sizeof(address)));

    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Table追加関数
//!
//! ME6E-PR Tableへ新規エントリーを追加する。
//! 新たなエントリーは、リストの最後尾に追加する。
//! 本関数内でME6E-PR Tableへアクセスするための排他の獲得と解放を行う。
//! ※entryは、ヒープ領域(malloc関数などで確保したメモリ領域)を渡すこと。
//! MACアドレス形式かどうかのチェックはコマンドアプリ側で実施のため、
//! ここでの形式チェックは行わない。 
//!
//! @param [in/out] table   追加するME6E-PR Table
//! @param [in]     entry   追加するエントリー情報
//!
//! @return true        追加成功
//!         false       追加失敗
///////////////////////////////////////////////////////////////////////////////
bool me6e_pr_add_entry(me6e_pr_table_t* table, me6e_pr_entry_t* entry)
{
    // 引数チェック
    if ((table == NULL) || (entry == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_pr_add_entry).");
        return false;
    }

    // 同一エントリー有無検索
    if(me6e_search_pr_table(table, &entry->macaddr) != NULL) {
        me6e_logging(LOG_ERR, "This entry is already exists(me6e_pr_add_entry).");
        return false;
    }

    if (table->num < PR_MAX_ENTRY_NUM) {
        // 排他開始
        DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
        pthread_mutex_lock(&table->mutex);

        // 要素数のインクリメント
        table->num++;

        me6e_list* node = malloc(sizeof(me6e_list));
        if(node == NULL){
            me6e_logging(LOG_WARNING, "fail to allocate ME6E-PR data.\n");
            return false;
        }

        me6e_list_init(node);
        me6e_list_add_data(node, entry);
        
        // リストの最後尾に追加
        me6e_list_add_tail(&table->entry_list, node);

        // 排他解除
        pthread_mutex_unlock(&table->mutex);
        DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    } else {
        me6e_logging(LOG_INFO, "ME6E-PR table is enough. num = %d\n", table->num);
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Table削除関数
//!
//! ME6E-PR Tableから指定されたエントリーを削除する。
//! ※エントリーが1個の場合、削除は失敗する。
//! 本関数内でME6E-PR Tableへアクセスするための排他の獲得と解放を行う。
//!
//! @param [in/out] table   削除するME6E-PR Table
//! @param [in]     addr    検索に使用するmac address
//!
//! @return true        削除成功
//!         false       削除失敗
///////////////////////////////////////////////////////////////////////////////
bool me6e_pr_del_entry(me6e_pr_table_t* table, struct ether_addr* addr)
{
    // 引数チェック
    if ((table == NULL) || (addr == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_pr_del_entry).");
        return false;
    }

    if (table->num > 1) {
        // 排他開始
        DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
        pthread_mutex_lock(&table->mutex);

        me6e_list* iter;
        me6e_list_for_each(iter, &table->entry_list){
            me6e_pr_entry_t* tmp = iter->data;

            // MACアドレスが一致するエントリーを検索
            if ((addr->ether_addr_octet[0] == tmp->macaddr.ether_addr_octet[0]) &&
                (addr->ether_addr_octet[1] == tmp->macaddr.ether_addr_octet[1]) &&
                (addr->ether_addr_octet[2] == tmp->macaddr.ether_addr_octet[2]) &&
                (addr->ether_addr_octet[3] == tmp->macaddr.ether_addr_octet[3]) &&
                (addr->ether_addr_octet[4] == tmp->macaddr.ether_addr_octet[4]) &&
                (addr->ether_addr_octet[5] == tmp->macaddr.ether_addr_octet[5]) ) { 

                // 一致したエントリーを削除
                free(tmp);
                me6e_list_del(iter);
                free(iter);

                // 要素数のディクリメント
                table->num--;

                break;
            }
        }

        // 排他解除
        pthread_mutex_unlock(&table->mutex);
        DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

        // 検索に失敗した場合、ログを残す
        if (iter == &table->entry_list) {
            char macaddrstr[MAC_ADDRSTRLEN];
            me6e_logging(LOG_INFO, "Don't match ME6E-PR Table. mac address = %s\n",
                    ether_ntoa_r((struct ether_addr *)&addr,macaddrstr));
            return false;
        }

    } else if (table->num == 1) {
        //エントリーの残数が1の場合は、エラーログ出力しfalseとする
        me6e_logging(LOG_INFO, "ME6E-PR table is only one entry.\n");
        return false;
    } else {
        me6e_logging(LOG_INFO, "ME6E-PR table is no-entry. num = %d\n", table->num);
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Table 有効/無効設定関数
//!
//! ME6E-PR Tableから指定されたエントリーの有効/無効を設定する。
//! 本関数内でME6E-PR Tableへアクセスするための排他の獲得と解放を行う。
//!
//! @param [in/out] table   設定するME6E-PR Table
//! @param [in]     addr    検索に使用するmac address
//! @param [in]     enable  有効/無効
//!
//! @return true        設定成功
//!         false       設定失敗
///////////////////////////////////////////////////////////////////////////////
bool me6e_pr_set_enable(me6e_pr_table_t* table, struct ether_addr* addr, bool enable)
{
    // 引数チェック
    if ((table == NULL) || (addr == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_pr_set_enable).");
        return false;
    }

    if (table->num > 0) {
        // 排他開始
        DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
        pthread_mutex_lock(&table->mutex);

        me6e_list* iter;
        me6e_list_for_each(iter, &table->entry_list){
            me6e_pr_entry_t* tmp = iter->data;

            // MACアドレスが一致するエントリーを検索
            if ((addr->ether_addr_octet[0] == tmp->macaddr.ether_addr_octet[0]) &&
                (addr->ether_addr_octet[1] == tmp->macaddr.ether_addr_octet[1]) &&
                (addr->ether_addr_octet[2] == tmp->macaddr.ether_addr_octet[2]) &&
                (addr->ether_addr_octet[3] == tmp->macaddr.ether_addr_octet[3]) &&
                (addr->ether_addr_octet[4] == tmp->macaddr.ether_addr_octet[4]) &&
                (addr->ether_addr_octet[5] == tmp->macaddr.ether_addr_octet[5]) ) {

                // 一致したエントリーの有効/無効フラグを変更
                tmp->enable = enable;

                break;
            }
        }

        // 排他解除
        pthread_mutex_unlock(&table->mutex);
        DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

        // 検索に失敗した場合、ログを残す
        if (iter == &table->entry_list) {
            char macaddrstr[MAC_ADDRSTRLEN];
            me6e_logging(LOG_INFO, "Don't match ME6E-PR Table. mac address = %s\n",
                    ether_ntoa_r((struct ether_addr *)&addr,macaddrstr));
            return false;
        }

    } else {
        //PRテーブルにエントリーなし
        me6e_logging(LOG_INFO, "ME6E-PR table is no-entry. num = %d\n", table->num);
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR拡張 ME6E-PR テーブル検索関数
//!
//! macアドレスから、同一エントリーを検索する。
//! 本関数内でME6E-PR Tableへアクセスするための排他の獲得と解放を行う。
//!
//! @param [in] table   検索するME6E-PRテーブル
//! @param [in] addr    検索するmacアドレス
//!
//! @return me6e_pr_entry_tアドレス    検索成功(マッチした ME6E-PR Entry情報)
//! @return NULL                        検索失敗
///////////////////////////////////////////////////////////////////////////////
me6e_pr_entry_t* me6e_search_pr_table(me6e_pr_table_t* table, struct ether_addr* addr)
{
    me6e_pr_entry_t* entry = NULL;

    // 引数チェック
    if ((table == NULL) || (addr == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_search_pr_table).");
        return NULL;
    }

    if (table->num > 0) {
        // 排他開始
        DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
        pthread_mutex_lock(&table->mutex);

        me6e_list* iter;
        me6e_list_for_each(iter, &table->entry_list){
            me6e_pr_entry_t* tmp = iter->data;

            // MACアドレスが一致するエントリーを検索
            if ( addr->ether_addr_octet[0] == tmp->macaddr.ether_addr_octet[0] &&
                 addr->ether_addr_octet[1] == tmp->macaddr.ether_addr_octet[1] &&
                 addr->ether_addr_octet[2] == tmp->macaddr.ether_addr_octet[2] &&
                 addr->ether_addr_octet[3] == tmp->macaddr.ether_addr_octet[3] &&
                 addr->ether_addr_octet[4] == tmp->macaddr.ether_addr_octet[4] &&
                 addr->ether_addr_octet[5] == tmp->macaddr.ether_addr_octet[5] ) {

                entry = tmp;

                char macaddrstr[MAC_ADDRSTRLEN];
                DEBUG_LOG("Match ME6E-PR Table address = %s\n",
                         ether_ntoa_r((struct ether_addr *)addr, macaddrstr));

                break;
            }
        }

        // 排他解除
        pthread_mutex_unlock(&table->mutex);
        DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    } else {
        DEBUG_LOG("ME6E-PR table is no-entry. num = %d\n", table->num);
    }

    return entry;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR plefix+PlaneID生成関数
//!
//! ME6E-PR prefix と PlaneID の値を元に
//! ME6E-PR prefix + PlaneID のアドレスを生成する。
//!
//! @param [in]     inaddr      ME6E-PR prefix
//! @param [in]     cidr        ME6E-PR prefix長
//! @param [in]     plane_id    Plane ID(NULL許容)
//! @param [out]    outaddr     ME6E-PR plefix+PlaneID 
//!
//! @retval true    正常終了
//! @retval false   異常終了
///////////////////////////////////////////////////////////////////////////////
bool me6e_pr_plane_prefix(
        struct in6_addr* inaddr,
        int cidr,
        char* plane_id,
        struct in6_addr* outaddr
)
{
    uint8_t*        src_addr;
    uint8_t*        dst_addr;
    char address[INET6_ADDRSTRLEN] = { 0 };

    // 引数チェック
    if ((inaddr == NULL) || (outaddr == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_pr_plane_prefix).");
        return false;
    }

    if(plane_id != NULL){
        // Plane IDが指定されている場合は、Plane IDで初期化する。
        strcat(address, "::");
        strcat(address, plane_id);
        strcat(address, ":0:0:0");

        // 値の妥当性は設定読み込み時にチェックしているので戻り値は見ない
        inet_pton(AF_INET6, address, outaddr);
    }
    else{
        // Plane IDが指定されていない場合はALL0で初期化する
        *outaddr = in6addr_any;
    }

    // plefix length分コピーする
    src_addr  = inaddr->s6_addr;
    dst_addr  = outaddr->s6_addr;

    for(int i = 0; (i < 16 && cidr > 0); i++, cidr-=CHAR_BIT){
        if(cidr >= CHAR_BIT){
            dst_addr[i] = src_addr[i];
        }
        else{
            dst_addr[i] = (src_addr[i] & (0xff << cidr)) | (dst_addr[i] & ~(0xff << cidr));
            break;
        }
    }

    DEBUG_LOG("pr_prefix + Plane ID = %s\n",
            inet_ntop(AF_INET6, outaddr, address, sizeof(address)));

    return true;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Entry 構造体変換関数
//!
//! ME6E-PR config情報構造体をME6E-PR Entry 構造体へ変換する。
//! ※変換成功時に受け取ったme6e_pr_entry_tのアドレスは、
//! ※free関数で解放すること。
//!
//! @param [in]  handler    アプリケーションハンドラー
//!        [in]  conf       ME6E-PR config情報
//!
//! @return me6e_pr_entry_tアドレス    変換処理正常終了
//! @return NULL                        変換処理異常終了
///////////////////////////////////////////////////////////////////////////////
me6e_pr_entry_t* me6e_pr_conf2entry(
        struct me6e_handler_t* handler,
        me6e_pr_config_entry_t* conf
)
{
    me6e_pr_entry_t* entry = NULL;

    // 引数チェック
    if ((handler == NULL) || (conf == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_pr_conf2entry).");
        return NULL;
    }

    // macaddr pr_prefixは省略可能なため、指定されている場合にのみ変換を実施
    if((conf->macaddr != NULL) && (conf->pr_prefix != NULL)){

        char macaddrstr[MAC_ADDRSTRLEN];
        char address[INET_ADDRSTRLEN];
        DEBUG_LOG("macaddr = %s\n", ether_ntoa_r(conf->macaddr, macaddrstr));
        DEBUG_LOG("pr_prefix = %s\n",inet_ntop(AF_INET6, conf->pr_prefix, address, sizeof(address)));

        entry = malloc(sizeof(me6e_pr_entry_t));
        if(entry == NULL){
            me6e_logging(LOG_WARNING, "fail to allocate ME6E-PR data.\n");
            return NULL;
        }

        // エントリーか有効/無効かを表すフラグ
        entry->enable = conf->enable;

        // IPv4アドレス
        memcpy(&entry->macaddr, conf->macaddr, sizeof(struct ether_addr));

        // ME6E-PR address prefix用のIPv6アドレス+Plane ID
        bool ret = me6e_pr_plane_prefix(
                conf->pr_prefix,
                conf->v6cidr,
                handler->conf->capsuling->plane_id,
                &entry->pr_prefix_planeid);

        if(!ret) {
            me6e_logging(LOG_WARNING, "fail to create ME6E-PR plefix+PlaneID.\n");
            free(entry);
            return NULL;
        }

        // ME6E-PR address prefix用のIPv6アドレス(表示用)
        memcpy(&entry->pr_prefix, conf->pr_prefix, sizeof(struct in6_addr));

        // ME6E-PR address prefixのサブネットマスク長(表示用)
        entry->v6cidr =  128;

    }
    
    return entry;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR拡張 テーブル検索関数(Stub側)
//!
//! 送信先MACアドレスから、送信先ME6E-PR Prefixを検索する。
//! 本関数内でME6E-PR Tableへアクセスするための排他の獲得と解放を行う。
//!
//! @param [in] table   検索するME6E-PRテーブル
//! @param [in] addr    検索するMACアドレス
//!
//! @return me6e_pr_entry_tアドレス    検索成功(マッチした ME6E-PR Entry情報)
//! @return NULL                        検索失敗
///////////////////////////////////////////////////////////////////////////////
me6e_pr_entry_t* me6e_pr_entry_search_stub(
        me6e_pr_table_t* table,
        struct ether_addr* addr
)
{
    me6e_pr_entry_t* entry = NULL;

    // 引数チェック
    if ((table == NULL) || (addr == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_pr_entry_search_stub).");
        return NULL;
    }

    if (table->num > 0) {
        // 排他開始
        DEBUG_LOG("pthread_mutex_lock  TID  = %x\n",  pthread_self());
        pthread_mutex_lock(&table->mutex);

        me6e_list* iter;
        me6e_list_for_each(iter, &table->entry_list){
        me6e_pr_entry_t* tmp = iter->data;

            // disableはスキップ
            if (!tmp->enable) {
                continue;
            }

            // MACアドレスが一致するエントリーを検索
            if ((addr->ether_addr_octet[0] == tmp->macaddr.ether_addr_octet[0]) &&
                (addr->ether_addr_octet[1] == tmp->macaddr.ether_addr_octet[1]) &&
                (addr->ether_addr_octet[2] == tmp->macaddr.ether_addr_octet[2]) &&
                (addr->ether_addr_octet[3] == tmp->macaddr.ether_addr_octet[3]) &&
                (addr->ether_addr_octet[4] == tmp->macaddr.ether_addr_octet[4]) &&
                (addr->ether_addr_octet[5] == tmp->macaddr.ether_addr_octet[5]) ) {

                entry = tmp;

                char macaddrstr[MAC_ADDRSTRLEN];
                char address[INET_ADDRSTRLEN];
                DEBUG_LOG("Match ME6E-PR Table address = %s\n",
                        ether_ntoa_r(&tmp->macaddr, macaddrstr));
                DEBUG_LOG("dest address = %s\n",
                        inet_ntop(AF_INET6, &tmp->pr_prefix, address, sizeof(address)));

                break;
            }
        }

        // 排他解除
        pthread_mutex_unlock(&table->mutex);
        DEBUG_LOG("pthread_mutex_unlock  TID  = %x\n",  pthread_self());

    } else {
        DEBUG_LOG("ME6E-PR table is no-entry. num = %d\n", table->num);
    }

    return entry;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Entry 構造体変換関数(コマンド用)
//!
//! ME6E-PR Commandデータ構造体をME6E-PR Entry 構造体へ変換する。
//! ※変換成功時に受け取ったme6e_pr_entry_tのアドレスは、
//! ※free関数で解放すること。
//!
//! @param [in]  handler    アプリケーションハンドラー
//!        [in]  data       ME6E-PR Commandデータ
//!
//! @return me6e_pr_entry_tアドレス    変換処理正常終了
//! @return NULL                        変換処理異常終了
///////////////////////////////////////////////////////////////////////////////
me6e_pr_entry_t* me6e_pr_command2entry(
        struct me6e_handler_t* handler,
        struct me6e_command_pr_data* data
)
{
    me6e_pr_entry_t* entry = NULL;

    // 引数チェック
    if ((handler == NULL) || (data == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_pr_command2entry).");
        return NULL;
    }

    entry = malloc(sizeof(me6e_pr_entry_t));
    if(entry == NULL){
        me6e_logging(LOG_WARNING, "fail to allocate ME6E-PR data.\n");
        return NULL;
    }

    // エントリーか有効/無効かを表すフラグ
    entry->enable = data->enable;

    // MACアドレス
    entry->macaddr = data->macaddr;

    // ME6E-PR address prefix用のIPv6アドレス+Plane ID
    bool ret = me6e_pr_plane_prefix(
            &data->pr_prefix,
            data->v6cidr,
            handler->conf->capsuling->plane_id,
            &entry->pr_prefix_planeid);

    if(!ret) {
        me6e_logging(LOG_WARNING, "fail to create ME6E-PR plefix+PlaneID.\n");
        free(entry);
        return NULL;
    }

    // ME6E-PR address prefix用のIPv6アドレス(表示用)
    memcpy(&entry->pr_prefix, &data->pr_prefix, sizeof(struct in6_addr));

    entry->v6cidr =  128;

    return entry;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Config Entry 構造体変換関数(コマンド用)
//!
//! ME6E-PR Commandデータ構造体をME6E-PR Config 構造体へ変換する。
//! ※変換成功時に受け取ったme6e_pr_config_entry_tのアドレスは、
//! ※free関数で解放すること。
//!
//! @param [in]  handler    アプリケーションハンドラー
//!        [in]  data       ME6E-PR Commandデータ
//!
//! @return me6e_pr_config_entry_tアドレス 変換処理正常終了
//! @return NULL                            変換処理異常終了
///////////////////////////////////////////////////////////////////////////////
me6e_pr_config_entry_t* me6e_pr_command2conf(
        struct me6e_handler_t* handler,
        struct me6e_command_pr_data* data
)
{
    me6e_pr_config_entry_t* entry = NULL;

    // 引数チェック
    if ((handler == NULL) || (data == NULL)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_pr_command2conf).");
        return NULL;
    }

    entry = malloc(sizeof(me6e_pr_config_entry_t));
    if(entry == NULL){
        me6e_logging(LOG_WARNING, "fail to allocate ME6E-PR data.\n");
        return NULL;
    }

    entry->macaddr = malloc(sizeof(struct ether_addr));
    if(entry == NULL){
        me6e_logging(LOG_WARNING, "fail to allocate ME6E-PR data.\n");
        return NULL;
    }

    entry->pr_prefix = malloc(sizeof(struct in6_addr));
    if(entry == NULL){
        me6e_logging(LOG_WARNING, "fail to allocate ME6E-PR data.\n");
        return NULL;
    }

    // エントリーか有効/無効かを表すフラグ
    entry->enable = data->enable;

    // MACアドレス
    entry->macaddr->ether_addr_octet[0] = data->macaddr.ether_addr_octet[0];
    entry->macaddr->ether_addr_octet[1] = data->macaddr.ether_addr_octet[1];
    entry->macaddr->ether_addr_octet[2] = data->macaddr.ether_addr_octet[2];
    entry->macaddr->ether_addr_octet[3] = data->macaddr.ether_addr_octet[3];
    entry->macaddr->ether_addr_octet[4] = data->macaddr.ether_addr_octet[4];
    entry->macaddr->ether_addr_octet[5] = data->macaddr.ether_addr_octet[5];

    // ME6E-PR address prefix用のIPv6アドレス
    memcpy(entry->pr_prefix, &data->pr_prefix, sizeof(struct in6_addr));

    // ME6E-PR address prefixのサブネットマスク長
    entry->v6cidr =  data->v6cidr;

    return entry;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Entry追加コマンド契機ME6E-PRテーブル追加関数
//!
//! 1.コマンドデータ形式からME6E-PR Entry形式に変換する。
//! 2.ME6E-PR TableにME6E-PR Entryを登録する。
//!
//! @param [in]     handler    ME6Eハンドラ
//! @param [in]     req        コマンド要求データ
//!
//! @return true        OK(追加成功)
//!         false       NG(追加失敗)
///////////////////////////////////////////////////////////////////////////////
bool me6e_pr_add_entry_pr_table(struct me6e_handler_t* handler, struct me6e_command_request_data* req)
{

    // ローカル変数
    char   v6addr[INET6_ADDRSTRLEN] = { 0 };
    char   macaddrstr[MAC_ADDRSTRLEN] = { 0 };

    // 引数チェック
    if((handler == NULL) || (req == NULL)) {
        return false;
    }

    // Commandデータ形式からME6E-PR Entry形式に変換
    me6e_pr_entry_t* entry = me6e_pr_command2entry(handler, &req->pr);

    if(entry == NULL) {
        me6e_logging(LOG_ERR,
             "fail to translate command data format to ME6E-PR Entry format : plane_id = %s mac address = %s ME6E-PR prefix = %s/%d\n",
             handler->conf->capsuling->plane_id,
             ether_ntoa_r((struct ether_addr*)&req->pr.macaddr, macaddrstr),
             inet_ntop(AF_INET6, &req->pr.pr_prefix, v6addr, INET6_ADDRSTRLEN),
             req->pr.v6cidr);
        // ここでConsoleに要求コマンド失敗のエラーを返す。
        me6e_pr_print_error(req->pr.fd, ME6E_PR_COMMAND_EXEC_FAILURE);

        return false;
    }
    else {
        // ME6E-PR Tableに当該エントリが登録済みかチェック
        // 登録済みの場合はConsoleにエラー出力
        if(me6e_search_pr_table(handler->pr_handler, &entry->macaddr) != NULL) {
            me6e_logging(LOG_ERR, "This entry is already exists(me6e_pr_add_entry_pr_table).");

            // ここでConsoleに要求コマンド失敗のエラーを返す。
            me6e_pr_print_error(req->pr.fd, ME6E_PR_COMMAND_ENTRY_FOUND);
            return false;
        }
        // 形式変換OKなのでME6E-PR TableにME6E-PR Entryを追加
        if(!me6e_pr_add_entry(handler->pr_handler, entry)) {
            me6e_logging(LOG_ERR,
                 "fail to add ME6E-PR Entry : plane_id = %s mac address = %s ME6E-PR prefix = %s/%d\n",
                 handler->conf->capsuling->plane_id,
                 ether_ntoa_r((struct ether_addr*)&req->pr.macaddr, macaddrstr),
                 inet_ntop(AF_INET6, &req->pr.pr_prefix, v6addr, INET6_ADDRSTRLEN),
                 req->pr.v6cidr);
            // ここでConsoleに要求コマンド失敗のエラーを返す。
            me6e_pr_print_error(req->pr.fd, ME6E_PR_COMMAND_EXEC_FAILURE);

            return false;
        }
        else {
            // nop
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Entry削除コマンド契機ME6E-PRテーブル削除関数
//!
//! 1.ME6E-PR TableからME6E-PR Entryを削除する。
//!
//! @param [in]     handler    ME6Eハンドラ
//! @param [in]     req        コマンド要求データ
//!
//! @return true        OK(削除成功)
//!         false       NG(削除失敗)
///////////////////////////////////////////////////////////////////////////////
bool me6e_pr_del_entry_pr_table(struct me6e_handler_t* handler, struct me6e_command_request_data* req)
{

    // ローカル変数
    char   v6addr[INET6_ADDRSTRLEN] = { 0 };
    char   macaddrstr[MAC_ADDRSTRLEN] = { 0 };

    // 引数チェック
    if((handler == NULL) || (req == NULL)) {
        return false;
    }
    // ME6E-PR Tableに当該エントリが登録されているかチェック
    // 登録されていない場合はConsoleにエラー出力
    if(me6e_search_pr_table(handler->pr_handler, &req->pr.macaddr) == NULL) {
        me6e_logging(LOG_ERR, "This entry is not exists.");

        // ここでConsoleに要求コマンド失敗のエラーを返す。
        me6e_pr_print_error(req->pr.fd, ME6E_PR_COMMAND_ENTRY_NOTFOUND);
        return false;
    }
    // ME6E-PR TableからME6E-PR Entryを削除
    if(!me6e_pr_del_entry(handler->pr_handler, &req->pr.macaddr)) {
        me6e_logging(LOG_ERR,
             "fail to del ME6E-PR Entry : plane_id = %s mac address = %s/%d ME6E-PR prefix = %s/%d\n",
             handler->conf->capsuling->plane_id,
             ether_ntoa_r((struct ether_addr*)&req->pr.macaddr, macaddrstr),
             inet_ntop(AF_INET6, &req->pr.pr_prefix, v6addr, INET6_ADDRSTRLEN),
             req->pr.v6cidr);
        // ここでConsoleに要求コマンド失敗のエラーを返す。
        me6e_pr_print_error(req->pr.fd, ME6E_PR_COMMAND_EXEC_FAILURE);

        return false;
    }
    else {
        // nop
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Entry全削除関数
//!
//! ME6E-PR TableからME6E-PR Entryを全て削除する。
//!
//! @param [in]     handler    ME6Eハンドラ
//! @param [in]     req        コマンド要求データ
//!
//! @return true        OK(削除成功)
//!         false       NG(削除失敗)
///////////////////////////////////////////////////////////////////////////////
bool me6e_pr_delall_entry_pr_table(struct me6e_handler_t* handler, struct me6e_command_request_data* req)
{
	
    // 引数チェック
    if((handler == NULL) || (req == NULL)) {
        return false;
    }

    // 排他開始
    pthread_mutex_lock(&handler->pr_handler->mutex);

    // ME6E-PR Entry全削除
    while(!me6e_list_empty(&handler->pr_handler->entry_list)){
        me6e_list* node = handler->pr_handler->entry_list.next;
        me6e_pr_entry_t* pr_entry = node->data;

        free(pr_entry);
        me6e_list_del(node);
        free(node);
        handler->pr_handler->num--;
    }

    //リストの初期化
    me6e_list_init(&handler->pr_handler->entry_list);

    // 排他解除
    pthread_mutex_unlock(&handler->pr_handler->mutex);

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Entry活性化コマンド契機ME6E-PR Entry活性化関数
//!
//! 1.ME6E-PR Tableの当該Entryを活性化する。
//!
//! @param [in]     handler    ME6Eハンドラ
//! @param [in]     req        コマンド要求データ
//!
//! @return true        OK(活性化成功)
//!         false       NG(活性化失敗)
///////////////////////////////////////////////////////////////////////////////
bool me6e_pr_enable_entry_pr_table(struct me6e_handler_t* handler, struct me6e_command_request_data* req)
{
    // ローカル変数
    char   v6addr[INET6_ADDRSTRLEN] = { 0 };
    char   macaddrstr[MAC_ADDRSTRLEN] = { 0 };

    // 引数チェック
    if((handler == NULL) || (req == NULL)) {
        return false;
    }

    //PR Entryを活性化
    if(!me6e_pr_set_enable(handler->pr_handler, &req->pr.macaddr, req->pr.enable)) {
        me6e_logging(LOG_ERR,
             "fail to enable ME6E-PR Entry : plane_id = %s mac address = %s/%d ME6E-PR prefix = %s\n",
             handler->conf->capsuling->plane_id,
             ether_ntoa_r((struct ether_addr*)&req->pr.macaddr, macaddrstr), 
             inet_ntop(AF_INET6, &req->pr.pr_prefix, v6addr, INET6_ADDRSTRLEN),
             req->pr.v6cidr);
        // ここでConsoleに要求コマンド失敗のエラーを返す。
        me6e_pr_print_error(req->pr.fd, ME6E_PR_COMMAND_EXEC_FAILURE);

        return false;
    }
    else {
        // nop
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Entry非活性化コマンド契機ME6E-PR Entry非活性化関数
//!
//! 1.コマンドデータ形式からME6E-PR Entry形式に変換する。
//! 2.ME6E-PR Tableの当該Entryを非活性化する。
//!
//! @param [in]     handler    ME6Eハンドラ
//! @param [in]     req        コマンド要求データ
//!
//! @return true        OK(非活性化成功)
//!         false       NG(非活性化失敗)
///////////////////////////////////////////////////////////////////////////////
bool me6e_pr_disable_entry_pr_table(struct me6e_handler_t* handler, struct me6e_command_request_data* req)
{
    // ローカル変数
    char   v6addr[INET6_ADDRSTRLEN] = { 0 };
    char   macaddrstr[MAC_ADDRSTRLEN] = { 0 };

    // 引数チェック
    if((handler == NULL) || (req == NULL)) {
        return false;
    }

    //PR Entryを非活性化
    if(!me6e_pr_set_enable(handler->pr_handler, &req->pr.macaddr, req->pr.enable)) {
        me6e_logging(LOG_ERR,
             "fail to serch ME6E-PR Entry : plane_id = %s mac address = %s ME6E-PR prefix = %s/%d\n",
             handler->conf->capsuling->plane_id,
             ether_ntoa_r((struct ether_addr*)&req->pr.macaddr, macaddrstr),
             inet_ntop(AF_INET6, &req->pr.pr_prefix, v6addr, INET6_ADDRSTRLEN),
             req->pr.v6cidr);
        // ここでConsoleに要求コマンド失敗のエラーを返す。
        me6e_pr_print_error(req->pr.fd, ME6E_PR_COMMAND_EXEC_FAILURE);

        return false;
    }
    else {
        // nop
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
////! @brief ハッシュテーブル内部情報出力関数
////!
////! ME6E-PR情報管理内のテーブルを出力する
////!
////! @param [in]     pr_handler      ME6E-PR情報管理
////! @param [in]     fd              出力先のディスクリプタ
////!
////! @return なし
/////////////////////////////////////////////////////////////////////////////////
void me6e_pr_show_entry_pr_table(me6e_pr_table_t* pr_handler, int fd, char *plane_id)
{

    // ローカル変数初期化
    char   v6addr[INET6_ADDRSTRLEN] = { 0 };
    char   macaddrstr[MAC_ADDRSTRLEN] = { 0 };

    // 引数チェック
    if(pr_handler == NULL) {
        return;
    }

    pthread_mutex_lock(&pr_handler->mutex);

    // ME6E-PR Table 表示
    dprintf(fd, "\n");
    dprintf(fd, " +------------------------------------------------------------------------------------------+\n");
    dprintf(fd, " /     ME6E Prefix Resolution Table                                                         /\n");
    dprintf(fd, " +---+-----------+----------------------+---------+-----------------------------------------+\n");
    dprintf(fd, "     | Plane ID  | MAC Address          | Netmask | ME6E-PR Address Prefix                  |\n");
    dprintf(fd, " +---+-----------+----------------------+---------+-----------------------------------------+\n");

    // entry_listが空でなければ表示する
    if(pr_handler->num != 0){
    me6e_list* iter;
    me6e_list_for_each(iter, &pr_handler->entry_list){
        me6e_pr_entry_t* pr_entry = iter->data;

            if(pr_entry != NULL){
                // enable/disable frag
                if(pr_entry->enable == true){
                    dprintf(fd, "   * |");
                } else {
                    dprintf(fd, "     |");
                }
                // plane id
                dprintf(fd, " %-9s |",plane_id);
                // MAC Address
                dprintf(fd, " %-20s |",ether_ntoa_r(&pr_entry->macaddr, macaddrstr));
                // Netmask
                dprintf(fd, " /%-6d |",pr_entry->v6cidr);
                // IPv6-PR Address Prefix
                dprintf(fd, " %-39s |\n",inet_ntop(AF_INET6, &pr_entry->pr_prefix, v6addr, sizeof(v6addr)));
            }
    }

    dprintf(fd, " +---+-----------+----------------------+---------+-----------------------------------------+\n");
    dprintf(fd, "  Note : [*] shows available entry for prefix resolution process.\n");
    }
    dprintf(fd, "\n");

    pthread_mutex_unlock(&pr_handler->mutex);

    return;
}


///////////////////////////////////////////////////////////////////////////////
////! @brief ME6E-PRモードエラー出力関数
////!
////! 引数で指定されたファイルディスクリプタにエラーを出力する。
////!
////! @param [in] fd           出力先のファイルディスクリプタ
////! @param [in] error_code   エラーコード
////!
////! @return なし
/////////////////////////////////////////////////////////////////////////////////
void me6e_pr_print_error(int fd, enum me6e_pr_command_error_code error_code)
{
   // ローカル変数宣言

    // ローカル変数初期化

    switch(error_code) {
    case ME6E_PR_COMMAND_MODE_ERROR:
        dprintf(fd, "\n");
        dprintf(fd,"Requested command is available for ME6E-PR mode only!\n");
        dprintf(fd, "\n");

        break;

    case ME6E_PR_COMMAND_EXEC_FAILURE:
        dprintf(fd, "\n");
        dprintf(fd,"Sorry! Fail to execute your requested command. \n");
        dprintf(fd, "\n");

        break;

    case ME6E_PR_COMMAND_ENTRY_FOUND:
        dprintf(fd, "\n");
        dprintf(fd,"Requested entry is already exist. \n");
        dprintf(fd, "\n");

        break;

    case ME6E_PR_COMMAND_ENTRY_NOTFOUND:
        dprintf(fd, "\n");
        dprintf(fd,"Requested entry is not exist. \n");
        dprintf(fd, "\n");

        break;

    default:
        // ありえないルート

        break;

    }

    return;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PRユニキャストプレーンプレフィックス生成関数
//!
//! ME6E-PRのプレフィックスとPlane IDの値を元に
//! ME6Eユニキャストプレーンプレフィックスを生成する。
//!
//! @param [in]     handler      ME6Eハンドラ
//!
//! @retval 0     正常終了
//! @retval 0以外 異常終了
///////////////////////////////////////////////////////////////////////////////
int me6e_pr_setup_uni_plane_prefix(struct me6e_handler_t* handler)
{
    me6e_config_t* conf;
    uint8_t*        src_addr;
    uint8_t*        dst_addr;
    int             prefixlen;
    char address[INET6_ADDRSTRLEN] = { 0 };
    
    // 引数チェック
    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(me6e_pr_setup_uni_plane_prefix).");
        return -1;
    }

    conf = handler->conf;
    if(conf->capsuling->plane_id != NULL){
        // Plane IDが指定されている場合は、Plane IDで初期化する。
        strcat(address, "::");
        strcat(address, conf->capsuling->plane_id);
        strcat(address, ":0:0:0");
        DEBUG_LOG("address =  %s\n", address);

        // 値の妥当性は設定読み込み時にチェックしているので戻り値は見ない
        inet_pton(AF_INET6, address, &handler->unicast_prefix);
    }
    else{
        // Plane IDが指定されていない場合はALL0で初期化する
        handler->unicast_prefix   = in6addr_any;
    }

    // plefix length分コピーする
    src_addr  = conf->capsuling->pr_unicast_prefixplaneid->s6_addr;
    dst_addr  = handler->unicast_prefix.s6_addr;
    prefixlen = conf->capsuling->unicast_prefixlen;
    for(int i = 0; (i < 16 && prefixlen > 0); i++, prefixlen-=CHAR_BIT){
        if(prefixlen >= CHAR_BIT){
            dst_addr[i] = src_addr[i];
        }
        else{
            dst_addr[i] = (src_addr[i] & (0xff << (CHAR_BIT - prefixlen))) |
                            (dst_addr[i] & ~(0xff << (CHAR_BIT - prefixlen)));
            break;
        }
    }

    DEBUG_LOG("unicast_prefix = %s\n",
            inet_ntop(AF_INET6, &handler->unicast_prefix, address, sizeof(address)));
    return 0;
}

