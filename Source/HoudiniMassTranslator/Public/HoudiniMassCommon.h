// Copyright (c) <2025> Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

#pragma once

#define HOUDINI_LANE_PROFILE_PREFIX                  TEXT("LP_HE_")
#define HAPI_ATTRIB_UNREAL_OUTPUT_ZONE_SHAPE         "unreal_output_zone_shape"
#define HAPI_ATTRIB_UNREAL_ZONE_SHAPE_TYPE           "unreal_zone_shape_type"  // both int and string are supported
#define HAPI_ATTRIB_UNREAL_ZONE_ZHAPE_TAGS           "unreal_zone_shape_tags"
#define HAPI_ATTRIB_UNREAL_ZONE_LANE_PROFILE         "unreal_zone_lane_profile"   // Define lanes, use d[]@unreal_zone_lane_profile to find or create LaneProfiles
#define HAPI_ATTRIB_UNREAL_ZONE_LANE_PROFILE_NAME    "unreal_zone_lane_profile_name"   // use s@unreal_zone_lane_profile_name to specify exists LaneProfiles, or name the created LaneProfiles
