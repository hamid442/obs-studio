#include "vulkan-subsystem.hpp"
#include <graphics/vec2.h>
#include <graphics/vec3.h>
#include <graphics/matrix3.h>
#include <graphics/matrix4.h>

void gs_shader_destroy(gs_shader_t *shader)
{
	delete shader;
}

int gs_shader_get_num_params(const gs_shader_t *shader)
{
	return 0;
}

gs_sparam_t *gs_shader_get_param_by_idx(gs_shader_t *shader, uint32_t param)
{
	return nullptr;
}

gs_sparam_t *gs_shader_get_param_by_name(gs_shader_t *shader, const char *name)
{
	/*
	for (size_t i = 0; i < shader->params.size(); i++) {
		gs_shader_param &param = shader->params[i];
		if (strcmp(param.name.c_str(), name) == 0)
			return &param;
	}
	*/
	return nullptr;
}

gs_sparam_t *gs_shader_get_viewproj_matrix(const gs_shader_t *shader)
{
	/*
	if (shader->type != GS_SHADER_VERTEX)
		return NULL;

	return static_cast<const gs_vertex_shader*>(shader)->viewProj;
	*/
	return nullptr;
}

gs_sparam_t *gs_shader_get_world_matrix(const gs_shader_t *shader)
{
	/*
	if (shader->type != GS_SHADER_VERTEX)
		return NULL;

	return static_cast<const gs_vertex_shader*>(shader)->world;
	*/
	return nullptr;
}

void gs_shader_get_param_info(const gs_sparam_t *param,
	struct gs_shader_param_info *info)
{
	if (!param)
		return;
	/*
	info->name = param->name.c_str();
	info->type = param->type;
	*/
}

static inline void shader_setval_inline(gs_shader_param *param,
	const void *data, size_t size)
{
	assert(param);
	if (!param)
		return;
	/*
	bool size_changed = param->curValue.size() != size;
	if (size_changed)
		param->curValue.resize(size);

	if (size_changed || memcmp(param->curValue.data(), data, size) != 0) {
		memcpy(param->curValue.data(), data, size);
		param->changed = true;
	}
	*/
}

void gs_shader_set_bool(gs_sparam_t *param, bool val)
{
	int b_val = (int)val;
	shader_setval_inline(param, &b_val, sizeof(int));
}

void gs_shader_set_float(gs_sparam_t *param, float val)
{
	shader_setval_inline(param, &val, sizeof(float));
}

void gs_shader_set_int(gs_sparam_t *param, int val)
{
	shader_setval_inline(param, &val, sizeof(int));
}

void gs_shader_set_matrix3(gs_sparam_t *param, const struct matrix3 *val)
{
	struct matrix4 mat;
	matrix4_from_matrix3(&mat, val);
	shader_setval_inline(param, &mat, sizeof(matrix4));
}

void gs_shader_set_matrix4(gs_sparam_t *param, const struct matrix4 *val)
{
	shader_setval_inline(param, val, sizeof(matrix4));
}

void gs_shader_set_vec2(gs_sparam_t *param, const struct vec2 *val)
{
	shader_setval_inline(param, val, sizeof(vec2));
}

void gs_shader_set_vec3(gs_sparam_t *param, const struct vec3 *val)
{
	shader_setval_inline(param, val, sizeof(float) * 3);
}

void gs_shader_set_vec4(gs_sparam_t *param, const struct vec4 *val)
{
	shader_setval_inline(param, val, sizeof(vec4));
}

void gs_shader_set_texture(gs_sparam_t *param, gs_texture_t *val)
{
	shader_setval_inline(param, &val, sizeof(gs_texture_t*));
}

void gs_shader_set_val(gs_sparam_t *param, const void *val, size_t size)
{
	shader_setval_inline(param, val, size);
}

void gs_shader_set_default(gs_sparam_t *param)
{
	/*
	if (param->defaultValue.size())
		shader_setval_inline(param, param->defaultValue.data(),
			param->defaultValue.size());
			*/
}

void gs_shader_set_next_sampler(gs_sparam_t *param, gs_samplerstate_t *sampler)
{
	//param->nextSampler = sampler;
}
