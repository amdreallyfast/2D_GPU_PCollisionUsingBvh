#pragma once

#include "Include/Buffers/SSBOs/SsboBase.h"

/*------------------------------------------------------------------------------------------------
Description:
    Encapsulates the SSBO that stores Particles.  It generates a chunk of space on the GPU that 
    is big enough to store the requested number of particles, and since this buffer will be used 
    in a drawing shader as well as a compute shader, this class will also set up the VAO and the 
    vertex attributes.
Creator:    John Cox, 3/2017
------------------------------------------------------------------------------------------------*/
class ParticleSsbo : public SsboBase
{
public:
    ParticleSsbo(unsigned int numItems);
    virtual ~ParticleSsbo() = default;
    using SharedPtr = std::shared_ptr<ParticleSsbo>;
    using SharedConstPtr = std::shared_ptr<const ParticleSsbo>;

    void ConfigureConstantUniforms(unsigned int computeProgramId) const override;
    void ConfigureRender(unsigned int drawStyle) override;
};