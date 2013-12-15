
#include "Timer.h"


Timer::Timer()
{
    gettimeofday(&mTime, nullptr);
}


long Timer::restart()
{
    timeval old = mTime;
    gettimeofday(&mTime, nullptr);
    return (mTime.tv_sec - old.tv_sec) * 1000 + (mTime.tv_usec - old.tv_usec) / 1000;
}
