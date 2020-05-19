#include<stdlib.h>
#include<string>
using namespace std;

int main(){
    string str = "killall process";
    const char *command = str.c_str();
    system(command);
}
