/******************************************************************************/
/* ファイル名 : me6eapp_MacManager.c                                          */
/* 機能概要   : Mac Managerクラス ソースファイル                              */
/* 修正履歴   : 2013.01.11 S.Yoshikawa 新規作成                               */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/ether.h>
#include "me6eapp_IProcessor.h"
#include "me6eapp_MacManager.h"
#include "me6eapp_config.h"
#include "me6eapp_util.h"
#include "me6eapp_log.h"
#include "me6eapp_network.h"


// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

///////////////////////////////////////////////////////////////////////////////
//! MacManagerクラスのフィールド定義
///////////////////////////////////////////////////////////////////////////////
struct MacManagerField{
       struct me6e_handler_t *handler;        ///< ME6Eのアプリケーションハンドラー
};
typedef struct MacManagerField MacManagerField;

////////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
static bool MacManager_Init(IProcessor* self, struct me6e_handler_t *handler);
static void MacManager_Release(IProcessor* proc);
static bool MacManager_RecvFromStub(IProcessor* self, char* recv_buffer, ssize_t recv_len);
static bool MacManager_RecvFromBackbone(IProcessor* self, char* recv_buffer, ssize_t recv_len);
static inline bool MacManager_entry_init(struct me6e_handler_t *handler);

#ifdef DEBUG
static void MacManager_print_hash_table(const char* key, const void* value, void* userdata);
#endif


///////////////////////////////////////////////////////////////////////////////
//! MacManagerのフィールドアクセス用マクロ
///////////////////////////////////////////////////////////////////////////////
#define MACMANAGER_FIELD(ptr)((MacManagerField*)(ptr->data))


///////////////////////////////////////////////////////////////////////////////
//! @brief MacMangerインスタンス生成関数
//!
//! MacManagerインスタンスを生成する。
//!
//! @return IProcessor構造体へのポインター
///////////////////////////////////////////////////////////////////////////////
IProcessor* MacManager_New()
{
    IProcessor* instance = NULL;

    DEBUG_LOG("MacManager_New start.\n");

    // インスタンスの生成
    instance = malloc(sizeof(IProcessor));
    if (instance == NULL) {
        me6e_logging(LOG_ERR, "fail to malloc for Mac Manager instance.");
        return NULL;
    }

    memset(instance, 0, sizeof(IProcessor));

    // フィールドの生成
    instance->data = malloc(sizeof(MacManagerField));
    if (instance->data== NULL) {
        me6e_logging(LOG_ERR, "fail to malloc for Mac Manager instance field.");
        return NULL;
    }
    memset(instance->data, 0, sizeof(MacManagerField));


    // メソッドの登録
    instance->init               = MacManager_Init;
    instance->release            = MacManager_Release;
    instance->recv_from_stub     = MacManager_RecvFromStub;
    instance->recv_from_backbone = MacManager_RecvFromBackbone;

    DEBUG_LOG("MacManager_New end.\n");
    return instance;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MacMangerインスタンス初期化関数
//!
//! MacManagerインスタンスのフィールドを初期化する
//!
//! @param [in] sefl IProcessor構造体
//! @param [in] handler アプリケーションハンドラー
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool MacManager_Init(IProcessor* self, struct me6e_handler_t *handler)
{
    bool result = true;
    me6e_config_mng_macaddr_t* mac;

    DEBUG_LOG("MacManager_Init start.\n");

    // 引数チェック
    if ( (handler == NULL) || (self == NULL) ) {
        me6e_logging(LOG_ERR, "Parameter Check NG(MacManager_Init).");
        return false;
    }

    // ハンドラーの設定
    MACMANAGER_FIELD(self)->handler = handler;

    // 静的エントリーの生成とホストに対応するME6Eアドレスの設定
    mac = handler->conf->mac;
    if (mac->mng_macaddr_enable) {
        if (mac->mac_entry_max > 0) {
            result = MacManager_entry_init(handler);
            if(!result) {
                me6e_logging(LOG_ERR, "fail to create mac manager host entry.");
                return result;
            }
        }
    }

    DEBUG_LOG("MacManager_Init end.\n");
    return  result;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief MacMangerインスタンス解放関数
//!
//! MacManagerインスタンスが保持するリソースを解放する。
//!
//! @param [in] self IProcessor構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static void MacManager_Release(IProcessor* self)
{
        // 引数チェック
    if (self == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(MacManager_Release).\n");
        return;
    }

    DEBUG_LOG("MacManager_Release start.");

    // MAC管理静的エントリの解放
    if (MACMANAGER_FIELD(self)->handler != NULL) {
        if (MACMANAGER_FIELD(self)->handler->mac_manager_static_entry != NULL) {
            me6e_hashtable_delete(MACMANAGER_FIELD(self)->handler->mac_manager_static_entry);
        }
    }

    // フィールドの解放
    free(MACMANAGER_FIELD(self));

    // インスタンスの解放
    free(self);

    DEBUG_LOG("MacManager_Release end.");
    return;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief StubNWパケット受信処理関数
//!
//! StubNWから受信したパケットの送信元ホストのME6Eアドレスを登録する。
//!
//! @param [in] self        IProcessor構造体
//! @param [in] recv_buffer 受信データ
//! @param [in] recv_len    受信データのサイズ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool MacManager_RecvFromStub(IProcessor* self, char* recv_buffer, ssize_t recv_len)
{
    DEBUG_LOG("MacManager_RecvFromStub\n");

    // 引数チェック
    if ((self == NULL) || (recv_buffer == NULL) || (recv_len <= 0)) {
        me6e_logging(LOG_ERR, "Parameter Check NG(MacManager_RecvFromStub).\n");
        return false;
    }

    struct me6e_handler_t * handler = MACMANAGER_FIELD(self)->handler;

    // 動的エントリー機能動作有無判定
    if (!(handler->conf->mac->mac_entry_update)) {
        // 次のクラスの処理を継続
        return true;
    }

    DEBUG_LOG("Mac Manager dynamic entry enable route start.\n");

    struct ethhdr*  p_orig_eth_hdr = (struct ethhdr*)recv_buffer;

    if(me6e_util_is_broadcast_mac(&p_orig_eth_hdr->h_source[0])){
        // ブロードキャストパケット
        DEBUG_LOG("recv src mac address broadcast packet.\n");
        // 次のクラスの処理を継続
        return true;

    } else if(me6e_util_is_multicast_mac(&p_orig_eth_hdr->h_source[0])){
        // マルチキャストパケット
        DEBUG_LOG("recv src mac address multicast packet.\n");
        // 次のクラスの処理を継続
        return true;

    } else{
        // ユニキャストパケット
        DEBUG_LOG("recv src mac address unicast packet.\n");

        struct in6_addr v6addr;
        if (me6e_create_me6eaddr(
                    &(handler->unicast_prefix),
                    (struct ether_addr*)&(p_orig_eth_hdr->h_source[0]),
                    &v6addr) == NULL) {
            me6e_logging(LOG_ERR, "fail to me6e_create_me6eaddr.");
            // 次のクラスの処理を継続
            return  true;
        }

        char macaddrstr[MAC_ADDRSTRLEN] = { 0 };

        char* result = NULL;
        // MACアドレスを文字列変換
        ether_ntoa_r((struct ether_addr*)&(p_orig_eth_hdr->h_source[0]), macaddrstr);

        // MACアドレスをkeyにデータ検索
        result = me6e_hashtable_get(handler->mac_manager_static_entry, macaddrstr);
        if(result!= NULL){
            DEBUG_LOG("Mach static etnry %s.\n", result);
        } else {
            DEBUG_LOG("Unmach static etnry %s.\n", macaddrstr);
            // StubNWに収容しているホストの動的エントリーの追加
            int time = handler->conf->mac->mac_vaild_lifetime;
            int ret = me6e_network_add_ipaddr_with_vtime(AF_INET6,
                    handler->conf->capsuling->tunnel_device.ifindex,
                    &v6addr, 128, time, time);

            if (ret != 0) {
                me6e_logging(LOG_ERR, "fail to add host ipv6 address : %d.", ret);
                // 次のクラスの処理を継続
                return  true;
            }
        }
    }

    return  true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief BackBoneパケット受信処理関
//!
//! BackboneNWから受信したパケットを処理する。
//!
//! @param [in] self IProcessor構造体
//! @param [in] recv_buffer 受信データ
//! @param [in] recv_len    受信データのサイズ
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static bool MacManager_RecvFromBackbone(IProcessor* self, char* recv_buffer, ssize_t recv_len)
{
    // backboneの処理
    DEBUG_LOG("MacManager_RecvFromBackbone\n");

    // 処理なし
    return  true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief 収容ホストテーブル初期化関数
//!
//! StubNWに収容するホストme6eアドレステーブルの作成を行う。
//!
//! @param [in] handler アプリケーションハンドラー
//!
//! @return true    正常終了
//! @return false   異常終了
///////////////////////////////////////////////////////////////////////////////
static inline bool MacManager_entry_init(struct me6e_handler_t *handler)
{
    bool result = true;
    int ret = 0;
    struct in6_addr* prefix;
    struct ether_addr* addr;
    struct in6_addr v6addr;
    char macaddrstr[MAC_ADDRSTRLEN] = { 0 };

    if (handler == NULL) {
        me6e_logging(LOG_ERR, "Parameter Check NG(MacManager_entry_init).");
        return false;
    }

    me6e_config_mng_macaddr_t* mac = handler->conf->mac;
    prefix = &handler->unicast_prefix;

    // hashの生成
    handler->mac_manager_static_entry =
            me6e_hashtable_create(mac->mac_entry_max);

    if (handler->mac_manager_static_entry == NULL) {
        me6e_logging(LOG_ERR, "fail to create mac manager host entry.\n");
        return false;
    }

    // static情報の登録
    me6e_list* iter;
    me6e_list_for_each(iter, &(mac->hosthw_addr_list)){
        addr = iter->data;
        // MACアドレスから、ME6Eアドレスへの変換
        v6addr.s6_addr[10] = addr->ether_addr_octet[0];
        v6addr.s6_addr[11] = addr->ether_addr_octet[1];
        v6addr.s6_addr[12] = addr->ether_addr_octet[2];
        v6addr.s6_addr[13] = addr->ether_addr_octet[3];
        v6addr.s6_addr[14] = addr->ether_addr_octet[4];
        v6addr.s6_addr[15] = addr->ether_addr_octet[5];

        result = me6e_hashtable_add(
            handler->mac_manager_static_entry,
            ether_ntoa_r(addr, macaddrstr),
            (void*)me6e_create_me6eaddr(prefix, addr, &v6addr),
            sizeof(v6addr),
            false,
            NULL,
            NULL
        );

        if (!result) {
            me6e_logging(LOG_ERR, "fail to add mac manager host entry.\n");
            return result;
        }

        // StubNWに収容しているホストの静的エントリーのME6Eアドレスの追加
        ret = me6e_network_add_ipaddr(AF_INET6,
                        handler->conf->capsuling->tunnel_device.ifindex,
                        &v6addr, 128);

        if (ret != 0) {
            me6e_logging(LOG_ERR, "fail to add host ipv6 address.\n");
        }
    }
    _D_(me6e_hashtable_foreach(handler->mac_manager_static_entry, MacManager_print_hash_table, NULL);)

    return result;
}

#ifdef DEBUG
///////////////////////////////////////////////////////////////////////////////
//! @brief ハッシュテーブル内部情報出力関数
//!
//! 設定情報のハッシュテーブルを出力する
//!
//! @param [in] key         ハッシュのキー
//! @param [in] value       ハッシュのバリュー
//! @param [in] userdata    未使用
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
static void MacManager_print_hash_table(const char* key, const void* value, void* userdata)
{
    char address[INET6_ADDRSTRLEN];

    // 引数チェック
    if( (key == NULL) || (value == NULL) ){
        me6e_logging(LOG_ERR, "Parameter Check NG(MacManager_print_hash_table).");
        return;
    }

    dprintf(STDOUT_FILENO, "%-22s|%-46s\n", key,
                    inet_ntop(AF_INET6, value, address, sizeof(address)));

    return;
}
#endif

