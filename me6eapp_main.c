/******************************************************************************/
/* ファイル名 : me6eapp_main.c                                                */
/* 機能概要   : メイン関数 ソースファイル                                     */
/* 修正履歴   : 2013.01.08 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*            : 2016.06.01 S.Anai PR機能の追加                                */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/signalfd.h>
#include <sys/prctl.h>


#include "me6eapp.h"
#include "me6eapp_config.h"
#include "me6eapp_statistics.h"
#include "me6eapp_log.h"
#include "me6eapp_setup.h"
#include "me6eapp_Controller.h"
#include "me6eapp_mainloop.h"
#include "me6eapp_pr.h"

// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

//! コマンドオプション構造体
static const struct option options[] = {
    {"file",  required_argument, 0, 'f'},
    {"help",  no_argument,       0, 'h'},
    {"usage", no_argument,       0, 'h'},
    {0, 0, 0, 0}
};

///////////////////////////////////////////////////////////////////////////////
//! @brief コマンド凡例表示関数
//!
//! コマンド実行時の引数が不正だった場合などに凡例を表示する。
//!
//! @param なし
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void usage(void)
{
    fprintf(stderr,
"Usage: me6eapp { -f | --file } CONFIG_FILE \n"
"       me6eapp { -h | --help | --usage }\n"
"\n"
    );

    return;
}


////////////////////////////////////////////////////////////////////////////////
// メイン関数
////////////////////////////////////////////////////////////////////////////////
int main(int argc, char *argv[])
{
    // ローカル変数宣言
    struct me6e_handler_t handler;
    int                    ret = 0;
    char*                  conf_file = NULL;
    int                    option_index = 0;
    pthread_t              bb_tid = -1;
    pthread_t              stub_tid = -1;

    // 初期化処理
    memset(&handler, 0, sizeof(handler));

    // 引数チェック
    while (1) {
        int c = getopt_long(argc, argv, "f:h", options, &option_index);
        if (c == -1)
            break;

        switch (c) {
        case 'f':
            conf_file = optarg;
            break;

        case 'h':
            usage();
            exit(EXIT_SUCCESS);
            break;

        default:
            usage();
            exit(EINVAL);
            break;
        }
    }

    if(conf_file == NULL){
        usage();
        exit(EINVAL);
    }

    // デバッグ時は設定ファイル解析前に標準エラー出力有り、
    // デバッグログ出力有りで初期化する
#ifdef DEBUG
    me6e_initial_log(NULL, true);
#else
    me6e_initial_log(NULL, false);
#endif

    me6e_logging(LOG_INFO, "ME6E application start!!.");

    // シグナルの登録
    if (me6e_set_signal(&handler) != 0 ) {
        me6e_logging(LOG_ERR, "fail to set signal.");
        return -1;
    }

    // 設定ファイルの読み込み
    handler.conf = me6e_config_load(conf_file);
    if(handler.conf == NULL){
        me6e_logging(LOG_ERR, "fail to load config file.");
        return -1;
    }
    _D_(me6e_config_dump(handler.conf, STDOUT_FILENO);)

    // 設定ファイルの内容でログ情報を再初期化
    me6e_initial_log(
        handler.conf->common->plane_name,
        handler.conf->common->debug_log
    );

    // デーモン化
    if(handler.conf->common->daemon && (daemon(0, 0) != 0)){
        me6e_logging(LOG_ERR, "fail to daemonize : %s.", strerror(errno));
        me6e_config_destruct(handler.conf);
        return -1;
    }

    // 統計情報用の初期化
    handler.stat_info = me6e_initial_statistics();
    if (handler.stat_info == NULL) {
        me6e_logging(LOG_ERR, "fail to initial statistics.");
        me6e_config_destruct(handler.conf);
        return -1;
    }

    // ME6E ユニキャストprefix アドレスの格納
    if(handler.conf->common->tunnel_mode == ME6E_TUNNEL_MODE_PR){
        // PRモードならば、PR PrefixをME6Eユニキャストに設定
        if(me6e_pr_setup_uni_plane_prefix(&handler) != 0){
            me6e_logging(LOG_ERR, "fail to setup pr unicast prefix address.");
            me6e_finish_statistics(handler.stat_info);
            me6e_config_destruct(handler.conf);
            return -1;
        }
    }
    else{
        if(me6e_setup_uni_plane_prefix(&handler) != 0){
            me6e_logging(LOG_ERR, "fail to setup unicast prefix address.");
            me6e_finish_statistics(handler.stat_info);
            me6e_config_destruct(handler.conf);
            return -1;
        }
    }

    // ME6E マルチキャストprefix アドレスの格納
    if(me6e_setup_multi_plane_prefix(&handler) != 0){
        me6e_logging(LOG_ERR, "fail to setup multicast prefix address.");
        me6e_finish_statistics(handler.stat_info);
        me6e_config_destruct(handler.conf);
        return -1;
    }

    // ME6E-PR tableの生成
    if(handler.conf->common->tunnel_mode == ME6E_TUNNEL_MODE_PR){
        handler.pr_handler = me6e_pr_init_pr_table(&handler);
        if(handler.pr_handler == NULL){
            me6e_logging(LOG_ERR, "fail to create ME6E-PR Table\n");
            _exit(-1);
        }
    }

    // トンネルデバイス生成
    if(me6e_create_tunnel_device(&handler) != 0){
        me6e_logging(LOG_ERR, "fail to tunnel device.");
        me6e_delete_tunnel_device(&handler);
        me6e_finish_statistics(handler.stat_info);
        me6e_config_destruct(handler.conf);
        return -1;
    }

    // Bridgeデバイス生成
    if(me6e_create_bridge_device(&handler) != 0){
        me6e_logging(LOG_ERR, "fail to bridge device.");
        me6e_delete_bridge_device(&handler);
        me6e_delete_tunnel_device(&handler);
        me6e_finish_statistics(handler.stat_info);
        me6e_config_destruct(handler.conf);
        return -1;
    }

    // Bridgeへアタッチ
    if(me6e_attach_bridge(&handler) != 0){
        me6e_logging(LOG_ERR, "fail to attach bridge device.");
        me6e_detach_bridge(&handler);
        me6e_delete_bridge_device(&handler);
        me6e_delete_tunnel_device(&handler);
        me6e_finish_statistics(handler.stat_info);
        me6e_config_destruct(handler.conf);
        return -1;
    }

    // Stub側のネットワークデバイスの起動
    if(me6e_start_stub_network(&handler) != 0) {
        me6e_logging(LOG_ERR, "fail to start sub network.");
        me6e_detach_bridge(&handler);
        me6e_delete_bridge_device(&handler);
        me6e_delete_tunnel_device(&handler);
        me6e_finish_statistics(handler.stat_info);
        me6e_config_destruct(handler.conf);
        return -1;
    }

    // Backbone側のネットワークデバイス設定
    if(me6e_setup_backbone_network(&handler) != 0){
        me6e_logging(LOG_ERR, "fail to setup backbone networks.");
        me6e_close_backbone_network(&handler);
        me6e_detach_bridge(&handler);
        me6e_delete_bridge_device(&handler);
        me6e_delete_tunnel_device(&handler);
        me6e_finish_statistics(handler.stat_info);
        me6e_config_destruct(handler.conf);
        return -1;
    }

    // 各機能クラスのインスタンスを生成
    if(me6e_construct_instances(&handler) != 0){
        me6e_logging(LOG_ERR, "fail to construct instances.");
        // 異常終了
        ret = -1;
        goto app_finish;
    }

    // 各機能インスタンスの初期化処理
    if(!me6e_init_instances(&handler)) {
        me6e_logging(LOG_ERR, "fail to init instances.");
        // 異常終了
        ret = -1;
        goto app_finish;
    }

    // スタートアップスクリプト実行
    run_startup_script(&handler);

    // Backbone側カプセリングパケット送受信スレッド起動
    if(pthread_create(&bb_tid, NULL, me6e_tunnel_backbone_thread, &handler) != 0){
        me6e_logging(LOG_ERR, "fail to create backbone tunnel thread : %s.", strerror(errno));
        // 異常終了
        ret = -1;
        goto app_finish;
    }

    // Stub側カプセリングパケット送受信スレッド起動
    if(pthread_create(&stub_tid, NULL, me6e_tunnel_stub_thread, &handler) != 0){
        me6e_logging(LOG_ERR, "fail to create stub tunnel thread : %s.", strerror(errno));
        if(handler.conf->common->tunnel_mode == ME6E_TUNNEL_MODE_PR){
            me6e_pr_destruct_pr_table(handler.pr_handler);
        }
        // 異常終了
        ret = -1;
        goto app_finish;
    }

    // mainloop
    ret = me6e_mainloop(&handler);

app_finish:
    // 後処理
    if (bb_tid != -1 ) {
        pthread_cancel(bb_tid);
        pthread_join(bb_tid, NULL);
    }

    if (stub_tid != -1 ) {
        pthread_cancel(stub_tid);
        pthread_join(stub_tid, NULL);
    }

    me6e_release_instances(&handler);
    me6e_destroy_instances(&handler);
    me6e_close_backbone_network(&handler);
    me6e_detach_bridge(&handler);
    me6e_delete_bridge_device(&handler);
    me6e_delete_tunnel_device(&handler);
    if(handler.conf->common->tunnel_mode == ME6E_TUNNEL_MODE_PR){
        me6e_pr_destruct_pr_table(handler.pr_handler);
    }
    me6e_finish_statistics(handler.stat_info);
    me6e_logging(LOG_INFO, "ME6E application finish!!");
    me6e_config_destruct(handler.conf);

    return ret;
}

