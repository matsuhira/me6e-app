/******************************************************************************/
/* ファイル名 : me6eapp_print_packet.c                                        */
/* 機能概要   : デバッグ用パケット表示関数 ソースファイル                     */
/* 修正履歴   : 2013.01.10 Y.Shibata 新規作成                                 */
/*            : 2016.04.15 H.Koganemaru 名称変更に伴う修正                    */
/*                                                                            */
/* ALL RIGHTS RESERVED, COPYRIGHT(C) FUJITSU LIMITED 2013-2016                */
/******************************************************************************/
#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <net/ethernet.h>
#include <netinet/ether.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <ctype.h>

#include "me6eapp.h"
#include "me6eapp_print_packet.h"
#include "me6eapp_log.h"

// デバッグ用マクロ
#ifdef DEBUG
#define _D_(x) x
#else
#define _D_(x)
#endif

////////////////////////////////////////////////////////////////////////////////
// 内部関数プロトタイプ宣言
////////////////////////////////////////////////////////////////////////////////
static void print_ipv4header(char* frame);
static void print_ipv6header(char* frame);
static void print_tcpheader(char* frame);
static void print_udpheader(char* frame);
static void print_icmpheader(char* frame);
static void print_icmpv6header(char* frame);
static void print_arp(char* frame);

///////////////////////////////////////////////////////////////////////////////
//! @brief パケット表示関数
//!
//! Etherフレームからのパケットをテキストで表示する。
//!
//! @param [in]  packet     表示するパケットの先頭ポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void me6e_print_packet(char* packet)
{
    struct ethhdr* p_ether = (struct ethhdr*)packet;
    int type = ntohs(p_ether->h_proto);  /* Ethernetタイプ */
    char macaddrstr[MAC_ADDRSTRLEN] = { 0 };

    if (type <= 1500) {
        DEBUG_LOG("IEEE 802.3 Ethernet Frame:\n");
    } else {
        DEBUG_LOG("Ethernet Frame:\n");
    }

    DEBUG_LOG("+-------------------------+-------------------------+\n");
    DEBUG_LOG("| Destination MAC Address:"
         "         %17s|\n", ether_ntoa_r((struct ether_addr*)&p_ether->h_dest, macaddrstr));
    DEBUG_LOG("+-------------------------+-------------------------+\n");
    DEBUG_LOG("| Source MAC Address:     "
         "         %17s|\n", ether_ntoa_r((struct ether_addr*)&p_ether->h_source, macaddrstr));
    DEBUG_LOG("+-------------------------+-------------------------+\n");
    if (type < 1500) {
        DEBUG_LOG("| Length:            %5u|\n", type);
    } else {
        DEBUG_LOG("| Ethernet Type:    0x%04x|\n", type);
    }

    DEBUG_LOG("+-------------------------+\n");

    if(type == ETH_P_IPV6){
        print_ipv6header(packet + sizeof(struct ethhdr));
    } else if(type == ETH_P_IP){
        print_ipv4header(packet + sizeof(struct ethhdr));
    } else if(type == ETH_P_ARP){
        print_arp(packet + sizeof(struct ethhdr));
    } else{
        DEBUG_LOG("Other Ether Type\n");
    }
}

///////////////////////////////////////////////////////////////////////////////
//! @brief パケット表示関数
//!
//! Etherフレームからのパケットをテキストで表示する。
//!
//! @param [in]  packet     表示するパケットの先頭ポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
const int width = 16;            // 1行のダンプサイズ
const int binwidth = 4;          // 1行の区切りサイズ

void me6eapp_hex_dump(const char *buf, size_t bufsize)
{
    int num,i;
    int linesize;                  // 1行の表示対象データサイズ
    unsigned char binbuf[width];   // 1行に表示するバイナリデータ格納域
    unsigned char charbuf[width];  // 1行に表示するキャラクタデータ格納域

    // タイトル行
    printf("ADDRESS  0");
    for(i= 0; i < width; i++){
        printf("--");
        if(((i+1) % binwidth) == 0){
            if((i+1) == width){
                printf("%d CHARACTER\n",width);
            }else{
                printf("+");
            }
        }
    }

    // ダンプ出力
    for(num = 0; num < bufsize; num += width){
        //1行の表示対象データサイズを計算
        if((bufsize - num - width ) < 0){
            linesize = (bufsize - num) % width;
        }else{
            linesize = width;
        }

        // 出力用データ加工
        for(i=0; i < width; i++){
            if( linesize > i){
                if( buf[i] < 0x20 || buf[i] > 0x7e ){
                    // 非可読文字なら.に置換
                    charbuf[i] = '.';
                }else{
                    charbuf[i] = *(buf+i);
                }
                binbuf[i] = *(buf+i);
            }else{
                // linsizeがwidth未満ならスペースに置換
                binbuf[i] = ' ' ;
                charbuf[i] = ' ';
            }
        }

        // アドレス出力
        printf("%08x ", num);

        // バイナリ部出力
        for(i= 0; i < width; i++){
            if( ((i) % binwidth) == 0){
                printf(" ");
            }
            if((bufsize - num ) > i ){
                printf("%02x", binbuf[i]);
            }else{
                printf("  ");
            }
        }

        // キャラクタ部出力
        printf("  *");
        for(i= 0; i < width; i++){
            printf("%c",charbuf[i]);
        }
        printf("*\n");

        buf += width;
    }
}


///////////////////////////////////////////////////////////////////////////////
//! @brief IPv4パケット表示関数
//!
//! IPv4パケットをテキストで表示する。
//!
//! @param [in]  frame     表示するパケットの先頭ポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void print_ipv4header(char* frame)
{
    struct  iphdr*    p_ip;
    struct  protoent* p_proto;
    struct  in_addr   insaddr;
    struct  in_addr   indaddr;
    char    addr[INET6_ADDRSTRLEN] = { 0 };

    p_ip = (struct iphdr*)(frame);

    insaddr.s_addr = p_ip->saddr;
    indaddr.s_addr = p_ip->daddr;

    DEBUG_LOG("----IP Header--------------------\n");
    DEBUG_LOG("version : %u\n",    p_ip->version);
    DEBUG_LOG("ihl : %u\n",        p_ip->ihl);
    DEBUG_LOG("tos : %u\n",        p_ip->tos);
    DEBUG_LOG("tot length : %u\n", ntohs(p_ip->tot_len));
    DEBUG_LOG("id : %u\n",         ntohs(p_ip->id));
    DEBUG_LOG("frag_off : %04x\n", htons(p_ip->frag_off));
    DEBUG_LOG("ttl : %u\n",        p_ip->ttl);
    if((p_proto = getprotobynumber(p_ip->protocol)) != NULL) {
        DEBUG_LOG("protocol : %x(%s)\n", p_ip->protocol, p_ip->protocol ? p_proto->p_name : "hopopt");
    }
    else {
        DEBUG_LOG("protocol : %x(unknown)\n", p_ip->protocol);
    }
    DEBUG_LOG("check : 0x%x\n", ntohs(p_ip->check));
    DEBUG_LOG("saddr : %s\n",   inet_ntop(AF_INET, &insaddr, addr, sizeof(addr)));
    DEBUG_LOG("daddr : %s\n",   inet_ntop(AF_INET, &indaddr, addr, sizeof(addr)));

    if(p_ip->protocol == IPPROTO_ICMP){
        print_icmpheader(frame + sizeof(struct iphdr));
    }
    if(p_ip->protocol == IPPROTO_TCP){
        print_tcpheader(frame + sizeof(struct iphdr));
    }
    if(p_ip->protocol == IPPROTO_UDP){
        print_udpheader(frame + sizeof(struct iphdr));
    }
}

///////////////////////////////////////////////////////////////////////////////
//! @brief IPv6パケット表示関数
//!
//! IPv6パケットをテキストで表示する。
//!
//! @param [in]  frame     表示するパケットの先頭ポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void print_ipv6header(char* frame)
{
    struct ip6_hdr*  p_ip;
    struct protoent* p_proto;
    char   saddr[INET6_ADDRSTRLEN] = { 0 };
    char   daddr[INET6_ADDRSTRLEN] = { 0 };

    p_ip = (struct ip6_hdr*)(frame);

    DEBUG_LOG("----IPv6 Header--------------------\n");
    DEBUG_LOG("version : %u\n",       p_ip->ip6_vfc >> 4);
    DEBUG_LOG("traffic class : %x\n", p_ip->ip6_vfc & 0x0f);
    DEBUG_LOG("flow label : %05x\n",  ntohl(p_ip->ip6_flow) & 0x000fffff);
    DEBUG_LOG("payload_len : %u\n",   ntohs(p_ip->ip6_plen));
    DEBUG_LOG("hop_limit : %u\n",     p_ip->ip6_hops);
    if((p_proto = getprotobynumber(p_ip->ip6_nxt)) != NULL) {
        DEBUG_LOG("protocol : %x(%s)\n", p_ip->ip6_nxt, p_ip->ip6_nxt ? p_proto->p_name : "hopopt");
    }
    else {
        DEBUG_LOG("protocol : %x(unknown)\n", p_ip->ip6_nxt);
    }
    DEBUG_LOG("saddr : %s\n",inet_ntop(AF_INET6, &p_ip->ip6_src, saddr, INET6_ADDRSTRLEN));
    DEBUG_LOG("daddr : %s\n",inet_ntop(AF_INET6, &p_ip->ip6_dst, daddr, INET6_ADDRSTRLEN));

    if(p_ip->ip6_nxt == IPPROTO_TCP){
        print_tcpheader(frame + sizeof(struct ip6_hdr));
    }
    if(p_ip->ip6_nxt == IPPROTO_UDP){
        print_udpheader(frame + sizeof(struct ip6_hdr));
    }
    if(p_ip->ip6_nxt == IPPROTO_IPIP){
        print_ipv4header(frame + sizeof(struct ip6_hdr));
    }
    if(p_ip->ip6_nxt == IPPROTO_ICMPV6){
        print_icmpv6header(frame + sizeof(struct ip6_hdr));
    }
    DEBUG_LOG("\n");
}

///////////////////////////////////////////////////////////////////////////////
//! @brief TCPパケット表示関数
//!
//! TCPパケットをテキストで表示する。
//!
//! @param [in]  frame     表示するパケットの先頭ポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void print_tcpheader(char* frame)
{
    struct tcphdr* p_tcp;
    char   tmp[256] = { 0 };

    p_tcp = (struct tcphdr*)(frame);

    p_tcp->fin ? strcat(tmp, " FIN") : 0 ;
    p_tcp->syn ? strcat(tmp, " SYN") : 0 ;
    p_tcp->rst ? strcat(tmp, " RST") : 0 ;
    p_tcp->psh ? strcat(tmp, " PSH") : 0 ;
    p_tcp->ack ? strcat(tmp, " ACK") : 0 ;
    p_tcp->urg ? strcat(tmp, " URG") : 0 ;

    DEBUG_LOG("----TCP Header-------------------\n");
    DEBUG_LOG("source port : %u\n", ntohs(p_tcp->source));
    DEBUG_LOG("dest port : %u\n",   ntohs(p_tcp->dest));
    DEBUG_LOG("sequence : %u\n",    ntohl(p_tcp->seq));
    DEBUG_LOG("ack seq : %u\n",     ntohl(p_tcp->ack_seq));
    DEBUG_LOG("frags :%s\n", tmp);
    DEBUG_LOG("window : %u\n",  ntohs(p_tcp->window));
    DEBUG_LOG("check : 0x%x\n", ntohs(p_tcp->check));
    DEBUG_LOG("urt_ptr : %u\n", p_tcp->urg_ptr);
}

///////////////////////////////////////////////////////////////////////////////
//! @brief UDPパケット表示関数
//!
//! UDPパケットをテキストで表示する。
//!
//! @param [in]  frame     表示するパケットの先頭ポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void print_udpheader(char* frame)
{
    struct udphdr* p_udp;

    p_udp = (struct udphdr*)(frame);

    DEBUG_LOG("----UDP Header-------------------\n");
    DEBUG_LOG("source port : %u\n", ntohs(p_udp->source));
    DEBUG_LOG("dest port : %u\n",   ntohs(p_udp->dest));
    DEBUG_LOG("length : %u\n",      ntohs(p_udp->len));
    DEBUG_LOG("check : 0x%x\n",     ntohs(p_udp->check));
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ICMPv4パケット表示関数
//!
//! ICMPv4パケットをテキストで表示する。
//!
//! @param [in]  frame     表示するパケットの先頭ポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void print_icmpheader(char* frame)
{
    struct icmphdr* p_icmp;
    char   tmp[256];

    p_icmp = (struct icmphdr*)(frame);

    switch(p_icmp->type){
    case ICMP_ECHOREPLY:
        sprintf(tmp, "Echo Reply");
        break;
    case ICMP_DEST_UNREACH:
        sprintf(tmp, "Destination Unreachable");
        break;
    case ICMP_SOURCE_QUENCH:
        sprintf(tmp, "Source Quench");
        break;
    case ICMP_REDIRECT:
        sprintf(tmp, "Redirect (change route)");
        break;
    case ICMP_ECHO:
        sprintf(tmp, "Echo Request");
        break;
    case ICMP_TIME_EXCEEDED:
        sprintf(tmp, "Time Exceeded");
        break;
    case ICMP_PARAMETERPROB:
        sprintf(tmp, "Parameter Problem");
        break;
    case ICMP_TIMESTAMP:
        sprintf(tmp, "Timestamp Request");
        break;
    case ICMP_TIMESTAMPREPLY:
        sprintf(tmp, "Timestamp Reply");
        break;
    case ICMP_INFO_REQUEST:
        sprintf(tmp, "Information Request");
        break;
    case ICMP_INFO_REPLY:
        sprintf(tmp, "Information Reply");
        break;
    case ICMP_ADDRESS:
        sprintf(tmp, "Address Mask Request");
        break;
    case ICMP_ADDRESSREPLY:
        sprintf(tmp, "Address Mask Reply");
        break;
    default:
        sprintf(tmp, "unknown");
        break;
    }

    DEBUG_LOG("----ICMP Header------------------\n");
    DEBUG_LOG("type : %u(%s)\n", p_icmp->type, tmp);
    DEBUG_LOG("code : %u\n",       p_icmp->code);
    DEBUG_LOG("checksum : 0x%x\n", ntohs(p_icmp->checksum));

    if(p_icmp->type == ICMP_DEST_UNREACH){
        print_ipv4header(frame + sizeof(struct icmphdr));
    }
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ICMPv6パケット表示関数
//!
//! ICMPv6パケットをテキストで表示する。
//!
//! @param [in]  frame     表示するパケットの先頭ポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void print_icmpv6header(char* frame)
{
    struct icmp6_hdr* p_icmp;
    char   tmp[256];

    p_icmp = (struct icmp6_hdr*)(frame);

    switch(p_icmp->icmp6_type){
    case ICMP6_DST_UNREACH:
        sprintf(tmp, "Destination Unreachable");
        break;
    case ICMP6_PACKET_TOO_BIG:
        sprintf(tmp, "Packet too Big");
        break;
    case ICMP6_TIME_EXCEEDED:
        sprintf(tmp, "Time Exceeded");
        break;
    case ICMP6_PARAM_PROB:
        sprintf(tmp, "Parameter Problem");
        break;
    case ICMP6_ECHO_REQUEST:
        sprintf(tmp, "Echo Request");
        break;
    case ICMP6_ECHO_REPLY:
        sprintf(tmp, "Echo Reply");
        break;
    case MLD_LISTENER_QUERY:
        sprintf(tmp, "Multicast Listener Query");
        break;
    case MLD_LISTENER_REPORT:
        sprintf(tmp, "Multicast Listener Report");
        break;
    case MLD_LISTENER_REDUCTION:
        sprintf(tmp, "Multicast Listener Done");
        break;
    case ND_ROUTER_SOLICIT:
        sprintf(tmp, "router solicitation");
        break;
    case ND_ROUTER_ADVERT:
        sprintf(tmp, "router advertisement");
        break;
    case ND_NEIGHBOR_SOLICIT:
        sprintf(tmp, "Neighbor solicitation");
        break;
    case ND_NEIGHBOR_ADVERT:
        sprintf(tmp, "Neighbor advertisement");
        break;
    case ND_REDIRECT:
        sprintf(tmp, "redirect");
        break;
    case ICMP6_ROUTER_RENUMBERING:
        sprintf(tmp, "Router Renumber");
        break;
    default:
        sprintf(tmp, "unknown");
        break;
    }

    DEBUG_LOG("----ICMPV6 Header------------------\n");
    DEBUG_LOG("type : %u(%s)\n", p_icmp->icmp6_type, tmp);
    DEBUG_LOG("code : %u\n",       p_icmp->icmp6_code);
    DEBUG_LOG("checksum : 0x%x\n", ntohs(p_icmp->icmp6_cksum));

    if(p_icmp->icmp6_type == ND_NEIGHBOR_SOLICIT){
        struct nd_neighbor_solicit  *ns = (struct nd_neighbor_solicit *)(frame);
        struct in6_addr* targetaddr = (struct in6_addr *)&(ns->nd_ns_target);
        char address[INET6_ADDRSTRLEN] = { 0 };
        DEBUG_LOG("target addr : %s\n", inet_ntop(AF_INET6, targetaddr, address, sizeof(address)));
    }

    if(p_icmp->icmp6_type == ND_NEIGHBOR_ADVERT){
        struct nd_neighbor_advert* na = (struct nd_neighbor_advert*)(frame);
        struct nd_opt_hdr* opthdr = (struct nd_opt_hdr *)(frame + sizeof(struct nd_neighbor_advert));
        void* optdata = (char *)(opthdr + sizeof(struct nd_opt_hdr));
        struct in6_addr* targetaddr = (struct in6_addr *)&(na->nd_na_target);
        char address[INET6_ADDRSTRLEN] = { 0 };
        char macaddrstr[MAC_ADDRSTRLEN] = { 0 };

        DEBUG_LOG("target addr : %s\n", inet_ntop(AF_INET6, targetaddr, address, sizeof(address)));
        DEBUG_LOG("target macaddr : %s\n", ether_ntoa_r((struct ether_addr*)(optdata), macaddrstr));
    }
}

///////////////////////////////////////////////////////////////////////////////
//! @brief ARPパケット表示関数
//!
//! ARPパケットをテキストで表示する。
//!
//! @param [in]  frame     表示するパケットの先頭ポインタ
//!
//! @return なし
///////////////////////////////////////////////////////////////////////////////
void print_arp(char * frame)
{
    static char *arp_op_name[] = {
        "Undefine",
        "(ARP Request)",
        "(ARP Reply)",
        "(RARP Request)",
        "(RARP Reply)"
    };   /* オペレーションの種類を表す文字列 */
    #define ARP_OP_MAX (sizeof arp_op_name / sizeof arp_op_name[0])

    struct ether_arp *arp = (struct ether_arp *)frame;
    int op = ntohs(arp->ea_hdr.ar_op);  /* ARPオペレーション */
    struct in_addr *src = (struct in_addr *)(arp->arp_spa);
    struct in_addr *dst = (struct in_addr *)(arp->arp_tpa);
    char macaddrstr[MAC_ADDRSTRLEN] = { 0 };
    char addr[INET6_ADDRSTRLEN] = { 0 };

    if (op < 0 || ARP_OP_MAX < op) {
        op = 0;
    }

    DEBUG_LOG("Protocol: ARP\n");
    DEBUG_LOG("+-------------------------+-------------------------+\n");
    DEBUG_LOG("| Hard Type: %2u%-11s| Protocol:0x%04x%-9s|\n",
         ntohs(arp->ea_hdr.ar_hrd),
         (ntohs(arp->ea_hdr.ar_hrd)==ARPHRD_ETHER)?"(Ethernet)":"(Not Ether)",
         ntohs(arp->ea_hdr.ar_pro),
         (ntohs(arp->ea_hdr.ar_pro)==ETHERTYPE_IP)?"(IP)":"(Not IP)");
    DEBUG_LOG("+------------+------------+-------------------------+\n");
    DEBUG_LOG("| HardLen:%3u| Addr Len:%2u| OP: %4d%16s|\n",
         arp->ea_hdr.ar_hln, arp->ea_hdr.ar_pln, ntohs(arp->ea_hdr.ar_op),
         arp_op_name[op]);
    DEBUG_LOG("+------------+------------+-------------------------+\n");
    DEBUG_LOG("| Source MAC Address:     "
         "         %17s|\n", ether_ntoa_r((struct ether_addr*)(arp->arp_sha), macaddrstr));
    DEBUG_LOG("+-------------------------+-------------------------+\n");
    DEBUG_LOG("| Source IP Address:                 %15s|\n",
         inet_ntop(AF_INET, src, addr, sizeof(addr)));
    DEBUG_LOG("+-------------------------+-------------------------+\n");
    DEBUG_LOG("| Destination MAC Address:"
         "         %17s|\n", ether_ntoa_r((struct ether_addr*)(arp->arp_tha), macaddrstr));
    DEBUG_LOG("+-------------------------+-------------------------+\n");
    DEBUG_LOG("| Destination IP Address:            %15s|\n",
         inet_ntop(AF_INET, dst, addr, sizeof(addr)));
    DEBUG_LOG("+---------------------------------------------------+\n");
}

