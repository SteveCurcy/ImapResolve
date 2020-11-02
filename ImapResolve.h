/*---------------------
* author: steve curcy
* target: IMAP 协议解析
* time: 2020-10-20
* -------------------*/
#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/stat.h>

#define DEBUG

#define OK 1
#define NO 2

#define CLIENT 1
#define SERVER 2

/* 邮件Flags移位数量 */
#define MSGID       7
#define HEADER      6
#define TEXT        5
#define ANSWERED    4
#define FLAGGED     3
#define DELETED     2
#define DRAFT       1
#define SEEN        0

/* 命令类型 */
#define LOGIN   1
#define SELECT  2
#define CREATE  3
#define DELETE  4
#define RENAME  5
#define SUBSCR  6
#define UNSUBS  7
#define APPEND  8
#define COPY    9

/*------------------------------------------------------------------------------
* class Message
* 邮件的标记FLAGS：\Answered \Flagged \Deleted \Draft \Seen；
* 为了节省空间，使用uint8类型保存状态，即五个标记分别占位16，8，4，2，1；
* InternalDate：邮件被接收的时间；RFC288.SIZE：邮件的字节数,信封，msg-id
* 邮件首部以及邮件正文。邮件类应有兼容部分和整体的功能，当内容不完整时，应将部分可用信息
* 写入首部；当首部内容完整时，应将之前的所有内容替换掉；对于正文，当内容不完整时，
* 应该写入该部分，并确保位置正确，当内容完整时应当将整个正文替换掉；并且首部和正文
* 都应确保，得到完整信息之后，不允许进行更改。实现功能如下：
*   1） 设置标志位，但不能修改前三位的值，若发现前三位为1，应返回错误；
*   2） 设置邮件的大小，查看邮件大小；
*   3） 设置邮件内部时间，msg-id信息，添加msg-id时应顺便设置部分首部（如果首部不完整）；
*   4） 设置部分首部信息，如From，To，Date，Content-Type， Subject，boundary等；
*       设置首部信息还有一个原则，应将新信息放到前方，确保信息的新鲜性；且若首部已完整
*       ，该功能不可用；
*   5） 设置部分邮件正文，如有部分与之前重叠，选择覆盖策略，保证信息最新；
*   6） 设置完整首部，读取到完整首部的响应，将首部全部删除并使用新首部进行替代，更改标志位；
*   7） 设置完整文本，读取到完整文本的响应，将文本全部删除并使用新文本进行替代，更改标志位
* ----------------------------------------------------------------------------*/

class Message {
    /* 在邮件类中添加三个额外标志位分别代表msg-id，首部完整，文本完整，Flags占位如下：
    * Msg-Id，HeadCom, TextCom, \Answered \Flagged \Deleted \Draft \Seen */
    u_int8_t Flags;
    int Size;
    std::string InternalDate, MessageId;
    /* 由于boundary和Content-Type的组合时固定的而其他首部并不要求顺序，
    * 所以额外存储这两项 */
    /* 对于部分头部提取，暂时只取得重要部分或完整信息，其他后续可改 */
    std::string Header, cont, bound;
    /* 为了文本部分最大程度的还原以及由于字符串的特殊性，此处文本在此使用map结构；
    * 以索取部分文本的第一字节位置作为key，以文本内容作为值，若出现完整文本信息，
    * 则应先将映射关系清空，只保留完整信息，并且更新标志位 */
    std::map<int, std::string> Text;
public:
    Message();
    ~Message() {};
    int SetFlags(u_int8_t flag);
    u_int8_t GetFlags() {return Flags;}
    int size()  {return Size;}
    int SetSize(int sz) {Size = (sz>0?sz:Size); return sz>0?OK:NO;}
    void SetInternalDate(std::string tm) {InternalDate.assign(tm);}
    std::string date() {return InternalDate;}
    /* Message只允许设置一次，因为Msg-id唯一确定一封邮件，不可更改 */
    std::string GetMsgId() {return MessageId;};
    int SetMsgId(std::string msg_id);
    /* 查看首部和文本信息 */
    std::string GetHeader() {return Header + cont + bound;}
    std::string GetText();
    /* 添加部分首部和文本信息 */
    int SetPartHeader(std::string field, std::string value);
    int SetPartText(int StartPos, std::string text);
    /* 添加完整首部和文本信息 */
    int SetFullHeader(std::string header);
    int SetFullText(std::string text);

    int save(std::string FileName);
};

/*--------------------------------------------------------------------------
* class Mailbox
* 注：在邮箱的结构体中并不存储本邮箱的名字，邮箱名在父节点中的map中存储，类似Linux文件格式；
* 成员首先包括是否可选，邮箱的标签（\NoSelect \HasNoChildren）,是否被订阅或活跃；
* 邮件数，最近邮件数，下一唯一标识符，唯一标识有效值，未查看数（int值，若为负数则没有相关信息）；
* 下属邮箱：使用Map进行存储，存储邮箱名到邮箱类的映射；
* 所属邮件：map存储，使用序列号进行查找，为查找方便，应加设map作为从邮件名到序列号的映射；
* 选取map存储邮件原因：map查找速度为logn，速度上可以接受；用户查看邮件具有局部性，且依靠一组
* 数据包只可能抓到一部分邮件，但如果邮件数量过多，但查看获得的有效信息少，vector则会浪费很大空间；
* 实现功能：
*   1） 改变订阅（或活跃）状态；
*   2） 改变邮箱的标签；改变邮箱的可选状态；（如果邮箱不可选，则应自动添加该标签，没有子节点同）
*   3） 删除和增添下属邮箱；
*   4） 改变下一唯一标识符，未查看数量，最近邮件数，总邮件数等;
*   5） 将当前工作目录下的序列集合所表示的邮件移动到某邮箱，若当前工作路径不存在则返回NULL;
*   6） 为5）服务，查找某名称的邮箱，返回该邮箱指针,若不存在则返回NULL；5）应放在Session类
*       中实现，而不是在此类，此类只是应为该功能提供一个支撑，方便邮箱的查找；
*   7) 增加和删除邮件
* ------------------------------------------------------------------------*/
class Mailbox {
    /* 是否可选，是否被订阅；规定初始均为true */
    bool BeSelected, BeSubed;
    // std::set<std::string> Tag;放弃标签存储，没有必要
    /* 初始化为-1，表示没有有效数据 */
    int TotalMails, RecentMails, UnseenMails;
    /* UID一定大于0，初始化为0，代表无效值 */
    u_int32_t UidNext, UidValidity;
    /* 由于存在删除邮箱（含下属邮箱）后建立同名邮箱的情况，所以使用multimap */
    std::multimap<std::string, Mailbox*> SubMailbox;
    std::map<int, Message*> Mails;
public:
    Mailbox();
    ~Mailbox();
    /* 返回相应数据的值 */
    bool IsSubed()  {return BeSubed;}
    bool IsSeled()  {return BeSelected;}
    u_int32_t GetUidNext() {return UidNext;}
    u_int32_t GetUidValidity() {return UidValidity;}
    int GetTotalMails() {return TotalMails;}
    int GetRecentMails() {return RecentMails;}
    int GetUnseenMails() {return UnseenMails;}
    void SetSel(bool val) {BeSelected = val;};
    void SetSub(bool val) {BeSubed = val;}
    void SetUidNext(u_int32_t UNext) {UidNext = UNext;}
    void SetUidValidity(u_int32_t UValid) {UidValidity = UValid;}
    /* 此处将邮件数和邮件分开添加，因为一开始选择邮箱就已经知道相应数量，但不知道邮件内容；
    * 在之后才能获得详细的邮件信息，所以两者分开；但是一定要注意保证两者的对应关系正确 */
    /* 邮件数量的更改，包括最近、总、未查看的数量 */
    int SetTotalMails(int number)   {TotalMails = (number>=0?number:-1);    return (number>=0?OK:NO);}
    int SetRecentMails(int number)  {RecentMails = (number>=0?number:-1);   return (number>=0?OK:NO);}
    int SetUnseenMails(int number)  {UnseenMails = (number>=0?number:-1);   return (number>=0?OK:NO);}
    /* 这里默认分割符都是/；在删除邮箱时注意还应将邮箱中的所有邮件一并删除 */
    Mailbox* AppendBox(std::string TarName, char Delimiter = '/');
    int DeleteBox(std::string TarName, char Delimiter = '/');
    /* 查找本邮箱下的某名称所属邮箱 */
    Mailbox* FindBoxByName(std::string TarName, char Delimiter = '/');
    /* 添加和删除邮件的函数
    * 添加邮件参数：邮件序列号，新建邮件的指针 */
    int AppendMail(int SeqId, Message* NewMail);
    /* 提供两种删除邮件的方式，一种是通过单个序列号删除单个；
    * 另一种通过序列号区间，删除批量邮件 */
    /* 需要有邮件类的支持，暂时只提供函数接口 */
    int DeleteMail(int SeqId);
    int DeleteMail(int Begin, int End);
    std::map<int, Message*>& GetAllMails() {return Mails;}
    int GetBoxNumebr() {return SubMailbox.size();}
    Mailbox* PopBox(std::string TarName, char Delimiter = '/');
    /* 此处的Push只是为了功能所写，实际功能为同名邮箱替换，使用指定的将原来的替换 */
    int PushBox(std::string TarName, Mailbox* TarBox, char Delimiter = '/');

    int save(std::string path_name);
};

/*-------------------------------------------------------------------
* class session
* 最重要的部分分为三部分，命令序列，响应序列，以及不完全数据序列；
* 命令序列和响应序列均应使用map映射，以字符串tag作为索引；
* 命令部分应包含的数据：命令种类，用于识别是哪一种命令，int类型；
* 命令参数，vector<string>存储，用于保存命令的参数；
* 响应部分应包含的数据：响应结果，成功或失败；
* 部分数据应包含的数据项：下一个字节流的首字节序列号，数据字符串；
* 所属邮件序列号，应接受数据的总数;
* 如接收到失败的响应，应直接将对应命令删除 
* ----------------------------------------------------------------*/
struct Command {
    int Kind;
    std::vector<std::string> args;
};

struct Response {
    int result;
    std::string data;
};

struct PartData {
    /* size为-1表示append数据；为-2表示不确定数据；
    * 此时应依靠StartSeq进行判断 */
    int size;
    u_int32_t StartSeq;
    bool IsEnd;
    std::string data;
};

class Session {
    std::string UserName, Password;
    /* 建立指向邮箱目录根的指针 */
    /* WorkPlace指出当前的工作目录，若为NULL则没有选择邮箱 */
    Mailbox RootMail, * WorkPlace;
    std::map<std::string, Command> commands;
    std::map<std::string, Response> responses;
    /* 请求序列号到数据的映射 */
    std::map<u_int32_t, PartData> datas;
public:
    /* 在会话结束时，应该自动生成对应邮箱的目录结构以及邮件 */
    Session();
    ~Session();

    /* 实现各个命令的功能 */
    void LogIn(std::string un, std::string pw) {UserName = un, Password = pw;}
    void Select(std::string BoxName, std::string res_data);
    int Rename(std::string Src, std::string Dst);
    void list(const std::string& data);
    void fetch(std::string data);
    void status(std::string data);

    /* 将当前工作目录下的序列集合所表示的邮件移动到某邮箱，若当前工作路径不存在则返回NULL */
    /* 序列号的左闭右开区间，同一般STL处理方式 */
    int CopyMails(int BIndex, int EIndex, std::string TarBoxName);
    int SetWorkPlace(std::string TarName);
    void AppendMail(std::string TarBoxName, Message* TarMail);
    int ReceiveData(std::string new_data, u_int32_t seq_no, int data_src);
    /* 如果是fetch，list，lsub命令，first存储指令，否则存储tag,
    * 如果不是正常结果返回的first为空 */
    std::pair<std::string, Response> GetResFromData(std::string& data);
};