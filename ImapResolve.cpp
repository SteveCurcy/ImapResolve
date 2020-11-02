#include "ImapResolve.h"

inline u_int32_t GetNextSeq(u_int32_t _start, u_int32_t _size) {
    u_int64_t st = _start, sz = _size;
    u_int32_t ans = (u_int32_t)((st + sz) % (1<<32));
    return ans;
}

inline bool check(char tar) {
    return (tar <= '9' && tar >= '0') || (tar <= 'z' && tar >= 'a') || (tar <= 'Z' && tar >= 'A');
}

std::string LowerCase(std::string tar) {
    for (int i = 0; i < (int)tar.size(); i++)
        if(tar[i] <= 'Z' && tar[i] >= 'A')  tar[i] = tar[i]-'A'+'a';
    return tar;
}

inline bool IsINBOX(std::string Name) {
    return (Name[0] == 'I' || Name[0] == 'i') && (Name[1] == 'N' || Name[1] == 'n') && (Name[2] == 'B' || Name[2] == 'b') && (Name[3] == 'O' || Name[3] == 'o') && (Name[4] == 'X' || Name[4] == 'x');
}

Message::Message() {
    Flags = 0;  Size = 0;
    InternalDate.clear();   MessageId.clear();
    Text.clear();
}

int Message::SetFlags(u_int8_t flag) {
    u_int8_t veri = (1 << MSGID) | (1 << HEADER) | (1 << TEXT);
    if((flag & veri) != (Flags & veri)) return NO;
    Flags = flag;
    return OK;
}

int Message::SetMsgId(std::string msg_id) {
    if(Flags & u_int8_t(1<<MSGID))  return NO;

    MessageId.assign(msg_id);
    /* 判断是否已经完整，如果不完整，则应直接跳过这一步 */
    if(Flags & u_int8_t(1<<HEADER)) return OK;
    std::string tmp("Message-ID: ");
    msg_id += "\n";
    Header.assign(tmp.append(msg_id.append(Header)));
    Flags |= u_int8_t(1<<MSGID);
    return OK;
}

std::string Message::GetText() {
    /* 如果已经获得了完整的邮件体，则应直接返回 */
    if(Flags & u_int8_t(1<<TEXT))   return Text.begin()->second;

    std::map<int, std::string>::iterator it = Text.begin();
    if (it == Text.end()) return "";
    std::string TempText(it->second);
    /* 维护最后一次加入的起始字节位置，以及结束字节位置 */
    int start = it->first, len = (it->second).size(), end = len + start;
    it++;
    for (; it != Text.end(); it++) {
        if (it->first == end) {
            /* 完美拼接，直接接在后面即可 */
            len = TempText.size();
            start = it->first, end = (it->second).size() + start;
            TempText.append(it->second);
        } else if (it->first > end) {
            /* 如果下一条记录的起始位置比上一次的结束位置还要大，
            * 则直接在原有的基础上合并即可 */
            TempText += "\n";
            len = TempText.size();
            TempText.append(it->second);
            start = it->first, end = (it->second).size() + start;
        } else {
            /* 如果下一条记录，和上一次的有重合，则应判断长度，
            * 如果长度不如之前获得的长，则直接跳过，否则进行覆盖 */
            if (int(it->first + (it->second).size()) < end) continue ;
            /* 预选取长度,其中end-it->first为需要减出的长度 */
            len = len - (end - it->first);
            start = it->first;  end = (it->second).size() + start;
            TempText.assign(TempText.substr(0, len) + it->second);
        }
    }
    return TempText;
}

int Message::SetPartHeader(std::string field, std::string value) {
    /* 先检查头部是否已经完整，如果完整则不可改变 */
    if(Flags & u_int8_t(1 << HEADER)) return NO;

    if(field == "boundary") {
        bound = "\tboundary=\"" + value + "\"\n";
    } else if(field == "Content-Type") {
        cont = "Content-Type: " + value + ";\n";
    } else Header.assign(field + ": " + value + "\n" + Header);
    return OK;
}

int Message::SetPartText(int StartPos, std::string text) {
    if(Flags & u_int8_t(1 << TEXT)) return NO;
    if(text.size() == 0)    return NO;

    if(Text.find(StartPos) != Text.end()) {
        /* 如果已经存在应该对比两者信息长度，保留长者 */
        if (Text[StartPos].size() > text.size()) return NO;
        Text[StartPos].assign(text);
    } else {
        Text.emplace(StartPos, text);
    }
    return OK;
}

int Message::SetFullHeader(std::string header) {
    if(Flags & u_int8_t(1 << HEADER)) return NO;

    Header.assign(header);
    Flags |= u_int8_t(1 << HEADER);
    return OK;
}

int Message::SetFullText(std::string text) {
    if(Flags & u_int8_t(1 << TEXT)) return NO;

    Text.clear();
    Text.emplace(0, text);
    Flags |= u_int8_t(1 << TEXT);
    return OK;
}

int Message::save(std::string FileName) {
    std::ofstream TarFile(FileName, std::ios::out);
    /* 打开文件失败，返回错误 */
    if(!TarFile.is_open())  return NO;

    if (Header.size())  TarFile << Header;
    if (cont.size())    TarFile << cont;
    if (bound.size())    TarFile << bound;
    TarFile << GetText() << std::endl;
    TarFile.close();
    return OK;
}

Mailbox::Mailbox() {
    BeSelected = BeSubed = true;
    SubMailbox.clear(); Mails.clear();
    TotalMails = RecentMails = UnseenMails = 0;
    UidNext = UidValidity = 0;
}

Mailbox::~Mailbox() {
    for (std::multimap<std::string, Mailbox*>::iterator it = SubMailbox.begin(); it != SubMailbox.end(); it++) {
        /* 对每一个下属邮箱都先释放空间 */
        delete it->second;
    }
    for (std::map<int, Message*>::iterator it = Mails.begin(); it != Mails.end(); it++) {
        delete it->second;
    }
}

Mailbox* Mailbox::AppendBox(std::string TarName, char Delimiter) {
    int TempPos = 0;
    bool IsSuper = false, IsInfer = true;   /* 置false，默认没有上层，进行新建 */
    for (; TempPos < int(TarName.size()); TempPos++) {
        if(TarName[TempPos] == Delimiter)  break ;
    }
    /* TempPos之前为上层目录的名字，后面为子目录名字 */
    std::string ArchDir = TarName.substr(0, TempPos);
    std::string InferDir;
    
    /* 检查是否存在子目录，如果不存在说明这是最后一次创建；
    * 如果存在，则构造子目录名，并可以继续进行创建 */
    if(TempPos == (int)TarName.size() || TempPos+1 == (int)TarName.size()) {
        IsInfer = false;
    } else {
        InferDir = TarName.substr(TempPos+1);
    }

    /* 先查找有无上层目录 */
    if(SubMailbox.find(ArchDir) != SubMailbox.end()) {  /* 可能有上层目录 */
        std::multimap<std::string, Mailbox*>::iterator begin, end;
        begin = SubMailbox.lower_bound(ArchDir);
        end = SubMailbox.upper_bound(ArchDir);

        /* 可能有多个被删除的同名邮箱，应该找到未被删除的 */
        for (; begin != end; begin++) {
            if(begin->second->BeSelected == true) {
                IsSuper = true; break;
            } else {
                /* 如果有下层的目标邮箱名，则可以在本邮箱下新建 */
                if (IsInfer)   return begin->second->AppendBox(InferDir, Delimiter);
                /* 如果本邮箱就是目标邮箱，则应返回错误 */
                if (IsInfer == false)   return NULL;
            }
        }
        /* 有上层目录，直接使用上层目录进行创建 */
        /* 如果没有子目录名，说明已经到了边界，并且目标已创建 */
        if(IsSuper && IsInfer) {
            return begin->second->AppendBox(InferDir, Delimiter);
        }
        /* 同名邮箱已存在，不能创建 */
        if(IsInfer == false && IsSuper) return NULL;
    }
    /* 如果没有发现上层目录 */
    if(IsSuper == false) {
        Mailbox* NewOne = new Mailbox;
        std::multimap<std::string, Mailbox*>::iterator NewIt = SubMailbox.emplace(ArchDir, NewOne);
        if(IsInfer)
            return NewIt->second->AppendBox(InferDir, Delimiter);
        else return NewOne;
    }
    return NULL;
}

int Mailbox::DeleteBox(std::string TarName, char Delimiter) {
    int TempPos = 0;
    bool IsInfer = true;
    for (; TempPos < (int)TarName.size(); TempPos++) {
        if(TarName[TempPos] == Delimiter)  break ;
    }
    /* TempPos之前为上层目录的名字，后面为子目录名字 */
    std::string ArchDir = TarName.substr(0, TempPos);
    std::string InferDir;
    
    if (TempPos == (int)TarName.size() || TempPos+1 == (int)TarName.size()) {
        IsInfer = false;
    } else {
        InferDir = TarName.substr(TempPos+1);
    }

    /* 确保INBOX不被删除 */
    if (IsINBOX(ArchDir) && IsInfer == false)   return NO;

    /* 先查找有无上层目录 */
    if(SubMailbox.find(ArchDir) != SubMailbox.end()) {  /* 可能有上层目录 */
        std::multimap<std::string, Mailbox*>::iterator begin, end;
        begin = SubMailbox.lower_bound(ArchDir);
        end = SubMailbox.upper_bound(ArchDir);

        /* 可能有多个被删除的同名邮箱，应该找到未被删除的 */
        for (; begin != end; begin++) {
            if(begin->second->BeSelected == true) {
                /* 没有NoSelect标签，自身可删除 */
                if(IsInfer == false) {
                    /* 如果该邮箱即为目标邮箱，查看是否有下属邮箱；
                    * 如果有，则只添加NoSelect标签 */
                    if(begin->second->GetBoxNumebr())   begin->second->SetSel(false);
                    else {
                        /* 没有下属邮箱，则应直接删除 */
                        delete begin->second;
                        SubMailbox.erase(begin);
                    }
                    return OK;
                } else {
                    /* 当前邮箱并不是目标邮箱 */
                    if (begin->second->DeleteBox(InferDir, Delimiter) == OK)   return OK;
                }
            } else {
                /* 标记有NoSelect标签，当没有子邮箱后，应该删除 */
                if(IsInfer == false)    continue;
                if(begin->second->DeleteBox(InferDir, Delimiter) == OK) {
                    /* 如果标有NoSelect标签并且被删除子目录；
                    * 应该查看是否还存在下属邮箱，如果不存在，则应被删除 */
                    if(begin->second->GetBoxNumebr() == 0) {
                        delete begin->second;
                        SubMailbox.erase(begin);
                    }
                    return OK;
                }
            }
        }
    }
    return NO;
}

Mailbox* Mailbox::FindBoxByName(std::string TarName, char Delimiter) {
    int TempPos = 0;
    /* bool值用于查看路径中是否存在上下层，并保存响应路径 */
    bool IsInfer = true;
    std::string ArchDir = TarName.substr(0, TempPos);
    std::string InferDir;

    /* 先取得分隔符所在位置，后判断所给路径中是否有下层目录 */
    for (; TempPos < (int)TarName.size() && TarName[TempPos] != Delimiter; TempPos++) {
        ArchDir.push_back(TarName[TempPos]);
    }
    if(TempPos == (int)TarName.size() || TempPos+1 == (int)TarName.size()) {
        IsInfer = false;
    } else {
        InferDir = TarName.substr(TempPos+1);
    }

    if(SubMailbox.find(ArchDir) != SubMailbox.end()) {
        /* 有可能存在上层目录,遍历查看是否真实存在，若不存在返回NULL */
        std::multimap<std::string, Mailbox*>::iterator begin, end;
        begin = SubMailbox.lower_bound(ArchDir);
        end = SubMailbox.upper_bound(ArchDir);

        /* 在此函数中，上层目录是否为最终目标目录对结果是有影响的；
        * 若上层目录被删除，且目标路径也有下层目录则应继续查询，否则，
        * 目标路径已经到达最终路径，则应返回NULL */
        for (; begin != end; begin++) {
            if(IsInfer) {
                Mailbox* temp = begin->second->FindBoxByName(InferDir);
                if(temp)    return temp;
                return NULL;
            } else {
                if(begin->second->BeSelected)   return begin->second;
            }
        }
    }
    return NULL;
}

int Mailbox::AppendMail(int SeqId, Message* NewMail) {
    if (NewMail == NULL)    return NO;

    Mails.emplace(SeqId, NewMail);
    return OK;
}

/* 邮件的删除暂时只实现部分功能，等邮件类实现后再进行进一步实现
* 注意删除邮件时，邮件个数和邮件详细信息的关系；
* 即：Mails和各类数目保持一致。 */
int Mailbox::DeleteMail(int SeqId) {
    std::map<int, Message*>::iterator it;
    if((it = Mails.find(SeqId)) != Mails.end()) {
        u_int8_t flags = it->second->GetFlags();
        if((flags & (1 << SEEN)) == 0) UnseenMails--;
        delete it->second;
        Mails.erase(it);
    }
    TotalMails--;
    return OK;
}

int Mailbox::DeleteMail(int Begin, int End) {
    int offset = End-Begin;
    std::map<int, Message*>::iterator it = Mails.begin();
    while (it != Mails.end()) {
        if(it->first >= Begin && it->first < End) {
            /* 序列号在此区间内的都应该被删除 */
            /* 在删除之前应先查看邮件的标志，以维持数量尽力一致 */
            u_int8_t flags = it->second->GetFlags();
            if((flags & (1 << SEEN)) == 0) UnseenMails--;
            delete it->second;
            it = Mails.erase(it);
        } else if(it->first >= End) break;
        else it++;
    }
    /* 注意序列号一定是顺序的，所以删除后，后面的邮件都应该前移 */
    while (it != Mails.end()) {
        /* 因为map不能直接修改key，所以应该先删除，再添加 */
        /* 由于有新添加的元素，一定要确保符合条件；
        * 新添加的元素key一定是第一个≤End的 */
        Mails.emplace(it->first - offset, it->second);
        it = Mails.erase(it);
    }
    /* 一定要保持数量上的一致 */
    TotalMails -= offset;
    return OK;
}

Mailbox* Mailbox::PopBox(std::string TarName, char Delimiter) {
    int TempPos = 0;
    bool IsInfer = true;
    for (; TempPos < (int)TarName.size(); TempPos++) {
        if(TarName[TempPos] == Delimiter)  break ;
    }
    /* TempPos之前为上层目录的名字，后面为子目录名字 */
    std::string ArchDir = TarName.substr(0, TempPos);
    std::string InferDir;
    
    if (TempPos == (int)TarName.size() || TempPos+1 == (int)TarName.size()) {
        IsInfer = false;
    } else {
        InferDir = TarName.substr(TempPos+1);
    }

    /* 确保INBOX不被弹出 */
    if (IsINBOX(ArchDir) && IsInfer == false)   return NULL;

    /* 先查找有无上层目录 */
    if(SubMailbox.find(ArchDir) != SubMailbox.end()) {  /* 可能有上层目录 */
        std::multimap<std::string, Mailbox*>::iterator begin, end;
        begin = SubMailbox.lower_bound(ArchDir);
        end = SubMailbox.upper_bound(ArchDir);

        /* 可能有多个被删除的同名邮箱，应该找到未被删除的 */
        for (; begin != end; begin++) {
            if(begin->second->BeSelected == true) {
                /* 没有NoSelect标签，自身可弹出 */
                if(IsInfer == false) {
                    /* 保存该邮箱并将元素弹出 */
                    Mailbox* tmp = begin->second;
                    SubMailbox.erase(begin);
                    return tmp;
                } else {
                    /* 当前邮箱并不是目标邮箱 */
                    return begin->second->PopBox(InferDir, Delimiter);
                }
            } else {
                if(IsInfer == false)    continue;
                return begin->second->PopBox(InferDir, Delimiter);
            }
        }
    }
    return NULL;
}

int Mailbox::PushBox(std::string TarName, Mailbox* TarBox, char Delimiter) {
    int TempPos = 0;
    bool IsInfer = true;
    for (; TempPos < (int)TarName.size(); TempPos++) {
        if(TarName[TempPos] == Delimiter)  break ;
    }
    /* TempPos之前为上层目录的名字，后面为子目录名字 */
    std::string ArchDir = TarName.substr(0, TempPos);
    std::string InferDir;
    
    if (TempPos == (int)TarName.size() || TempPos+1 == (int)TarName.size()) {
        IsInfer = false;
    } else {
        InferDir = TarName.substr(TempPos+1);
    }

    /* 确保INBOX不被替换 */
    if (IsINBOX(ArchDir) && IsInfer == false)   return NO;

    /* 先查找有无上层目录 */
    if(SubMailbox.find(ArchDir) != SubMailbox.end()) {  /* 可能有上层目录 */
        std::multimap<std::string, Mailbox*>::iterator begin, end;
        begin = SubMailbox.lower_bound(ArchDir);
        end = SubMailbox.upper_bound(ArchDir);

        /* 可能有多个被删除的同名邮箱，应该找到未被删除的 */
        for (; begin != end; begin++) {
            if(begin->second->BeSelected == true) {
                /* 没有NoSelect标签，自身可被替换 */
                if(IsInfer == false) {
                    /* 将指针指向的原有邮箱删除，用新的邮箱替代 */
                    delete begin->second;
                    begin->second = TarBox;
                    return OK;
                } else {
                    /* 当前邮箱并不是目标邮箱 */
                    return begin->second->PushBox(InferDir, TarBox, Delimiter);
                }
            } else {
                if(IsInfer == false)    continue;
                return begin->second->PushBox(InferDir, TarBox, Delimiter);
            }
        }
    }
    return NO;
}

int Mailbox::save(std::string path_name) {
    std::multimap<std::string, Mailbox*>::iterator it = SubMailbox.begin();
    std::map<int, Message*>::iterator it_mail = Mails.begin();
    std::string new_path, new_mail;
    while (it != SubMailbox.end()) {
        new_path = path_name+"/"+it->first;
        if (mkdir(new_path.c_str(), (S_IRWXU|S_IRWXG|S_IWOTH|S_IXOTH)) == 1) return NO;
        it->second->save(new_path);
        it++;
    }
    while (it_mail != Mails.end()) {
        new_mail = path_name+"/"+std::to_string(it_mail->first)+".eml";
        if (it_mail->second->save(new_mail) == NO) return NO;
        it_mail++;
    }
    return OK;
}

Session::Session() {
    /* 根邮箱比较特殊，它并不是实际存在的，但却作为其他邮箱的索引，是一种很特殊的存在 */
    /* 注意将所有数据均初始化 */
    RootMail.SetSel(false); commands.clear();
    responses.clear();  datas.clear();  UserName.clear();   Password.clear();
    RootMail.AppendBox("inbox");    WorkPlace = NULL;
}

Session::~Session() {
    /* 将数据按文件目录的格式保存，根目录为用户名 */
    mkdir(("./"+UserName).c_str(), 00773);
    /* 将对主目录进行save，以递归的形式进行文件和文件夹保存 */
    RootMail.save(("./"+UserName).c_str());

    /* 保存用户的密码 */
    std::ofstream pass("./"+UserName+"/password.txt", std::ios::out);
    pass << Password;
    pass.close();
}

int Session::CopyMails(int BIndex, int EIndex, std::string TarBoxName) {
    TarBoxName = LowerCase(TarBoxName);
    /* 如果拷贝成功，则返回OK；
    *  如果拷贝过程中失败，或目录不存在则返回NO*/

    /* 如果没有当前工作目录，则应直接返回 */
    if (WorkPlace == NULL)  return NO;

    /* 一共将添加邮件的数量 */
    int MailsNumber = EIndex-BIndex;
    /* 遍历当前工作路径下的所有邮件，若有在区间内的则应添加到新目录下 */
    std::map<int, Message*> Mails = WorkPlace->GetAllMails();
    std::map<int, Message*>::iterator it = Mails.begin();

    /* 取得目标目录对应的邮箱 */
    Mailbox* TarBox = RootMail.FindBoxByName(TarBoxName);
    std::map<int, Message*> NewMails = TarBox->GetAllMails();
    while (it != Mails.end()) {
        /* 将符合条件的邮件添加到新邮箱中 */
        if(it->first >= BIndex && it->first < EIndex) {
            /* 计算该邮件在新邮箱中相对于末尾的偏移量，注：序列号时正整数，无需-1 */
            int offset = it->first - BIndex + 1;
            NewMails.emplace(TarBox->GetTotalMails() + offset, it->second);
        } else it++;
    }
    /* 所有相关的邮件详细内容都已经添加完成；
    * 最后一步，新邮件增加相应邮件的数量 */
    TarBox->SetTotalMails(TarBox->GetTotalMails()+MailsNumber);
    return OK;
}

int Session::SetWorkPlace(std::string TarName) {
    WorkPlace = RootMail.FindBoxByName(TarName);
    if(WorkPlace == NULL)   return NO;
    return OK;
}

void Session::AppendMail(std::string TarBoxName, Message* TarMail) {
    TarBoxName = LowerCase(TarBoxName);

    Mailbox* tmp = NULL;
    if(IsINBOX(TarBoxName) == false) tmp = RootMail.AppendBox(TarBoxName);
    tmp = RootMail.FindBoxByName(TarBoxName);
    tmp->SetTotalMails((tmp->GetTotalMails())+1);
    tmp->AppendMail(tmp->GetTotalMails(), TarMail);
    u_int8_t flag = TarMail->GetFlags();
    if((flag & u_int8_t(1<<SEEN)) == 0) tmp->SetRecentMails((tmp->GetUnseenMails())+1);
}

void Session::Select(std::string BoxName, std::string res_data) {
    BoxName = LowerCase(BoxName);

    if(IsINBOX(BoxName) == false)   RootMail.AppendBox(BoxName);
    else BoxName.assign("inbox");
    WorkPlace = RootMail.FindBoxByName(BoxName);
    u_int32_t tmp_number = 0;
    int cur_pos = 0;
    std::string tmp_str;
    while (cur_pos < (int)res_data.size()) {
        while (cur_pos < (int)res_data.size() && res_data[cur_pos] != ' ') {
            if(res_data[cur_pos] <= '9' && res_data[cur_pos] >= '0') {
                tmp_number = (tmp_number<<1) + (tmp_number<<3) + res_data[cur_pos]-'0';
            } else if(res_data[cur_pos] <= 'Z' && res_data[cur_pos] >= 'A')
                tmp_str.push_back(res_data[cur_pos]);
            cur_pos++;
        }
        if(tmp_str == "OK") tmp_str.clear();
        else if(tmp_str.size() && tmp_number) {
            if(tmp_str == "EXISTS") WorkPlace->SetTotalMails(tmp_number);
            else if (tmp_str == "RECENT") WorkPlace->SetRecentMails(tmp_number);
            else if (tmp_str == "UNSEEN") WorkPlace->SetUnseenMails(tmp_number);
            else if (tmp_str == "UIDVALIDITY") WorkPlace->SetUidValidity(tmp_number);
            else if (tmp_str == "UIDNEXT") WorkPlace->SetUidNext(tmp_number);
            tmp_number = 0; tmp_str.clear();
        } else {
            tmp_str.clear();    tmp_number = 0;
        }
        cur_pos++;
    }
}

int Session::Rename(std::string Src, std::string Dst) {
    Src = LowerCase(Src);
    Dst = LowerCase(Dst);

    if(IsINBOX(Dst)) return NO;
    if(IsINBOX(Src)) {
        /* 如果对INBOX重命名，则应新建并将INBOX中所有邮件移入新邮箱 */
        Mailbox* tmp = WorkPlace;
        SetWorkPlace("inbox");
        Mailbox* dst = RootMail.AppendBox(Dst);
        if (dst == NULL) dst = RootMail.FindBoxByName(Dst);
        CopyMails(1, (WorkPlace->GetTotalMails())+1, Dst);
        WorkPlace->DeleteMail(1, (WorkPlace->GetTotalMails())+1);
        WorkPlace = tmp;
        return OK;
    }
    /* 与Inbox不同，INBOX的重命名本质是复制，但其他是邮箱结构体的移动 */
    /* 如果目标邮箱已经存在了则不允许移动 */
    if(RootMail.AppendBox(Dst) == NULL) return NO;
    /* 将源邮箱弹出，然后替换掉目标邮箱（因为新建，所以空，替换掉没有损失） */
    RootMail.PushBox(Dst, RootMail.PopBox(Src));
    return OK;
}

void Session::list(const std::string& data) {
    int cur_pos = 0;
    char Delimiter = '/';
    std::string TmpBoxName;
    while (cur_pos < (int)data.size()) {
        /* 直接找分隔符 */
        while(cur_pos < (int)data.size() && data[cur_pos] != '\"') cur_pos++;
        cur_pos++;
        Delimiter = data[cur_pos];  cur_pos += 4;
        while(cur_pos < (int)data.size() && data[cur_pos] != '\"') TmpBoxName.push_back(data[cur_pos++]);
        cur_pos += 7; /* 确保跳转到下一行 */
        /* 添加邮箱 */
        TmpBoxName = LowerCase(TmpBoxName);
        RootMail.AppendBox(TmpBoxName, Delimiter);
        TmpBoxName.clear();
    }
}

void Session::status(std::string data) {
    std::string TarBoxName;
    u_int32_t number = 0, cur_pos = 0;
    /* 先查找邮箱名 */
    while (cur_pos < data.size() && data[cur_pos] != '\"') cur_pos++;
    cur_pos++;
    while (cur_pos < data.size() && data[cur_pos] != '\"') {
        TarBoxName.push_back(data[cur_pos]);
        cur_pos++;
    }
    TarBoxName = LowerCase(TarBoxName);
    Mailbox* temp_box = RootMail.AppendBox(TarBoxName);
    if (temp_box == NULL) temp_box = RootMail.FindBoxByName(TarBoxName);
    /* 查找状态字信息 */
    TarBoxName.clear(); cur_pos += 3;
    while (cur_pos < data.size() && data[cur_pos] != ' ') {
        TarBoxName.push_back(data[cur_pos]);
        cur_pos++;
    }
    /* 查找返回数值 */
    while (cur_pos < data.size() && data[cur_pos] <= '9' && data[cur_pos] >= '0') {
        number = (number<<1) + (number<<3) + data[cur_pos++]-'0';
    }
    /* 对应标志进行数量更新 */
    if(TarBoxName == "MESSAGES") temp_box->SetTotalMails(number);
    else if(TarBoxName == "RECENT") temp_box->SetRecentMails(number);
    else if(TarBoxName == "UIDNEXT") temp_box->SetUidNext(number);
    else if(TarBoxName == "UIDVALIDITY") temp_box->SetUidValidity(number);
    else if(TarBoxName == "UNSEEN") temp_box->SetUnseenMails(number);
}

void Session::fetch(std::string data) {
    //std::cout << data << std::endl;
    int seq_mail = 0, cur_pos = 2, size_part = 0, pos_start = -1;
    std::string item, val;
    Message* tar_mail = NULL;

    // if(WorkPlace == NULL) printf("NO Workplace!\n");
    if (WorkPlace == NULL) return ;

    while (cur_pos < (int)data.size() && data[cur_pos] != ' ') {
        seq_mail = (seq_mail<<1) + (seq_mail<<3) + data[cur_pos]-'0';
        cur_pos++;
    }
    cur_pos += 8; /* 直接跳过FETCH (字符串 */
    /* 获得目标邮件 */
    std::map<int, Message*>::iterator it = (WorkPlace->GetAllMails()).find(seq_mail);
    if (it == (WorkPlace->GetAllMails()).end()) {
        /* 如果没有响应邮件，应加入 */
        tar_mail = new Message;
        WorkPlace->AppendMail(seq_mail, tar_mail);
    } else tar_mail = it->second;

    /* 后面紧接应该是数据项 */
    while (cur_pos < (int)data.size()) {
        /* 获得数据项名称 */
        while (cur_pos < (int)data.size() && data[cur_pos] <= 'Z' && data[cur_pos] >= 'A') {
            item.push_back(data[cur_pos]);
            cur_pos++;
        }
        /* 分别进行处理 */
        if (item == "FLAGS") {
            cur_pos++;
            /* 如果Flags有值，那么先清空，后添加 */
            if (data[cur_pos] != ')') tar_mail->SetFlags((tar_mail->GetFlags()) & 224);
            while (data[cur_pos] != ')') {
                cur_pos += 2;
                while ((data[cur_pos] <= 'Z' && data[cur_pos] >= 'A') || (data[cur_pos] <= 'z' && data[cur_pos] >= 'a')) {
                    val.push_back(data[cur_pos++]);
                }
                if (val == "Answered") {
                    tar_mail->SetFlags((tar_mail->GetFlags()) | (1<<ANSWERED));
                } else if (val == "Flagged") {
                    tar_mail->SetFlags((tar_mail->GetFlags()) | (1<<FLAGGED));
                } else if (val == "Deleted") {
                    tar_mail->SetFlags((tar_mail->GetFlags()) | (1<<DELETED));
                } else if (val == "Draft") {
                    tar_mail->SetFlags((tar_mail->GetFlags()) | (1<<DRAFT));
                } else if (val == "Seen") {
                    tar_mail->SetFlags((tar_mail->GetFlags()) | (1<<SEEN));
                }
                val.clear();
            }
            cur_pos += 2;
        } else if(item == "RFC") {
            /* 这里只考虑RFC822.SIZE的情况 */
            cur_pos += 9;
            int temp_size = 0;
            while (cur_pos < (int)data.size() && data[cur_pos] <= '9' && data[cur_pos] >= '0') {
                temp_size = (temp_size<<1) + (temp_size<<3) + data[cur_pos]-'0';
                cur_pos++;
            }
            tar_mail->SetSize(temp_size);
            cur_pos++;
        } else if (item == "INTERNALDATE") {
            cur_pos += 2;
            while (data[cur_pos] != '\"') {
                val.push_back(data[cur_pos]);
                cur_pos++;
            }
            tar_mail->SetInternalDate(val);
            cur_pos += 2;
        } else if (item == "ENVELOPE") {
            while (data[cur_pos] != '=') cur_pos++;
            while (data[cur_pos] != '\"') val.push_back(data[cur_pos++]);
            tar_mail->SetPartHeader("Subject", val);    val.clear();
            while (data[cur_pos] != '<') cur_pos++;
            while (data[cur_pos] != '\"') val.push_back(data[cur_pos++]);
            tar_mail->SetPartHeader("Message-ID", val);
            cur_pos += 4;
        } else if (item == "BODY") {
            if (data[cur_pos] != '[') {
                cur_pos++;  item.clear();   continue;
            }
            cur_pos++;
            std::string part;
            while (data[cur_pos] != ']') part.push_back(data[cur_pos++]);
            if (data[++cur_pos] == '<') {
                pos_start = 0; cur_pos++;
                while (data[cur_pos] != '>') {
                    pos_start = (pos_start<<1) + (pos_start<<3) + data[cur_pos++]-'0';
                }
                cur_pos++;
            }
            cur_pos += 2;
            while (data[cur_pos] != '}') {
                size_part = (size_part<<1) + (size_part<<3) + data[cur_pos++]-'0';
            }
            cur_pos += 3;
            if (pos_start == -1) {
                /* 说明是完整的部分 */
                if (part == "" || part == "TEXT") tar_mail->SetFullText(data.substr(cur_pos, size_part));
                else if(part == "HEADER") tar_mail->SetFullHeader(data.substr(cur_pos, size_part));
            } else {
                if (part == "TEXT") tar_mail->SetPartText(pos_start, data.substr(cur_pos, size_part));
                else if(part == "HEADER") tar_mail->SetPartHeader("Received", data.substr(cur_pos, size_part));
            }
            cur_pos += size_part+1;
        } else cur_pos++;

        item.clear();   val.clear();
        size_part = 0; pos_start = -1;
    }
}

int Session::ReceiveData(std::string new_data, u_int32_t seq_no, int data_src) {
    if (data_src == CLIENT) {
        /* 对从客户端发来的命令进行处理 */
        std::string command, tag;
        int beg = 0, end = 0;
        while (end < (int)new_data.size() && check(new_data[end])) end++;
        if(new_data[end] != ' ') {
            /* 这里不为空格，说明这应该是在添加邮件，应该去查找有无Append命令 */
            for (std::map<std::string, Command>::iterator it = commands.begin(); it != commands.end(); it++) {
                if ((it->second).Kind == APPEND && responses.find(it->first) != responses.end()) {
                    /* 如果能找到Append命令，并且已经有成功响应，则应该直接添加 */
                    Message* tmp = new Message;
                    tmp->SetFullText(new_data);
                    AppendMail((it->second).args[0], tmp);
                    /* 将已有的响应消息删除 */
                    responses.erase(responses.find(it->first));
                    return OK;
                }
            }
            /* 如果没有返回，说明没有append或响应，则应该现存储，邮件序列号设置为-1以方便查找 */
            PartData tmp_data;
            tmp_data.data = new_data;   tmp_data.size = -1;
            datas.emplace(seq_no, tmp_data);
            return OK;
        }
        tag.assign(new_data.substr(beg, end-beg));
        beg = ++end;
        for (; end < (int)new_data.size() && new_data[end] != ' '; end++) {
            command.push_back(new_data[end]);
        }
        command = LowerCase(command);
        Command new_com;
        beg = ++end;
        while (end < (int)new_data.size() && new_data[end] != '\n' && new_data[end] != '\r') {
            for (; end < (int)new_data.size() && new_data[end] != ' ' && new_data[end] != '\n' && new_data[end] != '\r'; end++);
            new_com.args.push_back(new_data.substr(beg, end-beg));
            beg = ++end;
        }
        #ifdef DEBU
            std::cout << command << std::endl;
            std::vector<std::string>::iterator it_str = new_com.args.begin();
            while (it_str != new_com.args.end()) {
                std::cout << *it_str << std::endl;
                it_str++;
            }
        #endif
        /* 在最后分析命令的时候，应该查看是否有响应已经提前收到了 */
        std::map<std::string, Response>::iterator it = responses.find(tag);
        if(command == "login") {
            new_com.Kind = LOGIN;
            if(it != responses.end()) {
                if ((it->second).result == OK)  LogIn(new_com.args[0], new_com.args[1]);
                responses.erase(it);
                return OK;
            }
        } else if(command == "select" || command == "examine") {
            /* 因为二者返回的重要信息一样，所以占用一个 */
            new_com.Kind = SELECT;
            if (it != responses.end()) {
                if ((it->second).result == OK) Select(new_com.args[0], (it->second).data);
                responses.erase(it);
                return OK;
            }
        } else if(command == "create") {
            new_com.Kind = CREATE;
            if(it != responses.end()) {
                new_com.args[0] = LowerCase(new_com.args[0]);
                if ((it->second).result == OK) RootMail.AppendBox(new_com.args[0]);
                responses.erase(it);
                return OK;
            }
        } else if(command == "delete") {
            new_com.Kind = DELETE;
            if(it != responses.end()) {
                new_com.args[0] = LowerCase(new_com.args[0]);
                if ((it->second).result == OK) RootMail.DeleteBox(new_com.args[0]);
                responses.erase(it);
                return OK;
            }
        } else if(command == "rename") {
            new_com.Kind = RENAME;
            if(it != responses.end()) {
                if ((it->second).result == OK) Rename(new_com.args[0], new_com.args[1]);
                responses.erase(it);
                return OK;
            }
        } else if(command == "subscribe") {
            new_com.Kind = SUBSCR;
            if(it != responses.end()) {
                if ((it->second).result == OK) {
                    new_com.args[0] = LowerCase(new_com.args[0]);
                    Mailbox* tmp = RootMail.AppendBox(new_com.args[0]);
                    if (tmp == NULL) tmp = RootMail.FindBoxByName(new_com.args[0]);
                    tmp->SetSub(true);
                }
                responses.erase(it);
                return OK;
            }
        } else if(command == "unsubscribe") {
            new_com.Kind = UNSUBS;
            if(it != responses.end()) {
                if ((it->second).result == OK) {
                    new_com.args[0] = LowerCase(new_com.args[0]);
                    Mailbox* tmp = RootMail.AppendBox(new_com.args[0]);
                    if (tmp == NULL) tmp = RootMail.FindBoxByName(new_com.args[0]);
                    tmp->SetSub(false);
                }
                responses.erase(it);
                return OK;
            }
        } else if(command == "append") {
            new_com.Kind = APPEND;
            if (it != responses.end()) {
                /* append添加命令成功，查看是否有序列号为-1的数据 */
                std::map<u_int32_t, PartData>::iterator it_data;
                for (it_data = datas.begin(); it_data != datas.end(); it_data++) {
                    if((it_data->second).size == -1) break ;
                }
                if ((it->second).result == OK && it_data != datas.end()) {
                    /* 如果有数据，将数据写入，并将响应删除 */
                    Message* tmp_mail = new Message;
                    tmp_mail->SetFullText((it_data->second).data);
                    AppendMail(new_com.args[0], tmp_mail);
                    responses.erase(it);    datas.erase(it_data);
                    return OK;
                }
                if ((it->second).result == NO) {
                    datas.erase(it_data);
                    return NO;
                }
                /* 如果没有响应，应直接跳过，将命令加入到命令集合中 */
            }
        } else if(command == "copy") {
            new_com.Kind = COPY;
            if(it != responses.end()) {
                if ((it->second).result == OK) {
                    /* 确保邮箱存在 */
                    RootMail.AppendBox(new_com.args[1]);
                    /* 计算区间 */
                    int beg = 0, end = 0, cur_pos = 0;
                    while (new_com.args[0][cur_pos] != ':') {
                        beg = (beg<<1) + (beg<<3) + new_com.args[0][cur_pos]-'0';
                        cur_pos++;
                    }
                    cur_pos++;
                    while (cur_pos < (int)new_com.args[0].size()) {
                        end = (end<<1) + (end<<3) + new_com.args[0][cur_pos]-'0';
                        cur_pos++;
                    }
                    if(end == 0) end = beg;
                    CopyMails(beg, end+1, new_com.args[1]);
                }
                responses.erase(it);
                return OK;
            }
        }
        commands.emplace(tag, new_com);
        return OK;
    } else {
        /* 服务器发来的响应数据 */
        /* 如果第一个时+，则直接丢弃 */
        if (new_data == "+ Ready for literal data" || new_data[0] == '+') return NO;
        /* 查看是否是一个完整的响应 */
        std::pair<std::string, Response> temp_pair = GetResFromData(new_data);
        if (temp_pair.first.size() == 0) {
            /* 如果并不是一个完整的响应，应该查看数据首字符是否为*，
            * 若是，则说明是fetch的第一个数据包 */
            if (new_data[0] == '*') {
                /* 返回的第一条为* OK [CAPABILITY...，应被过滤，
                * 第一个为*，第三个为O则为此响应，应该直接返回 */
                if(new_data[2] == 'O')  return NO;

                int data_size = 0, cur_pos = 0;
                PartData data_head;
                data_head.data = new_data;

                /* 读取数据部分应有的总长度 */
                while (cur_pos < (int)new_data.size() && new_data[cur_pos] != '{') cur_pos++;
                cur_pos++;
                while (cur_pos < (int)new_data.size() && new_data[cur_pos] <= '9' && new_data[cur_pos] <= '0')
                    data_size = (data_size<<1) + (data_size<<3) + new_data[cur_pos++]-'0';
                data_head.IsEnd = false;    data_head.size = data_size;
                data_head.StartSeq = seq_no;
                /* 查找是否有后半部分数据 */
                for (std::map<u_int32_t, PartData>::iterator it_head = datas.begin(); it_head != datas.end(); it_head++) {
                    if ((it_head->second).StartSeq == GetNextSeq(seq_no, data_head.data.size())) {
                        /* 找到数据的后半部分 */
                        if((it_head->second).IsEnd) {
                            /* 两个数据拼接即可得到完整数据 */
                            fetch(new_data+(it_head->second).data);
                            datas.erase(it_head);
                            return OK;
                        } else {
                            data_head.data += (it_head->second).data;
                            datas.erase(it_head);
                        }
                        break;
                    }
                }
                /* 将数据的前半部分插入 */
                datas.emplace(GetNextSeq(data_head.StartSeq, data_head.data.size()), data_head);
            } else {
                /* 数据的中间部分，应该查找前后是否有数据能拼接到一起 */
                /* 先查找前半部分 */
                PartData TarData;
                std::map<u_int32_t, PartData>::iterator it_body = datas.find(seq_no);
                if (it_body != datas.end()) {
                    /* 发现了前面的部分 */
                    TarData.data = (it_body->second).data + new_data;
                    TarData.size = (it_body->second).size;
                    TarData.IsEnd = false;
                    TarData.StartSeq = (it_body->second).StartSeq;
                    /* 信息记录完毕，将数据前半部分删除 */
                    datas.erase(it_body);
                } else {
                    /* 如果并没有发现，则应该将本数据当做数据的前半部分 */
                    TarData.StartSeq = seq_no;  TarData.data = new_data;
                    TarData.IsEnd = false;  TarData.size = -2;
                }
                /* 搜索数据的后半部分 */
                for (it_body = datas.begin(); it_body != datas.end(); it_body++) {
                    if ((it_body->second).StartSeq == GetNextSeq(seq_no, TarData.data.size())) {
                        /* 找到数据的后半部分 */
                        if((it_body->second).IsEnd) {
                            /* 两个数据拼接即可得到完整数据 */
                            fetch(new_data+(it_body->second).data);
                            datas.erase(it_body);
                            return OK;
                        } else {
                            TarData.data += (it_body->second).data;
                            datas.erase(it_body);
                        }
                        break;
                    }
                }
                datas.emplace(GetNextSeq(TarData.StartSeq, TarData.data.size()), TarData);
            }
            return OK;
        } else {
            /* 读取到响应 */
            if (temp_pair.first == "LIST" || temp_pair.first == "LSUB") {
                /* 两者返回有效信息相同，一起处理 */
                if (temp_pair.second.result == OK) {
                    list(new_data);
                    return OK;
                }
                return NO;
            }
            if (temp_pair.first == "STATUS") {
                if (temp_pair.second.result == OK) {
                    status(new_data);
                    return OK;
                }
                return NO;
            }
            if (temp_pair.first == "FETCH") {
                if(temp_pair.second.result == OK) {
                    /* 先查看是否为空 */
                    if (new_data.size() == 0) return OK;
                    if (new_data[0] == '*') {
                        /* 本数据是一个完整的fetch数据，直接交由fetch处理 */
                        fetch(new_data);    return OK;
                    }
                    /* 数据不完整，应去datas查找是否有数据的前半部分 */
                    std::map<u_int32_t, PartData>::iterator it_data;
                    PartData TarData;

                    it_data = datas.find(seq_no);
                    if (it_data != datas.end()) {
                        /* 如果能找到前半部分数据，拼接 */
                        if ((it_data->second).size == -2) {
                            TarData.data = (it_data->second).data + new_data;
                            TarData.IsEnd = true;   TarData.size = (it_data->second).size;
                            TarData.StartSeq = (it_data->second).StartSeq;
                            datas.emplace(GetNextSeq(TarData.StartSeq, TarData.data.size()), TarData);
                        } else {
                            /* 两份数据可以完美拼接 */
                            fetch((it_data->second).data + new_data);
                        }
                        datas.erase(it_data);
                        return OK;
                    } else {
                        /* 没有找到数据的前半部分 */
                        TarData.data = new_data;    TarData.IsEnd = true;
                        TarData.size = -2;  TarData.StartSeq = seq_no;
                        datas.emplace(GetNextSeq(TarData.StartSeq, TarData.data.size()), TarData);
                        return OK;
                    }
                }
                return NO;
            }
            // std::cout << "Tag of this Response is " << temp_pair.first << std::endl;
            /* 其他命令 */
            /* 先检查对应命令是否存在，如果不存在，那么先保存响应 */
            std::map<std::string, Command>::iterator it_com = commands.find(temp_pair.first);
            if(it_com == commands.end()) {
                /* 没有这条命令.则应该先将响应保存 */
                responses.insert(temp_pair);
                return OK;
            }
            /* 有命令 */
            if (temp_pair.second.result == NO) {
                /* 此次命令执行失败，则应该直接将命令从集合中删除 */
                if ((it_com->second).Kind == APPEND) {
                    /* Append命令执行失败，应该将数据从集合中移除 */
                    std::map<u_int32_t, PartData>::iterator it_data = datas.begin();
                    while (it_data != datas.end()) {
                        if ((it_data->second).size == -1) {
                            datas.erase(it_data);
                            return NO;
                        }
                    }
                }
                commands.erase(it_com);
                return NO;
            }
            // printf("Get Response and NOT fetch/list/lsub/status!\n");
            // printf("This Responce can be done and OK!\n");
            /* 命令运行成功，使用跳转表进行命令的解析 */
            Mailbox* tmp = NULL;
            Message* tmp_mail = NULL;
            std::map<u_int32_t, PartData>::iterator it_data;
            switch ((it_com->second).Kind)
            {
            case LOGIN:
                LogIn((it_com->second).args[0], (it_com->second).args[1]);
                break;
            case SELECT:
                Select((it_com->second).args[0], new_data);
                break;
            case CREATE:
                (it_com->second).args[0] = LowerCase((it_com->second).args[0]);
                RootMail.AppendBox((it_com->second).args[0]);
                break;
            case DELETE:
                (it_com->second).args[0] = LowerCase((it_com->second).args[0]);
                RootMail.DeleteBox((it_com->second).args[0]);
                break;
            case RENAME:
                Rename((it_com->second).args[0], (it_com->second).args[0]);
                break;
            case SUBSCR:
                (it_com->second).args[0] = LowerCase((it_com->second).args[0]);
                tmp = RootMail.AppendBox((it_com->second).args[0]);
                if (tmp == NULL) tmp = RootMail.FindBoxByName((it_com->second).args[0]);
                tmp->SetSub(true);
                break;
            case UNSUBS:
                (it_com->second).args[0] = LowerCase((it_com->second).args[0]);
                RootMail.AppendBox((it_com->second).args[0]);
                if (tmp == NULL) tmp = RootMail.FindBoxByName((it_com->second).args[0]);
                tmp->SetSub(false);
                break;
            case APPEND:
                /* append添加命令成功，查看是否有序列号为-1的数据 */
                for (it_data = datas.begin(); it_data != datas.end(); it_data++) {
                    if((it_data->second).size == -1) break ;
                }
                /* 如果有数据，将数据写入，并将响应删除 */
                if (it_data != datas.end()) {
                    tmp_mail = new Message;
                    tmp_mail->SetFullText((it_data->second).data);
                    AppendMail((it_com->second).args[0], tmp_mail);
                    datas.erase(it_data);
                } else return OK;
                break;
            case COPY:
                /* 确保邮箱存在 */
                RootMail.AppendBox((it_com->second).args[1]);
                /* 计算区间 */
                int beg = 0, end = 0, cur_pos = 0;
                while ((it_com->second).args[0][cur_pos] != ':') {
                    beg = (beg<<1) + (beg<<3) + (it_com->second).args[0][cur_pos]-'0';
                    cur_pos++;
                }
                cur_pos++;
                while (cur_pos < (int)(it_com->second).args[0].size()) {
                    end = (end<<1) + (end<<3) + (it_com->second).args[0][cur_pos]-'0';
                    cur_pos++;
                }
                if(end == 0) end = beg;
                CopyMails(beg, end+1, (it_com->second).args[1]);
                break;
            }
            commands.erase(it_com);
            return OK;
        }
    }
}

std::pair<std::string, Response> Session::GetResFromData(std::string& data) {
    int cur_pos = data.size()-1;
    Response new_res;

    /* 如果最后两个为\r\n说明是有响应，否则就是数据，直接返回 */
    if (data[cur_pos] == '\n') cur_pos -= 2;
    else return std::make_pair("", new_res);

    /* 分析响应 */
    std::string res;
    while (cur_pos >= 0 && data[cur_pos] != '\n' && data[cur_pos] != '\r') cur_pos--;
    cur_pos++;
    if (cur_pos < 0) cur_pos = 0;
    res.assign(data.substr(cur_pos));

    /* tag, command, result */
    std::string tag, command, result;
    int beg = 0, end = 0;
    while (end < (int)res.size() && res[end] != ' ') end++;
    tag.assign(res.substr(beg, end-beg));   beg = ++end;
    while (end < (int)res.size() && res[end] != ' ') end++;
    result.assign(res.substr(beg, end-beg));   beg = ++end;
    while (end < (int)res.size() && res[end] != ' ') end++;
    command.assign(res.substr(beg, end-beg));

    if (tag[0] == '*')   return std::make_pair("", new_res);

    /* 构造响应 */
    if(result == "OK")  new_res.result = OK;
    else if(result == "NO" || result == "BAD")  new_res.result = NO;
    else return std::make_pair("", new_res);

    if (cur_pos) data.assign(data.substr(0, cur_pos));
    else data.assign("");
    new_res.data.assign(data);
    if (command == "FETCH" || command == "LIST" || command == "LSUB" || command == "STATUS")
        return std::make_pair(command, new_res);
    return std::make_pair(tag, new_res);
}