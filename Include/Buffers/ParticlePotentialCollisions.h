#pragma once

/*------------------------------------------------------------------------------------------------
Description:   
    Must match the corresponding structure in ParticlePotentialCollisionsBuffer.comp.

    This object is the blueprint for an array of potential collisions that each particle is 
    engaged in.  This buffer was created because potential collision candidates is used only 
    during collision detection and handling, so it did not make much sense for a particle to be 
    carrying around a list of potential collisions, and there is the payoff of encapsulation of 
    responsibility by not having collision detection trying to sort out which particle should 
    actually be collided with.  With this buffer, collision detection can simply fill out one of 
    these structures for each particle, and then collision resolution can use these values as it 
    wishes.
Creator:    John Cox, 5/2017
------------------------------------------------------------------------------------------------*/
struct ParticlePotentialCollisions
{
    int 
};

