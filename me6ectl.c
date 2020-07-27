/******************************************************************************/
/* ファイル名 : me6ectl.c                                                     */
/* 機能概要   : ME6Eアクセス用外部コマンド ソースファイル                     */
/* 修正履歴   : 2013.01.24 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*            : 2016.06.01 H.Koganemaru PR機能の追加                          */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <sched.h>
#include <alloca.h>
#include <signal.h>
#include <getopt.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <netinet/in.h>

#include "me6eapp_command_data.h"
#include "me6eapp_command.h"
#include "me6eapp_socket.h"

//! コマンドオプション構造体
static const struct option options[] = {
    {"name",  required_argument, 0, 'n'},
    {"help",  no_argument,       0, 'h'},
    {"usage", no_argument,       0, 'h'},
    {0, 0, 0, 0}
};

//! コマンド引数構造体定義
struct command_arg {
    char* main;   ///< メインコマンド文字列
    char* sub;    ///< サブコマンド文字列
    int   code;   ///< コマンドコード
};

//! コマンド引数
static const struct command_arg command_args[] = {
    {"show",     "stat",  ME6E_SHOW_STATISTIC},
    {"show",     "conf",  ME6E_SHOW_CONF},
    {"show",     "arp",   ME6E_SHOW_ARP},
    {"show",     "ndp",   ME6E_SHOW_NDP},
    {"show",     "pr",    ME6E_SHOW_PR},
    {"add",      "arp",   ME6E_ADD_ARP},
    {"add",      "ndp",   ME6E_ADD_NDP},
    {"add",      "pr",    ME6E_ADD_PR},
    {"del",      "arp",   ME6E_DEL_ARP},
    {"del",      "ndp",   ME6E_DEL_NDP},
    {"del",      "pr",    ME6E_DEL_PR},
    {"enable",   "pr",    ME6E_ENABLE_PR},
    {"disable",  "pr",    ME6E_DISABLE_PR},
    {"delall",   "pr",    ME6E_DELALL_PR},
    {"load",     "pr",    ME6E_LOAD_PR},
    {"shutdown", "",      ME6E_SHUTDOWN},
    {NULL,       NULL,    ME6E_COMMAND_MAX}
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
static void usage(void)
{
    fprintf(stderr,
"Usage: me6ectl -n plane_name COMMAND \n"
"       me6ectl { -h | --help | --usage }\n"
"where  COMMAND := { show stat \n"
"                    show conf \n"
"                    show arp  \n"
"                    show ndp  \n"
"                    show pr   \n"
"                    add arp IPv4Addr MACAddr \n"
"                    add ndp IPv6Addr MACAddr \n"
"                    add pr  MACAddr IPv6Addr/Prefixlen mode \n"
"                    del arp IPv4Addr   \n"
"                    del ndp IPv6Addr   \n"
"                    del pr  MACAddr    \n"
"                    enable  pr MACAddr \n"
"                    disable pr MACAddr \n"
"                    delall pr          \n"
"                    load pr            \n"
"                    shutdown }\n"
"\n"
"  show stat  : Show the statistics information specified plane_name.\n"
"  show conf  : Show the configuration specified plane_name.\n"
"  show arp   : Show the Proxy ARP table specified plane_name.\n"
"  show ndp   : Show the Proxy NDP table specified plane_name.\n"
"  show pr    : Show the ME6E-PR table specified plane_name.\n"
"  add arp    : Add the ARP entry to the Proxy ARP tablen specified plane_name.\n"
"  add ndp    : Add the NDP entry to the Proxy NDP table specified plane_name.\n"
"  add pr     : Add the PR entry to the ME6E-PR table specified plane_name.\n"
"  del arp    : Delete the ARP entry from the Proxy ARP table specified plane_name.\n"
"  del ndp    : Delete the NDP entry from the Proxy NDP table specified plane_name.\n"
"  del pr     : Delete the PR entry from the ME6E-PR table specified plane_name.\n"
"  delall pr  : Delete the all PR entry from the ME6E-PR table specified plane_name.\n"
"  enable pr  : Delete the PR entry from the ME6E-PR table specified plane_name.\n"
"  disable pr : Delete the PR entry from the ME6E-PR table specified plane_name.\n"
"  load pr    : Load ME6E-PR Command file specified PLANE_NAME\n"
"  shutdown   : Shutting down the application specified plane_name.\n"
"\n"
    );

    return;
}

////////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
// メイン関数
////////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    char* name         = NULL;
    int   option_index = 0;
    bool  result = false;

    // 引数チェック
    while (1) {
        int c = getopt_long(argc, argv, "n:h", options, &option_index);
        if (c == -1){
            break;
        }

        switch (c) {
        case 'n':
            name = optarg;
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

    if(name == NULL){
        usage();
        exit(EINVAL);
    }

    if(argc <= optind){
        usage();
        exit(EINVAL);
    }

    char* cmd_main = argv[optind];
    char* cmd_sub  = (argc > (optind+1)) ? argv[optind+1] : "";
    char* cmd_opt1 = (argc > (optind+2)) ? argv[optind+2] : "";
    char* cmd_opt2 = (argc > (optind+3)) ? argv[optind+3] : "";
    char* cmd_opt3 = (argc > (optind+4)) ? argv[optind+4] : "";

    struct me6e_command_t command = {0};
    command.code = ME6E_COMMAND_MAX;
    for(int i=0; command_args[i].main != NULL; i++){
        if(!strcmp(cmd_main, command_args[i].main) && !strcmp(cmd_sub, command_args[i].sub)){
            command.code = command_args[i].code;
            break;
        }
    }

    if(command.code == ME6E_COMMAND_MAX){
        usage();
        exit(EINVAL);
    }

    if (command.code == ME6E_ADD_ARP) {
        if (argc != 7) {
            usage();
            exit(EINVAL);
        }

        result = me6e_command_arp_set_option(cmd_opt1, cmd_opt2, &command);
        if (!result) {
            exit(EINVAL);
        }
    }

    if (command.code == ME6E_DEL_ARP) {
        if (argc != 6) {
            usage();
            exit(EINVAL);
        }

        result = me6e_command_arp_set_option(cmd_opt1, cmd_opt2, &command);
        if (!result) {
            exit(EINVAL);
        }
    }

    if (command.code == ME6E_ADD_NDP) {
        if (argc != 7) {
            usage();
            exit(EINVAL);
        }

        result = me6e_command_ndp_set_option(cmd_opt1, cmd_opt2, &command);
        if (!result) {
            exit(EINVAL);
        }
    }

    if (command.code == ME6E_DEL_NDP) {
        if (argc != 6) {
            usage();
            exit(EINVAL);
        }

        result = me6e_command_ndp_set_option(cmd_opt1, cmd_opt2, &command);
        if (!result) {
            exit(EINVAL);
        }
    }

    if (command.code == ME6E_ADD_PR) {
        if ((argc != 8)) {
            usage();
            exit(EINVAL);
        }

        result = me6e_command_pr_add_option(cmd_opt1, cmd_opt2, cmd_opt3, &command);
        if (!result) {
            usage();
            exit(EINVAL);
        }
    }

    if (command.code == ME6E_DEL_PR) {
        if (argc != 6)  {
            usage();
            exit(EINVAL);
        }

        result = me6e_command_pr_del_option(cmd_opt1, &command);
        if (!result) {
            usage();
            exit(EINVAL);
        }
    }

    if (command.code == ME6E_DELALL_PR) {
        if (argc != 5)  {
            usage();
            exit(EINVAL);
        }
    }

    if (command.code == ME6E_ENABLE_PR) {
        if (argc != 6)  {
            usage();
            exit(EINVAL);
        }

        result = me6e_command_pr_enable_option(cmd_opt1, &command);
        if (!result) {
            usage();
            exit(EINVAL);
        }
    }

    if (command.code == ME6E_DISABLE_PR) {
        if (argc != 6)  {
            usage();
            exit(EINVAL);
        }

        result = me6e_command_pr_disable_option(cmd_opt1, &command);
        if (!result) {
            usage();
            exit(EINVAL);
        }
    }

    if (command.code == ME6E_SHOW_PR) {
        if (argc != 5)  {
            usage();
            exit(EINVAL);
        }
    }

    if (command.code == ME6E_LOAD_PR) {
        if (argc != 6) {
            usage();
            exit(EINVAL);
        }

        result = me6e_command_pr_load_option(cmd_opt1, &command, name);
        if (!result) {
            usage();
            exit(EINVAL);
        }

        return 0;
    }

    int fd;
    char path[sizeof(((struct sockaddr_un*)0)->sun_path)] = {0};
    char* offset = &path[1];

    sprintf(offset, ME6E_COMMAND_SOCK_NAME, name);

    fd = socket(PF_UNIX, SOCK_SEQPACKET, 0);
    if(fd < 0){
        printf("fail to open socket : %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));

    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, path, sizeof(addr.sun_path));

    if(connect(fd, (struct sockaddr*)&addr, sizeof(addr))){
        printf("fail to connect ME6E application(%s) : %s\n", name, strerror(errno));
        close(fd);
        return -1;
    }

    int ret;
    int pty;
    char buf[256];

    ret = me6e_socket_send_cred(fd, command.code, &command.req, sizeof(command.req));
    if(ret <= 0){
        printf("fail to send command : %s\n", strerror(-ret));
        close(fd);
        return -1;
    }

    ret = me6e_socket_recv(fd, &command.code, &command.res, sizeof(command.res), &pty);
    if(ret <= 0){
        printf("fail to receive response : %s\n", strerror(-ret));
        close(fd);
        return -1;
    }
    if(command.res.result != 0){
        printf("receive error response : %s\n", strerror(command.res.result));
        close(fd);
        return -1;
    }

    switch(command.code){
    case ME6E_SHOW_CONF:
    case ME6E_SHOW_STATISTIC:
    case ME6E_SHOW_ARP:
    case ME6E_SHOW_NDP:
    case ME6E_ADD_ARP:
    case ME6E_DEL_ARP:
    case ME6E_ADD_NDP:
    case ME6E_DEL_NDP:
    case ME6E_ADD_PR:
    case ME6E_DEL_PR:
    case ME6E_SHOW_PR:
    case ME6E_ENABLE_PR:
    case ME6E_DISABLE_PR:
    case ME6E_LOAD_PR:
    case ME6E_DELALL_PR:

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

    case ME6E_SHUTDOWN:
        close(fd);
        break;

    default:
        // ありえない
        close(fd);
        break;
    }

    return 0;
}

