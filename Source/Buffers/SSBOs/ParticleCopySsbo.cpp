#include "Include/Buffers/SSBOs/ParticleCopySsbo.h"

#include <vector>

#include "ThirdParty/glload/include/glload/gl_4_4.h"
#include "Shaders/Compute/ComputeHeaders/SsboBufferBindings.comp"

#include "Include/Particles/Particle.h"


/*------------------------------------------------------------------------------------------------
Description:
    Initializes base class, then gives derived class members initial values and allocates space
    for the SSBO.
Parameters:
    numItems    However many Particles user wants to store.
Returns:    None
Creator:    John Cox, 4/2017
------------------------------------------------------------------------------------------------*/
ParticleCopySsbo::ParticleCopySsbo(unsigned int numItems) :
    SsboBase()  // generate buffers
{
    std::vector<Particle> v(numItems);

    // now bind this new buffer to the dedicated buffer binding location
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, PARTICLE_COPY_BUFFER_BINDING, _bufferId);

    // and fill it with new data
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, _bufferId);
    glBufferData(GL_SHADER_STORAGE_BUFFER, v.size() * sizeof(Particle), v.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // ParticleBuffer already has an initializer for the size of the buffer, so don't bother
}
