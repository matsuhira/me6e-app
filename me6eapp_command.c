/*******************************************************************************/
/* ファイル名 : me6eapp_command.c                                              */
/* 機能概要   : 内部コマンドクラス ソースファイル                              */
/* 修正履歴   : 2013.01.24 Y.Shibata 新規作成                                  */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                     */
/*            : 2016.05.27 M.Kawano     PR機能の追加                           */
/*                                                                             */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                 */
/*******************************************************************************/
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/fcntl.h>
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <netinet/ether.h>
#include <limits.h>

#include "me6eapp.h"
#include "me6eapp_command.h"
#include "me6eapp_socket.h"
#include "me6eapp_log.h"

#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

//! オプション引数構造体定義
struct opt_arg {
    char* main;   ///< メインコマンド文字列
    char* sub;    ///< サブコマンド文字列
    int   code;   ///< コマンドコード
};

//! コマンド引数
static const struct opt_arg opt_args[] = {
    {"add",     "pr",  ME6E_ADD_PR},
    {"del",     "pr",  ME6E_DEL_PR},
    {"enable",  "pr",  ME6E_ENABLE_PR},
    {"disable", "pr",  ME6E_DISABLE_PR},
    {"delall",  "pr",  ME6E_DELALL_PR},
    {NULL,       NULL, ME6E_COMMAND_MAX}
};

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
        return false;
    }

    // ローカル変数初期化
    result = true;

    if(!strcasecmp(str, OPT_BOOL_ON)){
        *output = true;
    }
    else if(!strcasecmp(str, OPT_BOOL_OFF)){
        *output = false;
    }
    else if(!strcasecmp(str, OPT_BOOL_ENABLE)){
        *output = true;
    }
    else if(!strcasecmp(str, OPT_BOOL_DISABLE)){
        *output = false;
    }
    else if(!strcasecmp(str, OPT_BOOL_NONE)){
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
        _D_(printf("parse_int Parameter Check NG.\n");)
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
        _D_(printf("parse_int Parameter too big.\n");)
        result = false;
    }
    else if(tmp < min) {
        // 最小値よりも小さいのでエラー
        _D_(printf("parse_int Parameter too small.\n");)
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
static bool parse_ipv6address_pr(const char* str, struct in6_addr* output, int* prefixlen)
{
    // ローカル変数定義
    bool  result;
    char* tmp;
    char* token;

    DEBUG_LOG("%s start", __func__);

    // 引数チェック
    if((str == NULL) || (output == NULL)){
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
            *prefixlen = -1;
            result = false;
        }
        else{
            result = parse_int(token, prefixlen, CONFIG_IPV6_PREFIX_MIN, CONFIG_IPV6_PREFIX_MAX);
        }
    }

    free(tmp);

    DEBUG_LOG("%s end. return %s", __func__, result?"true":"false");
    return result;
}


///////////////////////////////////////////////////////////////////////////////
//! @brief ARP コマンドオプション設定処理関数
//!
//! ARPエントリーの追加と削除用のオプションを設定する。
//!
//! @param [in]  opt1       IPv4アドレスの文字列
//! @param [in]  opt2       MACアドレスの文字列
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool me6e_command_arp_set_option(
        const char * opt1,
        const char * opt2,
        struct me6e_command_t* command
)
{
    // 引数チェック
    if ((opt1 == NULL) || (command == NULL)){
        printf("Parameter Check NG.\n");
        return false;
    }


    struct in_addr      v4addr;
    struct ether_addr   macaddr;

    // IPv4アドレスの形式であるか確認
    if(inet_pton(AF_INET, opt1, &v4addr) <= 0){
        printf("fail to parse ipv4 address.\n");
        return false;
    }

    // IPv4アドレスの文字列形式を統一
    if(inet_ntop(AF_INET, &v4addr, command->req.arp.ipv4addr, INET_ADDRSTRLEN) == NULL){
        printf("fail to parse ipv4 address.\n");
        return false;
    }

    if (command->code == ME6E_ADD_ARP) {
        // Proxy ARP テーブルへの登録

        if (opt2 == NULL){
            printf("Parameter Check NG.\n");
            return false;
        }

        command->req.arp.code = ME6E_COMMAND_ARP_ADD;

        // MACアドレスの形式であるか確認
        if(ether_aton_r(opt2, &macaddr) == NULL){
            printf("fail to parse mac address.\n");
            return false;
        }

        // MACアドレスの文字列形式を統一
        if(ether_ntoa_r(&macaddr, command->req.arp.macaddr) == NULL){
            printf("fail to parse mac address.\n");
            return false;
        }
    } else if (command->code == ME6E_DEL_ARP) {
        // Proxy ARP テーブルからの削除

        command->req.arp.code = ME6E_COMMAND_ARP_DEL;
    }

    return true;

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
//! @brief NDP コマンドオプション設定処理関数
//!
//! NDPエントリーの追加と削除用のオプションを設定する。
//!
//! @param [in]  opt1       IPv6アドレスの文字列
//! @param [in]  opt2       MACアドレスの文字列
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool me6e_command_ndp_set_option(
        const char * opt1,
        const char * opt2,
        struct me6e_command_t* command
)
{
    // 引数チェック
    if ((opt1 == NULL) || (opt2 == NULL) || (command == NULL)){
        printf("Parameter Check NG.\n");
        return false;
    }

    struct in6_addr     v6addr;
    struct ether_addr   macaddr;

    // IPv6アドレスの形式であるか確認
    if(inet_pton(AF_INET6, opt1, &v6addr) <= 0){
        printf("fail to parse ipv6 address.\n");
        return false;
    }


    // IPv6アドレスの文字列形式を統一
    if(inet_ntop(AF_INET6, &v6addr, command->req.ndp.ipv6addr, INET6_ADDRSTRLEN) == NULL){
        printf("fail to parse ipv6 address.\n");
        return false;
    }

    if (command->code == ME6E_ADD_NDP) {
        // Proxy NDP テーブルへの登録

        if (opt2 == NULL){
            printf("Parameter Check NG.\n");
            return false;
        }

        command->req.ndp.code = ME6E_COMMAND_NDP_ADD;

        // MACアドレスの形式であるか確認
        if(ether_aton_r(opt2, &macaddr) == NULL){
            printf("fail to parse mac address.\n");
            return false;
        }

        // MACアドレスの文字列形式を統一
        if(ether_ntoa_r(&macaddr, command->req.ndp.macaddr) == NULL){
            printf("fail to parse mac address.\n");
            return false;
        }
    } else if (command->code == ME6E_DEL_NDP) {
        // Proxy NDP テーブルからの削除

        command->req.ndp.code = ME6E_COMMAND_NDP_DEL;
    }

    return true;

}


///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Entry追加コマンドオプション設定処理関数
//!
//! ME6E-PR Entry追加コマンドのオプションを設定する。
//!
//! @param [in]  opt1       MACアドレスの文字列
//! @param [in]  opt2       IPv6アドレス文字列
//! @param [in]  opt3       モード
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool me6e_command_pr_add_option(
        const char * opt1,
        const char * opt2,
        const char * opt3,
        struct me6e_command_t* command
)
{

    // 引数チェック
    if ((opt1 == NULL) || (opt2 == NULL) || (opt3 == NULL) || (command == NULL)){
        printf("Parameter Check NG.\n");
        return false;
    }

    // opt1:mac address
    if (!parse_macaddress(opt1, &command->req.pr.macaddr)) {
        printf("fail to parse parameters 'macaddress'\n");
        return false;
    }
    // opt2:me6e-pr_prefix/prefix_len
    if (!parse_ipv6address_pr(opt2, &command->req.pr.pr_prefix, &command->req.pr.v6cidr)) {
        printf("fail to parse parameters 'm66e-pr prefix/prefix_len'\n");
        return false;
    }
    // opt3:mode
    if (!parse_bool(opt3, &command->req.pr.enable)) {
        printf("fail to parse parameters 'mode'\n");
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Entry削除コマンドオプション設定処理関数
//!
//! ME6E-PR Entry削除コマンドのオプションを設定する。
//!
//! @param [in]  opt1       MACアドレスの文字列
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool me6e_command_pr_del_option(
        const char * opt1,
        struct me6e_command_t* command
)
{

    // 引数チェック
    if ((opt1 == NULL) || (command == NULL)){
        printf("Parameter Check NG.\n");
        return false;
    }

    // opt1:mac address
    if (!parse_macaddress(opt1, &command->req.pr.macaddr)) {
        printf("fail to parse parameters 'macaddress'\n");
        return false;
    }

    return true;

}


///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Entry活性化コマンドオプション設定処理関数
//!
//! ME6E-PR Entry活性化コマンドのオプションを設定する。
//!
//! @param [in]  opt1       MACアドレスの文字列
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool me6e_command_pr_enable_option(
        const char * opt1,
        struct me6e_command_t* command
)
{
    // 引数チェック
    if ((opt1 == NULL) || (command == NULL)){
        printf("Parameter Check NG.\n");
        return false;
    }

    // opt1:mac address
    if (!parse_macaddress(opt1, &command->req.pr.macaddr)) {
        printf("fail to parse parameters 'macaddress'\n");
        return false;
    }

    // コマンドデータにenableをセット
    command->req.pr.enable = true;

    return true;

}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Entry非活性化コマンドオプション設定処理関数
//!
//! ME6E-PR Entry非活性化コマンドのオプションを設定する。
//!
//! @param [in]  opt1       MACアドレスの文字列
//! @param [out] command    コマンド構造体
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool me6e_command_pr_disable_option(
        const char * opt1,
        struct me6e_command_t* command
)
{
    // 引数チェック
    if ((opt1 == NULL) || (command == NULL)){
        printf("Parameter Check NG.\n");
        return false;
    }

    // opt1:mac address
    if (!parse_macaddress(opt1, &command->req.pr.macaddr)) {
        printf("fail to parse parameters 'macaddress'\n");
        return false;
    }

    // コマンドデータにdisableをセット
    command->req.pr.enable = false;

    return true;
}



///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Commandの読み込み処理関数
//!
//! ME6E-PR ファイル内のCommand行の読み込み処理を行う。
//!
//! @param [in]  filename   Commandファイル名
//! @param [in]  command    コマンド構造体
//! @param [in]  name       Plane Name
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool me6e_command_pr_load_option(
        const char * filename,
        struct me6e_command_t* command,
        char * name
)
{

    FILE*       fp = NULL;
    char        line[OPT_LINE_MAX] = {0};
    uint32_t    line_cnt = 0;
    bool        result = true;
    char*       cmd_opt[DYNAMIC_OPE_ARGS_NUM_MAX] = { "" };
    int         cmd_num = 0;


    // 引数チェック
    if( (filename == NULL) || (strlen(filename) == 0) ||
            (command == NULL) || (name == NULL)){
        printf("internal error\n");
        return false;
    }

    // 設定ファイルオープン
    fp = fopen(filename, "r");
    if(fp == NULL) {
        printf("No such file : %s\n", filename);
        return false;
    }

    // 一行ずつ読み込み
    while(fgets(line, sizeof(line), fp) != NULL) {

        // ラインカウンタ
        line_cnt++;

        // 改行文字を終端文字に置き換える
        line[strlen(line)-1] = '\0';
        if (line[strlen(line)-1] == '\r') {
            line[strlen(line)-1] = '\0';
        }

        // コメント行と空行はスキップ
        if((line[0] == '#') || (strlen(line) == 0)){
            continue;
        }

        // コマンドオプションの初期化
        cmd_num = 0;
        cmd_opt[0] = "";
        cmd_opt[1] = "";
        cmd_opt[2] = "";
        cmd_opt[3] = "";
        cmd_opt[4] = "";
        cmd_opt[5] = "";

        /* コマンド行の解析 */
        result = me6e_command_parse_pr_file(line, &cmd_num, cmd_opt);
        if (!result) {
            printf("internal error\n");
            result = false;
            break;
        }
        else if (cmd_num == 0) {
            // スペースとタブからなる行のためスキップ
            _D_(printf("スペースとタブからなる行のためスキップ\n");)
            continue;
        }

        /* コマンドのパラメータチェック */
        command->code = ME6E_COMMAND_MAX;
        for(int i=0; opt_args[i].main != NULL; i++)
        {
            if(!strcmp(cmd_opt[0], opt_args[i].main) && !strcmp(cmd_opt[1], opt_args[i].sub)){
                command->code = opt_args[i].code;
                break;
            }
        }

        // 未対応コマンドチェック
        if(command->code == ME6E_COMMAND_MAX){
            printf("Line%d : %s %s is invalid command\n", line_cnt, cmd_opt[0], cmd_opt[1]);
            result = false;
            break;
        }

        // PR-Entry追加コマンド処理
        if (command->code == ME6E_ADD_PR) {
            _D_(printf("ME6E_ADD_PR_ENTRY\n");)
                if ( (cmd_num != 5) ) {
                    printf("Line%d : %s %s is invalid command\n", line_cnt, cmd_opt[0], cmd_opt[1]);
                    result = false;
                    break;
                }

            // PR-Entry追加コマンドオプションの設定
            result = me6e_command_pr_add_option((const char *)cmd_opt[2], (const char* )cmd_opt[3], (const char* )cmd_opt[4], command);
            if (!result) {
                printf("Line%d : %s %s is invalid command\n", line_cnt, cmd_opt[0], cmd_opt[1]);
                result = false;
                break;
            }
        }

        // PR-Entry削除コマンド処理
        if (command->code == ME6E_DEL_PR) {
            _D_(printf("ME6E_DEL_PR_ENTRY\n");)
                if (cmd_num != 3) {
                    printf("Line%d : %s %s is invalid command\n", line_cnt, cmd_opt[0], cmd_opt[1]);
                    result = false;
                    break;
                }

            // PR-Entry削除コマンドオプションの設定
            result = me6e_command_pr_del_option((const char *)cmd_opt[2], command);
            if (!result) {
                printf("Line%d : %s %s is invalid command\n", line_cnt, cmd_opt[0], cmd_opt[1]);
                result = false;
                break;
            }
        }

        // PR-Entry活性化コマンド処理
        if (command->code == ME6E_ENABLE_PR) {
            _D_(printf("ME6E_ENABLE_PR_ENTRY\n");)
                if (cmd_num != 3) {
                    printf("Line%d : %s %s is invalid command\n", line_cnt, cmd_opt[0], cmd_opt[1]);
                    result = false;
                    break;
                }

            // PR-Entry活性化コマンドオプションの設定
            result = me6e_command_pr_enable_option((const char *)cmd_opt[2], command);
            if (!result) {
                printf("Line%d : %s %s is invalid command\n", line_cnt, cmd_opt[0], cmd_opt[1]);
                result = false;
                break;
            }
        }

        // PR-Entry非活性化コマンド処理
        if (command->code == ME6E_DISABLE_PR) {
            _D_(printf("ME6E_DISABLE_PR_ENTRY\n");)
                if (cmd_num != 3) {
                    printf("Line%d : %s %s is invalid command\n", line_cnt, cmd_opt[0], cmd_opt[1]);
                    result = false;
                    break;
                }

            // PR-Entry非活性化コマンドオプションの設定
            result = me6e_command_pr_disable_option((const char *)cmd_opt[2], command);
            if (!result) {
                printf("Line%d : %s %s is invalid command\n", line_cnt, cmd_opt[0], cmd_opt[1]);
                result = false;
                break;
            }
        }

        /* コマンド送信 */
        result = me6e_command_pr_send(command, name);
        if (!result) {
            _D_(printf("me6e_command_pr_send NG\n");)
            result = false;
            break;
        }
    }

    fclose(fp);

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Command行の解析関数
//!
//! 引数で指定された行を、区切り文字「スペース または タブ」で
//! トークンに分解し、コマンドオプションとオプション数を格納する。
//!
//! @param [in]  line       コマンド行
//! @param [out] num        コマンドオプション数
//! @param [out] cmd_opt    コマンドオプション
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool me6e_command_parse_pr_file(char* line, int* num, char* cmd_opt[])
{
    char *tok = NULL;
    int cnt = 0;

    // 引数チェック
    if( (line == NULL) || (num == NULL) || (cmd_opt == NULL)){
        return false;
    }

    tok = strtok(line, DELIMITER);
    while( tok != NULL ) {
        cmd_opt[cnt] = tok;
        cnt++;
        tok = strtok(NULL, DELIMITER);  /* 2回目以降 */
    }

    *num = cnt;

#if 0
    printf("cnt = %d\n", cnt);
    for (int i = 0; i < cnt; i++) {
        printf( "%s\n", cmd_opt[i]);
    }
#endif

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ME6E-PR Commandファイル読み込み用コマンド送信処理関数
//!
//! ME6E-PR Commandファイルから読込んだコマンド行（1行単位）を送信する。
//!
//! @param [in]  command    コマンド構造体
//! @param [in]  name       Plane Name
//!
//! @retval true  正常終了
//! @retval false 異常終了
///////////////////////////////////////////////////////////////////////////////
bool me6e_command_pr_send(struct me6e_command_t* command, char* name)
{
    int     fd = -1;
    char    path[sizeof(((struct sockaddr_un*)0)->sun_path)] = {0};
    char*   offset = &path[1];

    // 引数チェック
    if( (command == NULL) || (name == NULL)) {
        return false;
    }

    sprintf(offset, ME6E_COMMAND_SOCK_NAME, name);

    fd = socket(PF_UNIX, SOCK_SEQPACKET, 0);
    if(fd < 0){
        printf("fail to open socket : %s\n", strerror(errno));
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));

    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, path, sizeof(addr.sun_path));

    if(connect(fd, (struct sockaddr*)&addr, sizeof(addr))){
        printf("fail to connect ME6E application(%s) : %s\n", name, strerror(errno));
        close(fd);
        return false;
    }

    int ret;
    int pty;
    char buf[256];

    ret = me6e_socket_send_cred(fd, command->code, &command->req, sizeof(command->req));
    if(ret <= 0){
        printf("fail to send command : %s\n", strerror(-ret));
        close(fd);
        return false;
    }

    ret = me6e_socket_recv(fd, &command->code, &command->res, sizeof(command->res), &pty);
    if(ret <= 0){
        printf("fail to receive response : %s\n", strerror(-ret));
        close(fd);
        return false;
    }
    if(command->res.result != 0){
        printf("receive error response : %s\n", strerror(command->res.result));
        close(fd);
        return false;
    }

    switch(command->code){
    case ME6E_ADD_PR:
    case ME6E_DEL_PR:
    case ME6E_ENABLE_PR:
    case ME6E_DISABLE_PR:
        // 出力結果がソケット経由で送信されてくるので、そのまま標準出力に書き込む
        while(1){
            ret = read(fd, buf, sizeof(buf));
            if(ret > 0){
                ret = write(STDOUT_FILENO, buf, ret);
            }
            else{
                break;
            }
        }
        close(fd);
        break;

    default:
        // ありえない
        close(fd);
        break;
    }

    return true;
}

