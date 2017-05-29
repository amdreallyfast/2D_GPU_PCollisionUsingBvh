#pragma once

#include "Include/Buffers/SSBOs/SsboBase.h"
#include "Include/Geometry/PolygonFace.h"
#include <vector>

/*-----------------------------------------------------------------------------------------------
Description:
    Sets up the Shader Storage Block Object for a 2D polygon.  The polygon will be used in both 
    the compute shader and in the geometry render shader.
Creator: John Cox, 9-8-2016
-----------------------------------------------------------------------------------------------*/
class PolygonSsbo : public SsboBase
{
public:
    PolygonSsbo(int numPolygons);
    PolygonSsbo(const std::vector<PolygonFace> &faceCollection);
    virtual ~PolygonSsbo() = default;
    using SharedPtr = std::shared_ptr<PolygonSsbo>;
    using SharedConstPtr = std::shared_ptr<const PolygonSsbo>;

    void ConfigureConstantUniforms(unsigned int computeProgramId) const override;
    void ConfigureRender() override;

    unsigned int NumItems() const;

private:
    unsigned int _numItems;

};

