/** @file main.cpp
 * Just a simple hello world using libfmt
 */
// The previous block is needed in every file for which you want to generate documentation

#include <cstdio>

int main(int argc, char* argv[])
{
    if (argc)
    {
        printf("hello world from %s!", argv[0]);
    }
    return 0;
}
