// REQUIRES Versoin.comp
// REQUIRES CountNearbyParticlesLimits.comp

// Note: The vec2's are in window space (both X and Y on the range [-1,+1])
// Also Note: The vec2s are provided as vec4s on the CPU side and specified as such in the 
// vertex array attributes, but it is ok to only take them as a vec2, as I am doing for this 
// 2D demo.
layout (location = 0) in vec4 pos;  
layout (location = 1) in vec4 vel;  
layout (location = 2) in float mass;
layout (location = 3) in float collisionRadius;
layout (location = 4) in uint mortonCode;
layout (location = 5) in int collideWithThisParticleIndex;
layout (location = 6) in int numberOfNearbyParticles;
layout (location = 7) in int isActive;

// must have the same name as its corresponding "in" item in the frag shader
smooth out vec4 particleColor;

void main()
{
    if (isActive == 0)
    {
        // the Z buffer in this 2D demo is of depth 1, so putting the innactive particle out of 
        // range should make it disappear entirely
        particleColor = vec4(0.0f, 0.0f, 0.0f, 0.0f);   // black
        gl_Position = vec4(pos.xy, -1.1f, 1.0f);

//        // for debugging
//        particleColor = vec4(1.0f, 0.0f, 1.0f, 1.0f);
//        gl_Position = vec4(pos.xy, -0.9f, 1.0f);
    }
    else
    {
        // mix red, green, and blue based on the number of nearby particles
        // Note: Mix with the following color convention:
        // - Red -> high pressure
        // - Green -> medium pressure
        // - Blue -> low pressure
        vec4 red = vec4(1.0f, 0.0f, 0.0f, 1.0f);
        vec4 green = vec4(0.0f, 1.0f, 0.0f, 1.0f);
        vec4 blue = vec4(0.0f, 0.0f, 1.0f, 1.0f);

        // min/mid/max possible nearby particles
        float min = 0;
        float mid = 20;
        float max = 40;


        if (collideWithThisParticleIndex == -1)
        //if (numberOfNearbyParticles == -1)
        {
            particleColor = vec4(1.0f, 1.0f, 1.0f, 1.0f);
        }
        else
        {
            float blendValue = float(numberOfNearbyParticles);
            float fractionLowToMid = (blendValue - min) / (mid - min);
            fractionLowToMid = clamp(fractionLowToMid, 0.0f, 1.0f);
    
            float fractionMidToHigh = (blendValue - mid) / (max - mid);
            fractionMidToHigh = clamp(fractionMidToHigh, 0.0f, 1.0f);
    
            // cast boolean to float (1.0f == true, 0.0f == false)
            // Note: There are two possible linear blends: blue->green and green->red.  This color 
            // blending is not like blending three points on a triangle, but it is three points on a 
            // 1-dimensional number line, so need to differentiate between two linear blends.
            float pressureIsLow = float(blendValue < mid);
            vec4 lowToMidPressureColor = mix(blue, green, fractionLowToMid);
            vec4 midToHighPressureColor = mix(green, red, fractionMidToHigh);
            particleColor = 
                (pressureIsLow * lowToMidPressureColor) + 
                ((1 - pressureIsLow) * midToHighPressureColor);
        }


        // Note: The W position seems to be used as a scaling factor (I must have forgotten this 
        // from the graphical math; it's been awhile since I examined it in detail).  If I do any 
        // position normalization, I should make sure that gl_Position's W value is always 1.
        gl_Position = vec4(pos.xy, -0.7f, 1.0f);    
    }
}

