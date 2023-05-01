#pragma once

class ICondition
{
public:
    virtual ~ICondition() = default;
    
    virtual void NotifyAll() noexcept = 0;
    virtual void NotifyOne() noexcept = 0;
};
