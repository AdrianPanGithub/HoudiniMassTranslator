# Houdini Mass Translator

Welcome to the [repository](https://github.com/AdrianPanGithub/HoudiniMassTranslator) for the Houdini Mass Translator For Unreal.

This plug-in provides seamless integration between Houdini Engine and Zone Graph, allow zone shape input and output.
Also see [Tutorial](https://youtu.be/HAM8_OP_Fyc?si=K_1HXkTBwF1rLYVB)

# Compatibility

This plug-in relies on **my custom** [HoudiniEngineForUnreal](https://github.com/AdrianPanGithub/HoudiniEngineForUnreal), so same [compatibility](https://github.com/AdrianPanGithub/HoudiniEngineForUnreal#compatibility) as the Houdini Engine. (Examples only run with >= 5.4)

# Installation
01. In this GitHub [repository](https://github.com/AdrianPanGithub/HoudiniMassTranslator), click [Releases](https://github.com/AdrianPanGithub/HoudiniMassTranslator/releases) on the right side. 
02. Download the Houdini Mass Translator zip file that matches your Unreal Engine Version.
03. Extract the **HoudiniMassTranslator** and **HoudiniEngine** to the **Plugins** of your Unreal Project Directory.

    e.g. `C:/Unreal Projects/MyGameProject/Plugins/HoudiniMassTranslator` and `C:/Unreal Projects/MyGameProject/Plugins/HoudiniEngine`

# Tutorial/Example
[Tutorial](https://youtu.be/HAM8_OP_Fyc?si=K_1HXkTBwF1rLYVB) Require Unreal Engine >= 5.4 to run the example:
01. Download the [CitySample](https://www.fab.com/listings/4898e707-7855-404b-af0e-a505ee690e68) project (Unreal Engine >= 5.4) from Fab Store.
02. Put these two plug-ins into **CitySample/Plugins folder**.

03. Open `/HoudiniMassTranslator/Example/HoudiniTrafficDemo` in the content of this plug-in, and simulate.

Also see what can be achieved by Only using your HDAs and these two unreal plugins: [City Toolchains](https://youtu.be/5Vp5nAFq1X8?si=IGSDG4cUdsefwn5x)

# Usage Brief

Support both input and output of mass ai zone shapes

Many properties on zone shape and zone shape points can be set by @**unreal_uproperty_***, such as i@unreal_uproperty_**PolygonRoutingType**, f@unreal_uproperty_**InnerTurnRadius**.

Here are some specific attributes for zone shape input and output:

i@**unreal_output_zone_shape**

    = 1 on detail, all curves will output as zone shapes
i@**unreal_zone_shape_type** / s@**unreal_zone_shape_type**

    define whether a curve is represented as zone shape spline or polygon (intersection).
s[]@**unreal_zone_shape_tags**

    specify zone graph tags on zone shape.
d[]@**unreal_zone_lane_profile**

    Represent Lanes. Will find or create lane profiles based on this attribute when output. could be both on point and prim. Please click menu "Build/Clean Up Houdini Lane Profiles" at last.
s@**unreal_zone_lane_profile_name**

    Will find lane profiles based on this attribute, could be both on point and prim at same time.
p@**rot**

    Specify polygon zone shape point directions.