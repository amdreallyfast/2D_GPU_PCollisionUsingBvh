#pragma once

/*------------------------------------------------------------------------------------------------
Description:   
    A convenience structure for BvhNode.  Must match the corresponding structure in 
    BvhNodeBuffer.comp.
Creator:    John Cox, 5/2017
------------------------------------------------------------------------------------------------*/
struct BoundingBox
{
    BoundingBox() :
        _left(0.0f),
        _right(0.0f),
        _top(0.0f),
        _bottom(0.0f)
    {
    }

    float _left;
    float _right;
    float _top;
    float _bottom;
};

#include "ThirdParty/glm/vec2.hpp"
/*------------------------------------------------------------------------------------------------
Description:   
    Must match the corresponding structure in BvhNodeBuffer.comp.
    Stores info about a single node in the BVH.  Can be either an internal node or a leaf node.  
    If internal, then its children are either leaf nodes or other internal nodes.  If a leaf 
    node, then it will have _data to analyze.
Creator:    John Cox, 5/2017
------------------------------------------------------------------------------------------------*/
struct BvhNode
{
    BvhNode() :
        _isLeaf(0),
        _parentIndex(-1),
        _leftChildIndex(-1),
        _rightChildIndex(-1),
        _threadEntranceCounter(0),
        _data(0)
    {
    }
    
    BoundingBox _boundingBox;

    int _isLeaf;
    int _parentIndex;
    int _leftChildIndex;
    int _rightChildIndex;
    int _threadEntranceCounter;
    unsigned int _data;

    // Note: Lesson learned about buffer padding.  It is only necessary if the structure defines 
    // a vec* (yes, vec2 included; I tested it) or mat*.  The CPU side can declare whatever it 
    // wants, but if the GLSL structure contains one of them, then the shader compiler will 
    // expect the buffer that contains those structures to be 16-byte aligned, as if it were an 
    // entire array of the things.  Th Particle structure has a couple vec4's, so it is under 
    // this restriction and the CPU-side structure must be padded to a 16-byte alignment.  If 
    // there are only primitives in the structure definition, like this BvhNode structure, then 
    // no padding necessary.
};

