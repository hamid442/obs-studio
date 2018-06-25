# Shader Filter
Rapidly prototype and create graphical effects using OBS's shader syntax.

# Awknowledgments
https://github.com/nleseul/obs-shaderfilter most of the underlying code was already hashed out by this wonderful plugin, this branch/plugin takes this plugin a few steps furthur.

# Notes
Unlike nleseul's plugin the shader file is not considered to be templated ***ever***. While I found this function neat to save space in his original gui, it was far simpler and better to deal w/ by creating shader files and using the browse file feature in combination w/ the reload button to make quick changes.

# Usage
See https://github.com/nleseul/obs-shaderfilter for the basics of OBS's graphical syntax. This plugin tweaks the original to support "annotations".

Annotations are blocks wrapped in <> which are used to describe the gui for the plugin. This plugin will read the file, extract the
variables necessary for the filter to function and prepare a gui based on the annotations given.

For int and floating point variables, you have the selection of a numerical up/down combo box, a slider (w/ the up/down combo box) or a list.

A quick example sliders and combo boxes:
```c
uniform int shadow_x <bool is_slider = true; int min = 0; int max = 10; int step = 1;>;
```

Boolean, integer and floating point values are automatically cast using the type of the variable the gui is being made for.
For example had min, max and/or step been floating point values they'd eventually be cast as an integer to fit the gui's control parameters.
Designer Beware!
EG
```c
uniform int shadow_x <bool is_slider = true; int min = 0; float max = 10.3; int step = 1;>;
```
Will result in the same property being presented in the gui.

For lists, min, max and step are ignored, instead the plugin will read through all of the annotations following the format:
```c
uniform int shadow_x <bool is_list = true; int list_data_1 = 2; string list_item_1_name = "2";>;
```

Order in this case is important, the plugin only cares for annotations w/ the name prefixed list_data_ and a similarly paired string annotation postfixed w/ _name.
The plugin will not reorder the list, instead it'll add the items as encountered. The names of the annotations can be therefore a bit more descriptive of what they may be
for.

EG
```c
uniform int shape <bool is_list = true; int list_data_square = 2; string list_item_square_name = "Square";>;
```

Another addition to this plugin is the support of `int2, int3, int4, float2, float3, float4` types. These will add one to four gui's
respectively of the associated type in order to control each component of the type's array.

A caveat remains with the `float4` type. This type in the original plugin is assumed to be a color and this assumption is maintained.
In order for it to be treated as a vector like the rest the annotation `is_vec4` should be used.

```c
uniform float4 direction <bool is_vec4 = true;>;
```

Boolean values ordinarily are treated as checkboxes, in this plugin you may create it as a list. The annotations `string enabled_string` and `string disabled_string` control the text of the drop down list in this case. 

```c
uniform bool filter_red <bool is_list = true; string enabled_string = "Red Filter"; string disabled_string = "Off";>;
```

In addition to the suppport of the vector types, you may also designate where textures get their textures from.

For a selection from sources:
```c
uniform texture2d source <bool is_source = true;>;
```

For a selection from video files:
```c
uniform texture2d source <bool is_media = true;>;
```

Otherwise by default textures are assumed to be still images.
```c
uniform texture2d source;
```

# Global Annotations
All properties in OBS at the moment feature a label which is seperate from the property name itself. To set the bit of text for this label `string name` is used across the board.

# Example Shader
This dead simple shader lets you mirror another source...in any source.
```c
uniform float4x4 ViewProj;
uniform texture2d image;

uniform float2 elapsed_time;
uniform float2 uv_offset;
uniform float2 uv_scale;
uniform float2 uv_pixel_interval;

sampler_state textureSampler {
	Filter    = Linear;
	AddressU  = Border;
	AddressV  = Border;
	BorderColor = 00000000;
};

struct VertData {
	float4 pos : POSITION;
	float2 uv  : TEXCOORD0;
};

VertData mainTransform(VertData v_in)
{
	VertData vert_out;
	vert_out.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
	vert_out.uv = v_in.uv * uv_scale + uv_offset;
	return vert_out;
}

uniform texture2d source <bool is_source = true;>;

float4 mainImage(VertData v_in) : TARGET
{    
    float4 color = source.Sample(textureSampler, v_in.uv);
	
    return color;
}

technique Draw
{
	pass p0
	{
		vertex_shader = mainTransform(v_in);
		pixel_shader = mainImage(v_in);
	}
}
```