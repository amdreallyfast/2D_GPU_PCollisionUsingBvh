#pragma once

#include "Include/Buffers/SSBOs/SsboBase.h"


/*------------------------------------------------------------------------------------------------
Description:
    Same as the ParticleSsbo, but called by a different name because it is a different 
    buffer that is meant exclusively for use during sorting.  There is no swapping in parallel.
    Race conditions are guaranteed.  So instead need to copy the original data to a copy buffer 
    and then have each thread copy the copied data back to the original buffer, but in a new 
    sorted index.
Creator:    John Cox, 4/2017
------------------------------------------------------------------------------------------------*/
class ParticleCopySsbo : public SsboBase
{
public:
    ParticleCopySsbo(unsigned int numItems);
    virtual ~ParticleCopySsbo() = default;
    using SHARED_PTR = std::shared_ptr<ParticleCopySsbo>;

private:
};