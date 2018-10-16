
#include "foo.h"
#include <cstdio>

int foo(bool branch)
{
    if(branch)
    {
        std::printf("This line will be untested, so that coverage is not 100%\n");
    }
    else
    {
        std::printf("This is the default behaviour and will be tested\n");
    }
    return 0;
}
