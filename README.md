# Houdini Mass Translator

Welcome to the [repository](https://github.com/AdrianPanGithub/HoudiniMassTranslator) for the Houdini Mass Translator For Unreal.

This plug-in provides seamless integration between Houdini Engine and Zone Graph, allow zone shape input and output.

# Compatibility

This plug-in is reply on **my custom** [HoudiniEngineForUnreal](https://github.com/AdrianPanGithub/HoudiniEngineForUnreal), so same compatibility as the Houdini Engine.

# Installation
01. In this GitHub [repository](https://github.com/AdrianPanGithub/HoudiniMassTranslator), click **Releases** on the right side. 
02. Download the Houdini Mass Transaltor zip file that matches your Unreal Engine Version.  
03. Extract the **HoudiniMassTranslator** and **HoudiniEngine** to the **Plugins** of your Unreal Project Directory.

    e.g. `C:/Unreal Projects/MyGameProject/Plugins/HoudiniMassTranslator` and `C:/Unreal Projects/MyGameProject/Plugins/HoudiniEngine`

# Example

01. Download the **CitySample** demo for Unreal Engine 5.4 or later from Epic Store.
02. Put these two plug-ins into **CitySample/Plugins folder**.

03. Open `/HoudiniMassTranslator/Example/HoudiniTrafficDemo` in the content of this plug-in, and simulate.

# Usage Brief

Support both input and output of mass ai zone shapes

There are some attributes for zone shape input and output

i@**unreal_output_zone_shape**

    = 1 on detail, all curves will output as zone shapes
i@**unreal_zone_shape_type** / s@**unreal_zone_shape_type**

    define whether a curve is represented as zone shape spline or polygon (intersection).
s[]@**unreal_zone_shape_tags**

    specify zone graph tags on zone shape.
d[]@**unreal_zone_lane_profile**

    Represent Lanes. Will find or create lane profiles based on this attribute when output. could be both on point and prim. Please click menu "Build/Clean Up Houdini Lane Profiles" at last.
s@**unreal_zone_lane_profile_name**

    Will find lane profiles based on this attribute, could be both on point and prim.