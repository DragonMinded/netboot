#include <stdio.h>
#include <stdint.h>
#include <naomi/video.h>

class Counter
{
    public:
        Counter();
        ~Counter();

        int next();

    private:
        bool _init;
        unsigned int _count;
};

Counter::Counter()
{
    _init = true;
    _count = 0;
}

Counter::~Counter()
{
    _init = false;
}

int Counter::next()
{
    if (!_init)
    {
        return -1;
    }
    else
    {
        return _count++;
    }
}

Counter gCounter;

extern "C" void main()
{
    video_init(VIDEO_COLOR_1555);

    while ( 1 )
    {
        // Draw a few simple things on the screen.
        video_fill_screen(rgb(48, 48, 48));
        video_fill_box(20, 20, 100, 100, rgb(0, 0, 0));
        video_draw_line(20, 20, 100, 100, rgb(0, 255, 0));
        video_draw_line(100, 20, 20, 100, rgb(0, 255, 0));
        video_draw_line(20, 20, 100, 20, rgb(0, 255, 0));
        video_draw_line(20, 20, 20, 100, rgb(0, 255, 0));
        video_draw_line(100, 20, 100, 100, rgb(0, 255, 0));
        video_draw_line(20, 100, 100, 100, rgb(0, 255, 0));
        video_draw_debug_text(20, 180, rgb(255, 255, 255), "It appears that C++ is working!");

        // Display a liveness counter, but using C++ to track.
        video_draw_debug_text(20, 220, rgb(200, 200, 20), "Aliveness counter: %d", gCounter.next());
        video_display_on_vblank();
    }
}

extern "C" void test()
{
    video_init(VIDEO_COLOR_1555);

    while ( 1 )
    {
        video_fill_screen(rgb(48, 48, 48));
        video_draw_debug_text(320 - 56, 236, rgb(255, 255, 255), "test mode stub");
        video_display_on_vblank();
    }
}
