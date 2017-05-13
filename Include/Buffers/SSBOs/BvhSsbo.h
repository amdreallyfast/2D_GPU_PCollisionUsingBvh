#pragma once


// Why have both leaves and internal nodes instead of just internal nodes and rely on the ParticleBuffer as the leaves?  Because generating the bounding boxes for the tree requires traversing from the leaves up to the root.  If the Particle objects themselves were the leaves, then they would have to carry around a "parent node index" member, and that member is only relevant when dealing with the BVH, so I decided that Particle objects will not carry it.  Also, leaf nodes that share the same structure as the internal nodes makes bounding box generation and comparison easier.
