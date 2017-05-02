#include "Include/Buffers/SSBOs/ParticleSsbo.h"

#include <vector>
#include <random>   // for generating initial data
#include <time.h>

#include "ThirdParty/glload/include/glload/gl_4_4.h"
#include "Shaders/Compute/ComputeHeaders/SsboBufferBindings.comp"
#include "Shaders/Compute/ComputeHeaders/CrossShaderUniformLocations.comp"

#include "Include/Particles/Particle.h"



/*------------------------------------------------------------------------------------------------
Description:
    Particle resetting on the GPU requires several random numbers.  GLSL doesn't have GPU 
    clock-reading functions, so the next best thing is a chaotic hash function 
    (see Random.comp).  Being a hash function though, if the same values are put into it (like 
    the Particle's default position XY and velocity XY values of 0), then the result won't 
    change.  

    This method gives the particle position and velocity initial random values.

    All particles start as "inactive", so they'll be reset immediately.  This method just gives 
    them a bump toward randomization.
Parameters: 
    initThese   Self-explanatory.
Returns:    None
Creator:    John Cox, 4/2017
------------------------------------------------------------------------------------------------*/
static void InitializeWithRandomData(std::vector<Particle> &initThese)
{
    srand(static_cast<unsigned int>(time(0)));

    float inverseRandMax = 1.0f / RAND_MAX;
    for (size_t particleIndex = 0; particleIndex < initThese.size(); particleIndex++)
    {
        // randomized X and Y values
        // Note: Once the sim gets running, the particles will be in window space (X and Y along 
        // the range [-1,+1]), but for initialiation, this 0-1 range will suffice for 
        // randomizing float values.  The 0-1 range isn't necessary, but the position and 
        // velocity values are floats, and dividing by RAND_MAX is an easy way to get a float.  
        // It just so happens to be along the range 0-1.
        initThese[particleIndex]._position.x = static_cast<float>(rand()) * inverseRandMax;
        initThese[particleIndex]._position.y = static_cast<float>(rand()) * inverseRandMax;
        
        // just outside the Z buffer (0 (far) to -1 (near)), so it won't draw
        initThese[particleIndex]._position.z = +0.1f;

        initThese[particleIndex]._velocity.x = static_cast<float>(rand()) * inverseRandMax;
        initThese[particleIndex]._velocity.y = static_cast<float>(rand()) * inverseRandMax;
    }
}


/*------------------------------------------------------------------------------------------------
Description:
    Initializes base class, then gives derived class members initial values and allocates space 
    for the SSBO.

    Generates random initial particle positions and velocities, but all particles are still 
    default inactive.  See description in InitializeWithRandomData(...).

    Uploads the initialized particles to the SSBOs newly allocated buffer memory.

Parameters: 
    numItems    However many instances of Particle the user wants to store.
Returns:    None
Creator:    John Cox, 4/2017
------------------------------------------------------------------------------------------------*/
ParticleSsbo::ParticleSsbo(unsigned int numItems) :
    SsboBase(),  // generate buffers
    _numItems(numItems)
{
    // each particle is 1 vertex, so for particles, "num vertices" == "num items"
    // Note: This can't be set in the class initializer list.  The class initializer list is for 
    // members of this class only (ParticleSsbo), not for base class members. 
    _numVertices = numItems;

    std::vector<Particle> v(numItems);
    InitializeWithRandomData(v);

    // now bind this new buffer to the dedicated buffer binding location
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, PARTICLE_BUFFER_BINDING, _bufferId);

    // and fill it with the new data
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, _bufferId);
    glBufferData(GL_SHADER_STORAGE_BUFFER, v.size() * sizeof(Particle), v.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

/*------------------------------------------------------------------------------------------------
Description:
    Defines the buffer's size uniform in the specified shader.  It uses the #define'd uniform 
    location found in CrossShaderUniformLocations.comp.

    If the shader does not have the uniform or if the shader compiler optimized it out, then 
    OpenGL will complain about not finding it.  Enable debugging in main() in main.cpp for more 
    detail.
Parameters: 
    computeProgramId    Self-explanatory.
Returns:    
    See Description.
Creator:    John Cox, 3/2017
------------------------------------------------------------------------------------------------*/
void ParticleSsbo::ConfigureConstantUniforms(unsigned int computeProgramId) const
{
    // the uniform should remain constant after this 
    glUseProgram(computeProgramId);
    glUniform1ui(UNIFORM_LOCATION_PARTICLE_BUFFER_SIZE, _numItems);
    glUseProgram(0);
}

/*------------------------------------------------------------------------------------------------
Description:
    A simple getter for the value that was passed in on creation.
Parameters: None
Returns:    
    See Description.
Creator:    John Cox, 3/2017
------------------------------------------------------------------------------------------------*/
unsigned int ParticleSsbo::NumItems() const
{
    return _numItems;
}

/*------------------------------------------------------------------------------------------------
Description:
    Sets up the vertex attribute pointers for this SSBO's VAO.
Parameters: 
    renderProgramId     Self-explanatory
    drawStyle           Expected to be GL_POINTS.
Returns:    None
Creator:    John Cox, 11-24-2016
------------------------------------------------------------------------------------------------*/
void ParticleSsbo::ConfigureRender(unsigned int renderProgramId, unsigned int drawStyle)
{
    _drawStyle = drawStyle;

    // set up the VAO
    // now set up the vertex array indices for the drawing shader
    // Note: MUST bind the program beforehand or else the VAO binding will blow up.  It won't 
    // spit out an error but will rather silently bind to whatever program is currently bound, 
    // even if it is the undefined program 0.
    glUseProgram(renderProgramId);
    glBindVertexArray(_vaoId);

    // the vertex array attributes only work on whatever is bound to the array buffer, so bind 
    // shader storage buffer to the array buffer, set up the vertex array attributes, and the 
    // VAO will then use the buffer ID of whatever is bound to it
    glBindBuffer(GL_ARRAY_BUFFER, _bufferId);
    // do NOT call glBufferData(...) because it was called earlier for the shader storage buffer

    // vertex attribute order is same as the structure
    // - glm::vec4 _position;
    // - glm::vec4 _velocity;
    // - float _mass;
    // - float _collisionRadius;
    // - unsigned int _hasCollidedAlreadyThisFrame;
    // - int _isActive;

    unsigned int vertexArrayIndex = 0;
    unsigned int bufferStartOffset = 0;
    unsigned int bytesPerStep = sizeof(Particle);
    unsigned int sizeOfLastItem = 0;

    // position
    GLenum itemType = GL_FLOAT;
    unsigned int numItems = sizeof(Particle::_position) / sizeof(float);
    glEnableVertexAttribArray(vertexArrayIndex);
    glVertexAttribPointer(vertexArrayIndex, numItems, itemType, GL_FALSE, bytesPerStep, (void *)bufferStartOffset);
    sizeOfLastItem = sizeof(Particle::_position);

    // velocity
    itemType = GL_FLOAT;
    numItems = sizeof(Particle::_velocity) / sizeof(float);
    bufferStartOffset += sizeOfLastItem;
    vertexArrayIndex++;
    glEnableVertexAttribArray(vertexArrayIndex);
    glVertexAttribPointer(vertexArrayIndex, numItems, itemType, GL_FALSE, bytesPerStep, (void *)bufferStartOffset);
    sizeOfLastItem = sizeof(Particle::_velocity);

    // numberOfNearbyParticles
    itemType = GL_UNSIGNED_INT;
    numItems = sizeof(Particle::_numberOfNearbyParticles) / sizeof(unsigned int);
    bufferStartOffset += sizeOfLastItem;
    vertexArrayIndex++;
    glEnableVertexAttribArray(vertexArrayIndex);
    glVertexAttribIPointer(vertexArrayIndex, numItems, itemType, bytesPerStep, (void *)bufferStartOffset);
    sizeOfLastItem = sizeof(Particle::_numberOfNearbyParticles);

    // mass
    itemType = GL_FLOAT;
    numItems = sizeof(Particle::_mass) / sizeof(float);
    bufferStartOffset += sizeOfLastItem;
    vertexArrayIndex++;
    glEnableVertexAttribArray(vertexArrayIndex);
    glVertexAttribPointer(vertexArrayIndex, numItems, itemType, GL_FALSE, bytesPerStep, (void *)bufferStartOffset);
    sizeOfLastItem = sizeof(Particle::_mass);

    // collision radius
    itemType = GL_FLOAT;
    numItems = sizeof(Particle::_collisionRadius) / sizeof(float);
    bufferStartOffset += sizeOfLastItem;
    vertexArrayIndex++;
    glEnableVertexAttribArray(vertexArrayIndex);
    glVertexAttribPointer(vertexArrayIndex, numItems, itemType, GL_FALSE, bytesPerStep, (void *)bufferStartOffset);
    sizeOfLastItem = sizeof(Particle::_collisionRadius);

    // morton coode
    itemType = GL_UNSIGNED_INT;
    numItems = sizeof(Particle::_mortonCode) / sizeof(unsigned int);
    bufferStartOffset += sizeOfLastItem;
    vertexArrayIndex++;
    glEnableVertexAttribArray(vertexArrayIndex);
    glVertexAttribIPointer(vertexArrayIndex, numItems, itemType, bytesPerStep, (void *)bufferStartOffset);
    sizeOfLastItem = sizeof(Particle::_mortonCode);

    // "has already collided this frame" flag
    itemType = GL_UNSIGNED_INT;
    numItems = sizeof(Particle::_hasCollidedAlreadyThisFrame) / sizeof(unsigned int);
    bufferStartOffset += sizeOfLastItem;
    vertexArrayIndex++;
    glEnableVertexAttribArray(vertexArrayIndex);
    glVertexAttribIPointer(vertexArrayIndex, numItems, itemType, bytesPerStep, (void *)bufferStartOffset);
    sizeOfLastItem = sizeof(Particle::_hasCollidedAlreadyThisFrame);

    // "is active" flag
    itemType = GL_INT;
    numItems = sizeof(Particle::_isActive) / sizeof(int);
    bufferStartOffset += sizeOfLastItem;
    vertexArrayIndex++;
    glEnableVertexAttribArray(vertexArrayIndex);
    glVertexAttribIPointer(vertexArrayIndex, numItems, itemType, bytesPerStep, (void *)bufferStartOffset);
    sizeOfLastItem = sizeof(Particle::_isActive);

    // cleanup
    glBindVertexArray(0);   // unbind this BEFORE the array or else the VAO will bind to buffer 0
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);    // render program
}
