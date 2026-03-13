#include "input_thread.h"

#include <atomic>
#include <iostream>
#include <thread>
#include <chrono>
#include <stop_token>

#ifdef _MSC_VER
#include <conio.h>   // For _kbhit() and _getch()
#else
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#ifndef _MSC_VER
// Linux helper: Check if a key was pressed without blocking
int linux_kbhit()
{
    struct termios oldt, newt;
    int ch;
    int oldf;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);

    if (ch != EOF)
    {
        ungetc(ch, stdin);
        return 1;
    }
    return 0;
}
#endif

void inputThreadLoop(std::stop_token st, std::atomic<bool>& keepRunning)
{
    std::cout << "Input thread started. Press 'q' to stop dispatcher." << std::endl;

    while (!st.stop_requested() && keepRunning)
    {
#ifdef _MSC_VER
        if (_kbhit())
        {
            if (_getch() == 'q') keepRunning = false;
        }
#else
        if (linux_kbhit())
        {
            if (getchar() == 'q') keepRunning = false;
        }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "Shutdown signal received..." << std::endl;
}