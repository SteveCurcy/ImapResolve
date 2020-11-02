#pragma once
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "ImapResolve.h"


/* obsolete */
#define BUF_SIZE 10240
#define STR_SIZE 1024
#define NO_INPUT -1
#define SUCESS   -3
#define NO_MAC   -4
#define NO_IP    -5
#define NO_TCP   -6
#define NO_PCAP  -7
#define FILE_OPEN_ERR   -2
/* end of obsolete */

/* IMAP 返回字符串的分析 */
#define BAD_ANSWER  1
#define OK_OTHER    2
#define FETCH_HEADERS   3
#define FETCH_BODY  4
#define FETCH_OTHER 5

#define ETHER_HEAD      14
#define LINUX_COOKED_CAPTURE_HEAD 16

/* Protocol Type */
#define ETHERNET    1
#define LINUXCOOKED 113

/* 接收邮件的状态 */
#define READY   1
#define RECEIVE 2

typedef int32_t     bpf_int32;
typedef u_int32_t   bpf_u_int32;
typedef u_int16_t   u_short;
typedef u_int32_t   u_int32;
typedef u_int16_t   u_int16;
typedef u_int8_t    u_int8;

/* 时间戳 */
typedef struct time_val {
    int tv_sec;         /* seconds 含义同 time_t 对象的值 */
    int tv_usec;        /* and microseconds */
}time_val;

/* pcap 文件头结构体 */
typedef struct pcap_file_header {
    bpf_u_int32 magic;          /* 0xa1b2c3d4 */
    u_short     version_major;  /* magjor Version 2 */
    u_short     version_minor;  /* minor Version 4 */
    bpf_int32   thiszone;       /* gmt to local correction */
    bpf_u_int32 sigfigs;        /* accuracy of timestamps */
    bpf_u_int32 snaplen;        /* max length saved portion of each pkt */
    bpf_u_int32 linktype;       /* data link type (LINKTYPE_*) */
} pcap_file_header;

/* pcap数据包头结构体 */
typedef struct pcap_pkthdr {
    time_val    ts;     /* time stamp */
    bpf_u_int32 caplen; /* length of portion present */
    bpf_u_int32 len;    /* length this packet (off wire) */
}pcap_pkthdr;

/* 数据帧头 */
typedef struct FrameHeader_t {   /* Pcap捕获的数据帧头*/ 
    u_int8  DstMAC[6];          /* 目的MAC地址 */
    u_int8  SrcMAC[6];          /* 源MAC地址 */
    u_short FrameType;          /* 帧类型 */
} FrameHeader_t;
 
/* IP数据报头 */
typedef struct IPHeader_t {
    u_int8  Ver_HLen;       /* 版本+报头长度 */
    u_int8  TOS;            /* 服务类型 */
    u_int16 TotalLen;       /* 总长度 */
    u_int16 ID;             /* 标识 */
    u_int16 Flag_Segment;   /* 标志+片偏移 */
    u_int8  TTL;            /* 生存周期 */
    u_int8  Protocol;       /* 协议类型 */
    u_int16 Checksum;       /* 头部校验和 */
    u_int32 SrcIP;          /* 源IP地址 */
    u_int32 DstIP;          /* 目的IP地址 */
} IPHeader_t;
 
//TCP数据报头
typedef struct TCPHeader_t {/* TCP数据报头 */
    u_int16 SrcPort;        /* 源端口 */
    u_int16 DstPort;        /* 目的端口 */
    u_int32 SeqNO;          /* 序号 */
    u_int32 AckNO;          /* 确认号 */
    u_int8  HeaderLen;      /* 数据报头的长度(4 bit) + 保留(4 bit) */
    u_int8  Flags;          /* 保留(2 bit) + 标识TCP不同的控制消息(6 bit) */
    u_int16 Window;         /* 窗口大小 */
    u_int16 Checksum;       /* 校验和 */
    u_int16 UrgentPointer;  /* 紧急指针 */
} TCPHeader_t;

struct sock {
    u_int32 IP_fir, IP_sec;
    u_int16 Port_fir, Port_sec;
    sock(u_int32 FIP, u_int32 SIP, u_int16 FPort, u_int16 SPort);
    bool operator<(const sock& obj) const;
    bool operator==(const sock& obj) const;
};

class Package {
    int LinkLen, CurPos;
    pcap_file_header FileHeader;
    FILE* InputFile;
    std::map<sock, Session*> sessions;
public:
    Package(const char* FileName);
    ~Package();

    int GetData();
    /* 添加会话数据，如果会话不存在则新建，需要数据的序列号以及数据来源 */
    void AppendDataForSession(sock index_session, std::string new_data, u_int32 seq_no, int CS);
};