#include <iostream>
#include <string>
#include "filewatcher.h"

using namespace std;

int main(int argc, char** argv)
{
    FileWatcher watcher;
    int pos = -1;
    for(int i = strlen(argv[0]); i >= 0; --i) {
        if (argv[0][i] == '\\' || argv[0][i] == ':') {
            pos = i;
            break;
        }
    }
    char dir[MAX_PATH] = {0};
    strncpy(dir, argv[0], pos+1);
    watcher.Init(dir);
    watcher.addpathlistener("test.txt", [](const std::string& path){
        cout << path << " has modify" << endl;
    });

    getchar();
    watcher.Unint();
    return 0;
}
