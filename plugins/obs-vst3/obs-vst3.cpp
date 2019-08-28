/*
Copyright (C) 2019 andersama <anderson.john.alexander@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "obs-vst3.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-vst3", "en-US")

#define blog(level, msg, ...) blog(level, "obs-vst3: " msg, ##__VA_ARGS__)

int get_max_obs_channels()
{
	static int channels = 0;
	if (channels > 0) {
		return channels;
	} else {
		for (int i = 0; i < 1024; i++) {
			int c = get_audio_channels((speaker_layout)i);
			if (c > channels)
				channels = c;
		}
		return channels;
	}
}

VSTPluginFormat  vst2format;
VST3PluginFormat vst3format;

const int obs_output_frames = AUDIO_OUTPUT_FRAMES;

static FileSearchPath search = vst3format.getDefaultLocationsToSearch();
StringArray           paths;

static FileSearchPath search_2x = vst2format.getDefaultLocationsToSearch();
StringArray           paths_2x;


StringArray get_paths(VSTPluginFormat &f)
{
	UNUSED_PARAMETER(f);
	return paths_2x;
}

StringArray get_paths(VST3PluginFormat &f)
{
	UNUSED_PARAMETER(f);
	return paths;
}

void set_paths(VSTPluginFormat &f, StringArray p)
{
	paths_2x = p;
}

void set_paths(VST3PluginFormat &f, StringArray p)
{
	paths = p;
}

const volatile int obs_max_channels = get_max_obs_channels();

static void free_type_data(void *vptr)
{
	vptr = 0;
}

bool obs_module_load(void)
{
	MessageManager::getInstance();

	struct obs_source_info vst3_filter = {0};
	vst3_filter.id                     = "vst_filter_juce_3x";
	vst3_filter.type                   = OBS_SOURCE_TYPE_FILTER;
	vst3_filter.output_flags           = OBS_SOURCE_AUDIO;
	vst3_filter.get_name               = PluginHost<VST3PluginFormat>::Name;
	vst3_filter.create                 = PluginHost<VST3PluginFormat>::Create;
	vst3_filter.destroy                = PluginHost<VST3PluginFormat>::Destroy;
	vst3_filter.update                 = PluginHost<VST3PluginFormat>::Update;
	vst3_filter.filter_audio           = PluginHost<VST3PluginFormat>::Filter_Audio;
	vst3_filter.get_properties         = PluginHost<VST3PluginFormat>::Properties;
	vst3_filter.save                   = PluginHost<VST3PluginFormat>::Save;
	vst3_filter.type_data              = (void *)true;
	vst3_filter.free_type_data         = free_type_data;

	struct obs_source_info vst_filter = {0};
	vst_filter.id                     = "vst_filter_juce_2x";
	vst_filter.type                   = OBS_SOURCE_TYPE_FILTER;
	vst_filter.output_flags           = OBS_SOURCE_AUDIO;
	vst_filter.get_name               = PluginHost<VSTPluginFormat>::Name;
	vst_filter.create                 = PluginHost<VSTPluginFormat>::Create;
	vst_filter.destroy                = PluginHost<VSTPluginFormat>::Destroy;
	vst_filter.update                 = PluginHost<VSTPluginFormat>::Update;
	vst_filter.filter_audio           = PluginHost<VSTPluginFormat>::Filter_Audio;
	vst_filter.get_properties         = PluginHost<VSTPluginFormat>::Properties;
	vst_filter.save                   = PluginHost<VSTPluginFormat>::Save;
	vst_filter.type_data              = (void *)true;
	vst_filter.free_type_data         = free_type_data;
	
	int version = (JUCE_MAJOR_VERSION << 16) | (JUCE_MINOR_VERSION << 8) | JUCE_BUILDNUMBER;
	blog(LOG_INFO, "JUCE Version: (%i) %i.%i.%i", version, JUCE_MAJOR_VERSION, JUCE_MINOR_VERSION,
			JUCE_BUILDNUMBER);

	char *iconPath = obs_module_file("obs-studio.ico");
	juce::String iconStr  = iconPath;
	juce::File   iconFile = juce::File(iconStr);
	if (iconStr.length() > 0)
		windowIcon = ImageFileFormat::loadFrom(iconFile);
	bfree(iconPath);
	
	blog(LOG_INFO, "%i", sizeof(vst3_filter));

	obs_register_source(&vst3_filter);
	obs_register_source(&vst_filter);
	//#define DEBUG_JUCE_VST 1
	if (vst3_filter.free_type_data) {
		auto rescan_vst3 = [](void * = nullptr) {
			if (vst3format.canScanForPlugins())
				paths = vst3format.searchPathsForPlugins(search, true, true);
		};
		obs_frontend_add_tools_menu_item("Rescan VST3", rescan_vst3, nullptr);
		rescan_vst3();
#if DEBUG_JUCE_VST
		for (int i = 0; i < paths.size(); i++) {
			juce::OwnedArray<juce::PluginDescription> descs;
			vst3format.findAllTypesForFile(descs, paths[i]);
			for (int j = 0; j < descs.size(); j++) {
				std::string n = descs[j]->name.toStdString();
				blog(LOG_INFO, "[%i][%i]: %s", i, j, n.c_str());
			}
		}
#endif
	}

	if (vst_filter.free_type_data) {
		auto rescan_vst2 = [](void * = nullptr) {
			if (vst2format.canScanForPlugins())
				paths_2x = vst2format.searchPathsForPlugins(search_2x, true, true);
		};
		obs_frontend_add_tools_menu_item("Rescan VST", rescan_vst2, nullptr);
		rescan_vst2();
#if DEBUG_JUCE_VST
		for (int i = 0; i < paths_2x.size(); i++) {
			juce::OwnedArray<juce::PluginDescription> descs;
			vst2format.findAllTypesForFile(descs, paths[i]);
			for (int j = 0; j < descs.size(); j++) {
				std::string n = descs[j]->name.toStdString();
				blog(LOG_INFO, "[%i][%i]: %s", i, j, n.c_str());
			}
		}
#endif
	}

	return true;
}

void obs_module_unload()
{
}
