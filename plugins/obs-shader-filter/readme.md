# Shader Filter <[shader-filter-master](https://github.com/Andersama/obs-studio/tree/shader-filter-master)>
>Rapidly prototype and create graphical effects using OBS's shader syntax.

## Usage
>See [https://obsproject.com/docs/graphics.html](https://obsproject.com/docs/graphics.html) for the basics of OBS's shader syntax.
>
>This plugin makes use of annotations. Annotations are blocks of variables wrapped in `<>` which are used to describe the gui for the plugin. This plugin will read the file, extract the variables necessary for the shader to function and prepare a gui based on the ones given. Giving you the freedom to design the gui of your shader without needing to write any additional c/c++ code to update the shader's variables or the gui.

# Annotations
> This plugin makes use of a limited number of types made available in HLSL, namely `bool`, `int`, `float` and `string`. Annotation syntax as specified by Microsoft's HLSL is.
> ```c
> <DataType Name = Value; ... ;>
> ```
> For example to add a new integer parameter with a maximum value:
> ```c
> uniform int modes <int max = 5;>;
> ```
> Note where sensible annotations are cast to their appropriate typing constraints, for example this integer parameter could've used a float for it's maximum value, at the point of gui creation this value will be cast to an integer. Meaning for the most part `int`, `float` and `bool` annotations are exchangeable.

## Generic Annotations
> `[any type]`
> ### name
> ```c
> <string name;>
> ```
> This annotation determines the label text
> ### module_text
> ```c
> <bool module_text;>
> ```
> This annotation determines if the label text should be searched from OBS's ini files.

## Numerical Annotations
> `[int, int2, int3, int4, float, float2, float3, float4]`
> ### is_slider
> ```c
> <[bool] is_slider;>
> ```
> This boolean flag changes the gui from a numerical up/down combo box to a slider
> ### is_list
> ```c
> <[bool] is_list;>
> ```
> This boolean flag changes the gui into a drop down list (see [list syntax](#lists))
> ### min
> ```c
>  <[int,float,bool] min;>
> ```
> This annotation specifies the lower bound of a slider or combo box gui.
> ### max
> ```c
> <[int,float,bool] max;>
> ```
> This annotation specifies the upper bound of a slider or combo box gui.
> ### step
> ```c
> <[int,float,bool] step;>
> ```
> This annotation specifies the value that the combo box or slider will increment by.
> 
> ### Lists
> ```c
> <[int|float|bool] list_item = ?; string list_item_?_name = "">
> ```
> When `is_list == true` the gui will be changed to a drop down list. The values for the list are set following the example above. Any number of values can be specified, the `string list_item_?_name` determines the text that'll be shown to the user for that value. By default the text will assume the numerical value as the text to use. The values are loaded in from left to right into the drop down in top down order.

## Float4 Only
> `[float4]`
> ### is_float4
> ```c
> <bool is_float4;>
> ```
> The `[float4]` type is considered by default a four component rgba color ranging from 0-255 (tranformed into 0-1 range for the shader). `is_float4` set to true will treat float4 like all the other vectors.

## Texture Annotations
> `[texture2d]`
> ### is_source
> ```c
> <bool is_source;>
> ```
> This annotation will create a drop down list of active graphic sources that can be used as textures.
> ### is_audio_source
> ```c
> <bool is_audio_source;>
> ```
> This annotation will create a drop down list of active audio sources that can be used as textures.
> ### is_fft
> ```c
> <bool is_fft;>
> ```
> This annotation (in combination w/ an audio source) if set to true will perform an FFT on the audio data being recieved.

## Boolean Annotations
> `[bool]`
> ### is_list
> ```c
> <bool is_list;>
> ```
> This annotation if set to true changes the gui into a drop down list as opposed to a checkbox.
> 
> ### enabled_string
> ```c
> <string enabled_string;>
> ```
> This annotation specifies the text for the drop down list for its "true" value.
> ### enabled_module_text
> ```c
> <bool enabled_module_text;>
> ```
> This annotation determines whether the text should be searched from OBS's ini files.
> ### disabled_string
> ```c
> <string disabled_string;>
> ```
> This annotation specifies the text for the drop down list for it's "false" value.
> ### disabled_module_text
> ```c
> <bool disabled_module_text;>
> ```
> This annotation determines whether the text should be searched from OBS's ini files.

## Example Shader
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
    float4 color = source.Sample(textureSampler, float2(1-v_in.uv.x, v_in.uv.y));
	
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

## Acknowledgments
> https://github.com/nleseul/obs-shaderfilter most of the underlying code was already hashed out by this wonderful plugin, this branch/plugin takes this plugin a few steps furthur.
