# Shader Plugins <[shader-plugins](https://github.com/Andersama/obs-studio/tree/shader-filter-cpp)>
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

# Generic Annotations
The string type annotation is used to specify and control different variations of gui elements
```c
string type = "Value";
```
Currently several values are supported, for `[int][float][bool]` and vectors of these types you can use:
```
string type = "combobox";
string type = "list";
string type = "num";
string type = "slider';
string type = "color";
```
If no value is specified or the annotation is not found the default "num" will be used for a a spinbox control.

For `[texture2d]` textures you can use:
```c
string type = "source";
string type = "audio";
string type = "image";
string type = "media';
string type = "buffer";
```
If no value is specified or the annotation is not found the default "image" will be used for an image selector.

# Numerical Annotations
There are three main variations of numerical controls: a spinbox, a drop-down list (or combobox),
and a slider (also includes a spinbox).
## sliders & spinboxes
For these several parameters are used to specify how the gui functions:
```c
DataType min = Value;
DataType max = Value;
DataType step = Value;
DataType default = Value;
```
For vector types each of these values may be specified per component by appending `'_'` and their index, eg:
```c
DataType min_0 = Value;
DataType max_1 = Value;
DataType step_2 = Value;
DataType default_3 = Value;
```
By default the minimum and maximum values are the upper and lower ilmits of those types
>warning: for sliders these values are too ridiculously far apart for the slider to function, specify a reasonable limit for your control.

The default value for all types is 0.

## lists
Drop down lists are made by creating any number of annotation pairs following this syntax:
```c
Datatype list_item_? = Value
Datatype list_item_?_name = Value
```
Where the list_item determines the value of the that particular drop down item and _name determines the forward facing text.
If no name is found the value of the parameter is used by default as the text.

The default annotation is used to specify the default option selected (by value), if no default is specified the first of the list
is selected by default.

# Texture Annotations
There are many textures in use by OBS at any given time, this plugin gives access to a few different types for your plugin to work with.
see [#Generic Annotations](/#generic-annotations). Here are a few special variations.

The audio type creates a texture representing the waveform of the audio source selected along the x axis and per channel along the y axis.
If the is_fft annotation is set to true, a fast fourier tranform (FFT) is performed on the data. This can be furthur trasformed to give the power spectra (the strength of the frequencies measured in dB).
If performing a FFT a window function may be specified via the string window annotation, currently supported are:
```c
string window = "bartlett";
string window = "blackmann";
string window = "blackmann_exact";
string window = "blackmann_harris";
string window = "blackmann_nuttall";
string window = "flat_top";
string window = "hann";
string window = "nuttall";
string window = "sine";
string window = "triangular";
string window = "welch";
```
> See https://en.wikipedia.org/wiki/Window_function

by default no (or rectangular) window is specified.

The buffer type copies a texture from a particular pass which is specified by the string technique annotation and int pass annotation.
The default pass of -1 indicates to copy the texture from the last of a given technique.

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

## Advanced
The ability to extract annotations also gives very intresting prospects. Here to give full flexibility of 
the creative juices I've taken a handy library [tinyexpr](https://github.com/codeplea/tinyexpr) to give you the ability
to write mathmatical expressions in order to evaluate parameters.

Yes...that's right, if you can express the value that you want in a mathmatical formula. You can.
Note: these values are not added to the gui, as they don't need to be shown.

## tinyexpr (& crop / expansion)
Similar in syntax to min, max, step and default for numerical types expressions can be specfied per component of a vector.
```c
string expr = "math * expression / here";
```
If specified an expression will hide that particular parameter / component from the gui, and will be evaulated one per frame.
These mathmatical expressions may use existing values which are snake-cased, along with several mathmatical functions.

```c
uniform int someInt; //camel cased
uniform int somesecondint; //not camel cased
uniform int someOtherInt<string expr = "some_int * somesecondint * 5";>;
```
In addition several common mathmatical functions are available for use:
```c
abs(input)
acos(input)
asin(input)
atan(input)
atan2(y, x)
ceil(input)
clamp(input, min, max)
cos(radians)
cosh(input)
degrees(radians)
exp(input)
fac(input)
floor(input)
hz_from_mel(mel)
ln(input)
log10(input)
mel_from_hz(hz)
ncr(objects, sample)
npr(objects, sample)
pow(base, power)
radians(degrees)
random(min, max)
screen_height(screen_index)
screen_width(screen_index)
sin(radians)
sqrt(input)
tan(radians)
tanh(input)
```
And some additional constants:
```c
channels //OBS's output channel count
e //The best growth constant
float_max //The maximum value a float can store
float_min //The minimum value a float can store
int_max //The minimum value an int can store
int_min //The maximum value an int can store
key //Returns the value of the last key pressed
key_pressed //Returns "true" if the key was pressed
mix //The % progress of the transition (for audio mixing)
mouse_button //The last mouse button pressed
mouse_click_x //The x position of the last mouse click relative to the current screen
mouse_click_y //The y position of the last mouse click relative to the current screen
mouse_click_screen //The index of the last screen the mouse was clicked
mouse_event_pos_x //The x position of the mouse relative to the interact screen
mouse_event_pos_y //The y position of the mouse relative to the interact screen
mouse_leave //Returns "true" 1.0 if the mouse is in the interact screen area
mouse_pos_x //The x position of the mouse relative to the current screen
mouse_pos_y //The y position of the mouse relative to the current screen
mouse_screen //The index of which screen the mouse is currently on (use in combination w/ screen_height / width)
mouse_up //Returns "true" if mouse button was pressed
mouse_visible //Returns "true" 1.0 if the mouse is visible on any screen
mouse_wheel_delta_x
mouse_wheel_delta_y
mouse_wheel_x
mouse_wheel_y
pi
sample_rate //OBS's output sample rate
```

### Cropping / Expansion
These annotations specify mathmatical expressions to evaluate cropping / expansion of the frame in their respective directions by pixel amounts.
Positive values = expansion, Negative = cropping.

### resize_expr_left, resize_expr_right, resize_expr_top, resize_expr_bottom
```c
<string resize_expr_left; string resize_expr_right; string resize_expr_top; string resize_expr_bottom;>
```

## Bound variables
Some variables are predefined for ease of use in designing your shader:
```c
uniform float4x4 ViewProj;
uniform texture2d image;
uniform texture2d image_0; //alternative to image
uniform texture2d image_1; //for transitions

uniform float elapsed_time; //the time since the start of the effect
uniform float2 uv_offset; //the offset of a source from the corner of the screen
uniform float2 uv_scale; //converts pixels into unit vector
uniform float2 uv_pixel_interval //the inverse of uv_scale, use to convert unit vector back into pixels;

uniform float transition_percentage; //for transitions
uniform float transition_time; //for transitions
```


## Advanced Shader
```c
uniform float4x4 ViewProj;
uniform texture2d image;

uniform float elapsed_time;
uniform float2 uv_offset;
uniform float2 uv_scale;
uniform float2 uv_pixel_interval;

sampler_state textureSampler {
	Filter    = Linear;
	AddressU  = Border;
	AddressV  = Border;
	BorderColor = 00000000;
};

sampler_state textureSampler_H {
	Filter    = Linear;
	AddressU  = Border;
	AddressV  = Border;
	BorderColor = 00000000;	
};

sampler_state textureSampler_V {
	Filter    = Linear;
	AddressU  = Wrap;
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

#define PI 3.141592653589793238462643383279502884197169399375105820974
#define PIO3 1.047197551196597746154214461093167628065723133125035273658
#define PI2O3 2.094395102393195492308428922186335256131446266250070547316

float melScale(float freq){
	return 2595 * log10 (1 + freq / 700.0);
}

float hertzFromMel(float mel) {
	return 700 * (pow(10, mel / 2595) - 1);
}

uniform texture2d audio <string type = "audio"; bool is_fft = true; int fft_samples = 1024; string window = "blackmann_harris";>;
uniform bool vertical;
uniform float px_shift <bool is_slider = true; float min = 0.5; float max = 1920; float step = 0.5;>;
uniform bool show_fft;
uniform float sample_rate <string expr = "sample_rate";>;
uniform float mel_total <string expr = "mel_from_hz(sample_rate / 2)";>;
uniform float variability <bool is_slider = true; float min = 0; float max = 10000;>;
uniform float rand_num <string expr = "random(0,1.0)"; bool update_expr_per_frame = true;>;

float4 mainImage(VertData v_in) : TARGET
{
	float2 px;
	float mel;
	float hz;
	float4 color;
	float px_2;
	float2 shift;
	float db;
	if(vertical){
		px = float2((1 - distance(v_in.uv.y, 0.5) * 2), v_in.uv.x);
		color = audio.Sample(textureSampler_V, px + float2(elapsed_time * 0.0001 * variability * rand_num, 0));
		if(show_fft)
			return color;
		db = clamp( ((log10( 1 / (2 * PI * 1024) * pow(color.r,2) )) + 12) / 12.0, 0, 2 );
		px_2 = (sin(color.r) * db * px_shift);
		shift = float2(px_2 * uv_pixel_interval.x, 0);
		return image.Sample(textureSampler_H, v_in.uv + shift);
	} else {
		px = float2((1 - distance(v_in.uv.x, 0.5) * 2), v_in.uv.y);
		color = audio.Sample(textureSampler_V, px + float2(elapsed_time * 0.0001 * variability * rand_num, 0));
		if(show_fft)
			return color;
		db = clamp( ((log10( 1 / (2 * PI * 1024) * pow(color.r,2) )) + 12) / 12.0, 0, 2 );
		px_2 = (sin(color.r) * db * px_shift);
		shift = float2(0, px_2 * uv_pixel_interval.y);
		return image.Sample(textureSampler_V, v_in.uv + shift);
	}
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
