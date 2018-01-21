std::vector<short> make_hash_map(long route_array[]){
	std::vector<short> out = std::vector<short>(256);
	long max_size = 0;
	for(short i = 0; i < MAX_AUDIO_CHANNELS; i++){
		long ch_index = route_array[i]+1;
		max_size = max(ch_index,max_size);
		out.resize(max_size);
		out[ch_index] = i;
	}
	return out;
}

long* make_route_map(std::vector<short> hash_map){
	short mapped = 0;
	long *arr = (long*)malloc(MAX_AUDIO_CHANNELS*sizeof(long));
	memset(arr, -1, MAX_AUDIO_CHANNELS*sizeof(long));
	
	for(size_t i = 0; i < hash_map.size(); i++){
		if(hash_map[i] >= 0 && hash_map[i] < MAX_AUDIO_CHANNELS){
			arr[hash_map[i]] = i;
		}
	}
	
	return arr;
}