/******************************************************************************/
/* ファイル名 : me6eapp_hashtable.h                                           */
/* 機能概要   : ハッシュテーブルクラス ヘッダファイル                         */
/* 修正履歴   : 2013.01.08 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef __ME6EAPP_HASHTABLE_H__
#define __ME6EAPP_HASHTABLE_H__

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

//! ハッシュテーブルの要素構造体
typedef struct _me6e_hash_cell_t me6e_hash_cell_t;
//! ハッシュテーブル構造体
typedef struct _me6e_hashtable_t me6e_hashtable_t;

//! ユーザ指定ハッシュ要素コピー関数
typedef void* (*me6e_hash_copy_func)(const void* src, const size_t size);
//! ユーザ指定ハッシュ要素削除関数
typedef void  (*me6e_hash_delete_func)(void* obj);
//! ユーザ指定ハッシュ要素出力関数
typedef void  (*me6e_hash_foreach_cb)(const char* key, const void* value, void* userdata);

////////////////////////////////////////////////////////////////////////////////
// 外部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
me6e_hashtable_t* me6e_hashtable_create(const uint32_t table_size);
void  me6e_hashtable_delete(me6e_hashtable_t* table);
bool  me6e_hashtable_add(me6e_hashtable_t* table, const char* key, const void* value,
            const size_t value_size, const bool overwrite,
            me6e_hash_copy_func copy_func, me6e_hash_delete_func delete_func);
bool  me6e_hashtable_remove(me6e_hashtable_t* table, const char* key, void** value);
void* me6e_hashtable_get(me6e_hashtable_t* table, const char* key);
void  me6e_hashtable_clear(me6e_hashtable_t* table);
void  me6e_hashtable_foreach(me6e_hashtable_t* table, me6e_hash_foreach_cb callback, void* userdata);

#endif // __ME6EAPP_HASHTABLE_H__

