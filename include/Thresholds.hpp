/*****************************************************************/ /**
 * @file   Thresholds.hpp
 * @brief  header file of Thresholds struct
 * 
 * @author ichi-raven
 * @date   April 2025
 *********************************************************************/
#ifndef EVENTRENDERER_INCLUDE_THRESHOLDS_HPP_
#define EVENTRENDERER_INCLUDE_THRESHOLDS_HPP_

struct Thresholds
{
    float positive = 1.0f;
    float negative = 1.0f;
    float sigma    = 0.03f;  // v2e
};

#endif
