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
        _data(0)
    {
    }
    
    BoundingBox _boundingBox;

    int _isLeaf;
    int _parentIndex;
    int _leftChildIndex;
    int _rightChildIndex;
    unsigned int _data;

    // any necessary padding out to 16 bytes to match the GPU's version
    int _padding[3];
};

