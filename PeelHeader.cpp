#include "PeelHeader.h"

sock::sock(u_int32 FIP, u_int32 SIP, u_int16 FPort, u_int16 SPort) {
    /* 保证小端口在前，维持顺序，一个sock结构唯一确定一个会话 */
    if(FPort > SPort) {
        std::swap(FIP, SIP);
        std::swap(FPort, SPort);
    }
    IP_fir = FIP, IP_sec = SIP;
    Port_fir = FPort, Port_sec = SPort;
}

bool sock::operator<(const sock& obj) const {
    if (IP_fir == obj.IP_fir) {
        if(IP_sec == obj.IP_sec) {
            if(Port_fir == obj.Port_fir) {
                return Port_sec < obj.Port_sec;
            } else return Port_fir < obj.Port_fir;
        } else return IP_sec < obj.IP_sec;
    } else return IP_fir < obj.IP_fir;
}

bool sock::operator==(const sock& obj) const {
    return ((IP_fir == obj.IP_fir) && (IP_sec == obj.IP_sec) && (Port_fir == obj.Port_fir) && (Port_sec == obj.Port_sec));
}

Package::Package(const char* FileName) {
    if((InputFile = fopen(FileName, "r")) == NULL)  throw(FILE_OPEN_ERR);
    if(fread(&FileHeader, sizeof(pcap_file_header), 1, InputFile) != 1) throw(NO_PCAP);

    /* 计算当前数据链路帧首部的长度 */
    switch (FileHeader.linktype)
    {
    case ETHERNET:
        /* code */
        LinkLen = ETHER_HEAD;
        break;
    case LINUXCOOKED:
        LinkLen = LINUX_COOKED_CAPTURE_HEAD;
        break;
    }
    CurPos = 24;
}

Package::~Package() {
    fclose(InputFile);
    for (std::map<sock, Session*>::iterator it = sessions.begin(); it != sessions.end(); it++) {
        delete it->second;
    }
}

int Package::GetData() {
    pcap_pkthdr*    DataHeader = new pcap_pkthdr;
    IPHeader_t*     IpHeader = new IPHeader_t;
    TCPHeader_t*    TcpHeader = new TCPHeader_t;
    char DataTime[STR_SIZE];
    char src_ip[30], dst_ip[30];
    std::string MailBuff;
    int src_port, dst_port;

    /* 先调整文件指针到下一个数据的起始位置 */
    /* 如果已经到了文件末尾，返回读取失败 */
    while (fseek(InputFile, CurPos, SEEK_SET) == 0) {
        memset(DataHeader, 0, sizeof((void*)DataHeader));
        memset(IpHeader, 0, sizeof((void*)IpHeader));
        memset(TcpHeader, 0, sizeof((void*)TcpHeader));
        MailBuff.clear();

        if(fread(DataHeader, 16, 1, InputFile) != 1) {
            printf("Analysis has been finished!\n");
            break;
        }

        /* 计算下一个数据包的偏移值 */
        CurPos += (16 + DataHeader->caplen);
        /* 读取pcap包时间戳，转换成标准格式时间 */
        struct tm *timeinfo;
        time_t t = (time_t)(DataHeader->ts.tv_sec);
        timeinfo = localtime(&t);
        strftime(DataTime, sizeof(DataTime), "%Y-%m-%d %H:%M:%S", timeinfo);
        //printf("%s: ", DataTime);

        /* 忽略数据帧头 */
        fseek(InputFile, LinkLen, SEEK_CUR); /* 忽略数据帧头 */

        if(fread(IpHeader, sizeof(IPHeader_t), 1, InputFile) != 1) {
            break;
        }
        inet_ntop(AF_INET, (void *)&(IpHeader->SrcIP), src_ip, 16);
        inet_ntop(AF_INET, (void *)&(IpHeader->DstIP), dst_ip, 16);
        //printf("SourIP: %s, DestIP: %s, Protocol: %d\n", src_ip, dst_ip, IpHeader->Protocol);
        if(IpHeader->Protocol != 6) {
            /* 不是TCP，直接跳过 */
            continue;
        }

        if(fread(TcpHeader, sizeof(TCPHeader_t), 1, InputFile) != 1) {
            break;
        }
        /* 注意网络字节序和电脑字节序相反，先转换后比较 */
        src_port = ntohs(TcpHeader->SrcPort);
        dst_port = ntohs(TcpHeader->DstPort);
        //printf("SourPort: %d, DestPort: %d\n", src_port, dst_port);
        if((dst_port != 143 && src_port != 143) || (TcpHeader->Flags)&(u_int8)3) continue;

        int TcpLen = ntohs(IpHeader->TotalLen) - 40;
        if(TcpLen == 0) continue ;
        char tmp;
        for (int i = 0; i < TcpLen && (tmp = fgetc(InputFile)) != EOF; i++)
            MailBuff.push_back(tmp);
        AppendDataForSession(sock(IpHeader->SrcIP, IpHeader->DstIP, TcpHeader->SrcPort, TcpHeader->DstPort), MailBuff, htonl(TcpHeader->SeqNO), (src_port == 143?SERVER:CLIENT));
    }

    delete DataHeader;
    delete IpHeader;
    delete TcpHeader;
    return OK;
}

void Package::AppendDataForSession(sock index_session, std::string new_data, u_int32 seq_no, int CS) {
    /* 先查看是否已经建立了对应的会话， 如果没有先建立 */
    std::map<sock, Session*>::iterator it = sessions.find(index_session);
    if (it == sessions.end()) {
        Session* new_session = new Session;
        it = sessions.emplace(index_session, new_session).first;
    }
    /* 下一步应该由指定的session进行数据的处理 */
    it->second->ReceiveData(new_data, seq_no, CS);
}