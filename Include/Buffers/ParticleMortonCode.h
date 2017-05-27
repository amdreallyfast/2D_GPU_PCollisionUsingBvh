#pragma once

/*------------------------------------------------------------------------------------------------
Description:
    This struct exists for two reasons:
    (1) Various attempts to move particles around on every loop of the parallel sorting routine 
        have demonstrated an ~2/3rds performance drop compared to using a lightweight 
        intermediate structure like this.
    (2) Morton Codes are only used during particle collision detection, so particles should not 
        be required to carry the codes around at all times.
Creator:    John Cox, 5/2017
------------------------------------------------------------------------------------------------*/
struct ParticleMortonCode
{
    /*--------------------------------------------------------------------------------------------
    Description:
        Initializes members to 0.
    Parameters: None
    Returns:    None
    Creator:    John Cox, 5/2017
    --------------------------------------------------------------------------------------------*/
    ParticleMortonCode::ParticleMortonCode() :
        _mortonCode(0),
        _preSortedParticleIndex(0)
    {
    }

    // used for a "radix" algorithm, so this should be unsigned
    unsigned int _mortonCode;

    // used to fish out the unsorted particle that this object was created from so that it can 
    // be moved to the sorted position
    int _preSortedParticleIndex;

    // no GLSL-native structures on the shader side, so no padding necessary
};