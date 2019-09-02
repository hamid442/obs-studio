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

/* For full GPL v2 compatibility it is required to build libs with
 * our open source sdk instead of steinberg sdk , see our fork:
 * https://github.com/pkviet/portaudio , branch : openasio
 * If you build with original asio sdk, you are free to do so to the
 * extent that you do not distribute your binaries.
 */

#include "obs-vst3.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-vst3", "en-US")

#define blog(level, msg, ...) blog(level, "obs-vst3: " msg, ##__VA_ARGS__)

using VST3Host = PluginHost<VST3PluginFormat>;
using VSTHost  = PluginHost<VSTPluginFormat>;

static FileSearchPath search;
StringArray           paths;

static FileSearchPath search_2x;
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

FileSearchPath get_search_paths(VSTPluginFormat &f)
{
	UNUSED_PARAMETER(f);
	return search_2x;
}

FileSearchPath get_search_paths(VST3PluginFormat &f)
{
	UNUSED_PARAMETER(f);
	return search;
}

void set_paths(VSTPluginFormat &f, StringArray p)
{
	paths_2x = p;
}

void set_paths(VST3PluginFormat &f, StringArray p)
{
	paths = p;
}

void set_search_paths(VSTPluginFormat &f, FileSearchPath p)
{
	search_2x = p;
}

void set_search_paths(VST3PluginFormat &f, FileSearchPath p)
{
	search = p;
}

template<class _T> void register_plugin(const char *id)
{
	struct obs_source_info _filter = {0};
	_filter.id                     = id;
	_filter.type                   = OBS_SOURCE_TYPE_FILTER;
	_filter.output_flags           = OBS_SOURCE_AUDIO;
	_filter.get_name               = PluginHost<_T>::Name;
	_filter.create                 = PluginHost<_T>::Create;
	_filter.destroy                = PluginHost<_T>::Destroy;
	_filter.update                 = PluginHost<_T>::Update;
	_filter.filter_audio           = PluginHost<_T>::Filter_Audio;
	_filter.get_properties         = PluginHost<_T>::Properties;
	_filter.save                   = PluginHost<_T>::Save;

	obs_register_source(&_filter);

	static _T f;

	auto rescan = [](void * = nullptr) {
		static _T      _f;
		FileSearchPath s_path = get_search_paths(_f);
		if (_f.canScanForPlugins()) {
			StringArray p = _f.searchPathsForPlugins(search, true, true);
			set_paths(_f, p);
		}
	};

	std::string s = std::string("Rescan ") + f.getName().toStdString();
	obs_frontend_add_tools_menu_item(s.c_str(), rescan, nullptr);
	rescan();
}

bool obs_module_load(void)
{
	int version = (JUCE_MAJOR_VERSION << 16) | (JUCE_MINOR_VERSION << 8) | JUCE_BUILDNUMBER;
	blog(LOG_INFO, "JUCE Version: (%i) %i.%i.%i", version, JUCE_MAJOR_VERSION, JUCE_MINOR_VERSION,
			JUCE_BUILDNUMBER);

	MessageManager::getInstance();
#if WIN32
	register_plugin<VST3PluginFormat>("vst_filter_juce_3x");
	register_plugin<VSTPluginFormat>("vst_filter_juce_2x");
#endif
	return true;
}

void obs_module_unload()
{
}
