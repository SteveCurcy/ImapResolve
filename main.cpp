#include <cstdio>
#include "PeelHeader.h"
#include "ImapResolve.h"

/*----------------------
* 用argv接收要处理的文件名
* 文件需要使用绝对路径
* --------------------*/
int main(int args, char* argv[]) {
    Package data("all_test.pcap");
    data.GetData();
    return 0;
}