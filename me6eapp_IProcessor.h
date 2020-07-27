/******************************************************************************/
/* ファイル名 : me6eapp_IProcessor.h                                          */
/* 機能概要   : パケット処理抽象クラス ヘッダファイル                         */
/* 修正履歴   : 2013.01.11 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#ifndef ___ME6EAPP_IPROCESSOR_H__
#define ___ME6EAPP_IPROCESSOR_H__


#include "me6eapp.h"

///////////////////////////////////////////////////////////////////////////////
//! パケット処理抽象クラスの構造体
///////////////////////////////////////////////////////////////////////////////
struct IProcessor_t
{
        void* data;                                     ///< カプセル化フィールド
        bool  (*init)(struct IProcessor_t* proc,
            struct me6e_handler_t* handler);           ///< 初期化メソッド
        void  (*release)(struct IProcessor_t* proc);    ///< 終了メソッド
        bool  (*recv_from_stub)
            (struct IProcessor_t* proc,
                char* recv_buffer, ssize_t recv_len);   ///< StubNWパケット処理メソッド
        bool  (*recv_from_backbone)
            (struct IProcessor_t* proc,
                char* recv_buffer, ssize_t recv_len);   ///< BackBoneパケット処理メソッド
};
typedef struct IProcessor_t IProcessor;

///////////////////////////////////////////////////////////////////////////////
//! メソッドアクセス用マクロ
///////////////////////////////////////////////////////////////////////////////
#define IProcessor_Init(p, handler)                             (p)->init(p, handler)
#define IProcessor_Release(p)                                   (p)->release(p)
#define IProcessor_RecvFromStub(p, recv_buffer, recv_len)       (p)->recv_from_stub(p, recv_buffer, recv_len)
#define IProcessor_RecvFromBackbone(p, recv_buffer, recv_len)   (p)->recv_from_backbone(p, recv_buffer, recv_len)

#endif // ___ME6EAPP_IPROCESSOR_H__

