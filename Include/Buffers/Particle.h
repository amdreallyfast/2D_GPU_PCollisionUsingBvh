#pragma once

#include "ThirdParty/glm/vec4.hpp"

/*------------------------------------------------------------------------------------------------
Description:
    This is a simple structure that says where a particle is, where it is going, and whether it
    has gone out of bounds ("is active" flag).  That flag also serves to prevent all particles
    from going out all at once upon creation by letting the "particle updater" regulate how many
    are emitted every frame.
Creator:    John Cox (7-2-2016)
------------------------------------------------------------------------------------------------*/
struct Particle
{
    /*-------------------------------------------------------------------------------------------
    Description:
        Sets initial values.  The glm structures have their own zero initialization and I don't 
        care about the integer padding to "is active".  The "is active" flag starts at 0 because 
        I want the first run of the particles to be reset to an emitter.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 10-2-2016
    -------------------------------------------------------------------------------------------*/
    Particle() :
        // glm structures already have "set to 0" constructors
        _mass(0.3f),
        _collisionRadius(0.01f),
        _mortonCode(0),
        _collideWithThisParticleIndex(0),
        _numberOfNearbyParticles(0),
        _isActive(0)
    {
    }

    // TODO: do "previous position" too (for collision detection with geometry lines or planes)

    // even though this is a 2D program, I wasn't able to figure out the byte misalignments 
    // between C++ and GLSL (every variable is aligned on a 16byte boundry, but adding 2-float 
    // padding to glm::vec2 didn't work and the compute shader just didn't send any particles 
    // anywhere), so I just used glm::vec4 
    // Note: Yes, I did take care of the byte offset in the vertex attrib pointer.
    glm::vec4 _position;
    glm::vec4 _velocity;

    // all particles have identical mass for now
    // TODO: ??change to micrograms for air particles? nanograms??
    float _mass;

    // used for collision detection because a particle's position is float values, so two
    // particles' position are almost never going to be exactly equal
    float _collisionRadius;

    // generated in the shader and stored for later use
    // Note: This value allows for proximity comparison of two 3-dimensional coordinates as if 
    // they were 1-dimensional coordinates.  Particles are sorted over this value.
    // Also Note: Must be an unsigned int.  The calculation assumes that all bits are relavent 
    // to the numerical quantity.  A sign bit is not, so it must not be an signed integer.  It 
    // may not be a problem since this program uses a 30bit Morton Code and the sign bit is the 
    // 32nd bit, but still keep it unsigned because that is what is expected in the calculation.
    unsigned int _mortonCode;

    // recorded in DetectCollisions.comp, used in ResolveCollisions.comp
    int _collideWithThisParticleIndex;

    // used to determine color
    int _numberOfNearbyParticles;

    // Note: Booleans cannot be uploaded to the shader 
    // (https://www.opengl.org/sdk/docs/man/html/glVertexAttribPointer.xhtml), so send the 
    // "is active" flag as an integer.  
    int _isActive; 
    
    // any necessary padding out to 16 bytes to match the GPU's version
    int _padding[2];
};
