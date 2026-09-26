#pragma once

class FuserController
{
public:
    void Start()
    {
        running_ = true;
    }
    void Stop()
    {
        running_ = false;
    }
    bool IsRunning() const
    {
        return running_;
    }

private:
    bool running_ = false;
};
