/*
 * This file is part of vitaGL
 * Copyright 2017, 2018, 2019, 2020 Rinnegatamante
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* 
 * buffers.c:
 * Implementation for buffers related functions
 */

#include "shared.h"

#define DISABLED_ATTRIBS_POOL_SIZE (256 * 1024) // Disabled attributes circular pool size in bytes for the default VAO
#define DISABLED_AUX_ATTRIBS_POOL_SIZE (64 * 1024) // Disabled attributes circular pool size in bytes for non default VAOs

uint32_t main_vertex_attrib_pool_size = DISABLED_ATTRIBS_POOL_SIZE;
uint32_t aux_vertex_attrib_pool_size = DISABLED_AUX_ATTRIBS_POOL_SIZE;
uint32_t vertex_array_unit = 0; // Current in-use vertex array buffer unit
uint32_t uniform_array_unit = 0; // Current in-use uniform buffer unit

void *vertex_object; // Vertex object address for vgl* draw pipeline
void *color_object; // Color object address for vgl* draw pipeline
void *texture_object; // Texture object address for vgl* draw pipeline
void *index_object; // Index object address for vgl* draw pipeline

static vao default_vao; // Vertex Array Object used when no vao is bound
vao *cur_vao = &default_vao; // Current in-use vertex array object

query *active_query = NULL; // Active query object
static query queries[MAX_QUERIES_NUM]; // Available query objects pool
uint32_t *queries_buffer = NULL; // Buffer used for visibility testing
SceGxmNotification query_fence; // Fence used for occlusion queries sync

void reset_vao(vao *v) {
	vgl_memset(v->vertex_attrib_offsets, 0, sizeof(uint32_t) * VERTEX_ATTRIBS_NUM);
	vgl_memset(v->vertex_attrib_vbo, 0, sizeof(uint32_t) * VERTEX_ATTRIBS_NUM);
	for (int i = 0; i < VERTEX_ATTRIBS_NUM; i++) {
		v->vertex_attrib_size[i] = 4;
		v->vertex_attrib_config[i].regIndex = i;
	}
	v->vertex_attrib_state = 0;
	v->vertex_attrib_divisor = 0;
	v->index_array_unit = 0;

	uint32_t circular_pool_size = v == &default_vao ? main_vertex_attrib_pool_size : aux_vertex_attrib_pool_size;
	v->vertex_attrib_pool = (float *)gpu_alloc_mapped_for_cpu(circular_pool_size);
	v->vertex_attrib_pool_ptr = v->vertex_attrib_pool;
	v->vertex_attrib_pool_limit = (float *)((uint8_t *)v->vertex_attrib_pool + circular_pool_size);

	// Init generic vertex attrib arrays
	vao *vao_bkp = cur_vao;
	cur_vao = v;
	for (int i = 0; i < VERTEX_ATTRIBS_NUM; i++) {
		v->vertex_attrib_value[i] = reserve_attrib_pool(4);
		v->vertex_attrib_config[i].componentCount = 4;
		v->vertex_attrib_config[i].offset = 0;
		v->vertex_attrib_config[i].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
		v->vertex_attrib_config[i].regIndex = i;
		v->vertex_attrib_config[i].streamIndex = i;
		v->vertex_stream_config[i].stride = 0;
		v->vertex_stream_config[i].indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
	}
	cur_vao = vao_bkp;
}

void reset_queries() {
	queries_buffer = gpu_alloc_mapped_for_cpu(MAX_QUERIES_NUM * 4 * sizeof(uint32_t));
	sceGxmSetVisibilityBuffer(gxm_context, queries_buffer, MAX_QUERIES_NUM * sizeof(uint32_t));
	query_fence.value = 0;
	query_fence.address = sceGxmGetNotificationRegion();
	*query_fence.address = 0;
	
	for (GLuint i = 0; i < MAX_QUERIES_NUM; i++) {
		queries[i].id = 0xFF;
		queries[i].sync = 0;
	}
}

/*
 * ------------------------------
 * - IMPLEMENTATION STARTS HERE -
 * ------------------------------
 */
 
void glGenQueries(GLsizei n, GLuint *ids) {
	THREAD_SAFE()

#ifndef SKIP_ERROR_HANDLING
	if (n < 0) {
		SET_GL_ERROR(GL_INVALID_VALUE)
	}
#endif
	for (GLuint i = 0; i < MAX_QUERIES_NUM; i++) {
		if (queries[i].id == 0xFF) {
			ids[n - 1] = (GLuint)&queries[i];
			queries[i].id = i;
			n--;
			if (n == 0) {
				break;
			}
		}
	}
}

void glDeleteQueries(GLsizei n, const GLuint *ids) {
	THREAD_SAFE()

#ifndef SKIP_ERROR_HANDLING
	if (n < 0) {
		SET_GL_ERROR(GL_INVALID_VALUE)
	}
#endif
	for (GLuint i = 0; i < n; i++) {
		queries[ids[i]].id = 0xFF;
	}
}

void glBeginQuery(GLenum target, GLuint id) {
	THREAD_SAFE()

	switch (target) {
	case GL_SAMPLES_PASSED:
		sceGxmSetFrontVisibilityTestOp(gxm_context, SCE_GXM_VISIBILITY_TEST_OP_INCREMENT);
		sceGxmSetBackVisibilityTestOp(gxm_context, SCE_GXM_VISIBILITY_TEST_OP_INCREMENT);
		break;
	case GL_ANY_SAMPLES_PASSED:
	case GL_ANY_SAMPLES_PASSED_CONSERVATIVE:
		sceGxmSetFrontVisibilityTestOp(gxm_context, SCE_GXM_VISIBILITY_TEST_OP_SET);
		sceGxmSetBackVisibilityTestOp(gxm_context, SCE_GXM_VISIBILITY_TEST_OP_SET);
		break;
	default:
		SET_GL_ERROR_WITH_VALUE(GL_INVALID_ENUM, target)
	}
	active_query = (query *)id;
	queries_buffer[active_query->id] = 0;
	queries_buffer[active_query->id + MAX_QUERIES_NUM] = 0;
	queries_buffer[active_query->id + MAX_QUERIES_NUM * 2] = 0;
	queries_buffer[active_query->id + MAX_QUERIES_NUM * 3] = 0;
	active_query->mode = target;
	sceGxmSetFrontVisibilityTestIndex(gxm_context, active_query->id);
	sceGxmSetBackVisibilityTestIndex(gxm_context, active_query->id);
	sceGxmSetFrontVisibilityTestEnable(gxm_context, SCE_GXM_VISIBILITY_TEST_ENABLED);
	sceGxmSetBackVisibilityTestEnable(gxm_context, SCE_GXM_VISIBILITY_TEST_ENABLED);
}

void glEndQuery(GLenum target) {
	THREAD_SAFE()

	sceGxmSetFrontVisibilityTestEnable(gxm_context, SCE_GXM_VISIBILITY_TEST_DISABLED);
	sceGxmSetBackVisibilityTestEnable(gxm_context, SCE_GXM_VISIBILITY_TEST_DISABLED);
	active_query->sync = query_fence.value + 1;
	active_query = NULL;
}

void glGetQueryObjectiv(GLuint id, GLenum pname, GLint *params) {
	THREAD_SAFE()

	query *q = (query *)id;

	switch (pname) {
	case GL_QUERY_RESULT:
		if (q->sync > *query_fence.address) {
			dirty_query = GL_TRUE;
			scene_reset();
			sceGxmNotificationWait(&query_fence);
		}
		*params = queries_buffer[q->id] + queries_buffer[q->id + MAX_QUERIES_NUM] + queries_buffer[q->id + MAX_QUERIES_NUM * 2] + queries_buffer[q->id + MAX_QUERIES_NUM * 3];
		if (q->mode != GL_SAMPLES_PASSED) {
			*params = *params > 0 ? GL_TRUE : GL_FALSE;
		}
		break;
	case GL_QUERY_RESULT_NO_WAIT:
		if (q->sync <= *query_fence.address) {
			*params = queries_buffer[q->id] + queries_buffer[q->id + MAX_QUERIES_NUM] + queries_buffer[q->id + MAX_QUERIES_NUM * 2] + queries_buffer[q->id + MAX_QUERIES_NUM * 3];
			if (q->mode != GL_SAMPLES_PASSED) {
				*params = *params > 0 ? GL_TRUE : GL_FALSE;
			}
		}
		break;
	case GL_QUERY_RESULT_AVAILABLE:
		*params = (q->sync <= *query_fence.address) ? GL_TRUE : GL_FALSE;
		break;
	case GL_QUERY_TARGET:
		*params = q->mode;
		break;
	default:
		SET_GL_ERROR_WITH_VALUE(GL_INVALID_ENUM, pname)
	}
}

void glGetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params) {
	THREAD_SAFE()

	query *q = (query *)id;

	switch (pname) {
	case GL_QUERY_RESULT:
		if (q->sync > *query_fence.address) {
			dirty_query = GL_TRUE;
			scene_reset();
			sceGxmNotificationWait(&query_fence);
		}
		*params = queries_buffer[q->id] + queries_buffer[q->id + MAX_QUERIES_NUM] + queries_buffer[q->id + MAX_QUERIES_NUM * 2] + queries_buffer[q->id + MAX_QUERIES_NUM * 3];
		if (q->mode != GL_SAMPLES_PASSED) {
			*params = *params > 0 ? GL_TRUE : GL_FALSE;
		}
		break;
	case GL_QUERY_RESULT_NO_WAIT:
		if (q->sync <= *query_fence.address) {
			*params = queries_buffer[q->id] + queries_buffer[q->id + MAX_QUERIES_NUM] + queries_buffer[q->id + MAX_QUERIES_NUM * 2] + queries_buffer[q->id + MAX_QUERIES_NUM * 3];
			if (q->mode != GL_SAMPLES_PASSED) {
				*params = *params > 0 ? GL_TRUE : GL_FALSE;
			}
		}
		break;
	case GL_QUERY_RESULT_AVAILABLE:
		*params = (q->sync <= *query_fence.address) ? GL_TRUE : GL_FALSE;
		break;
	case GL_QUERY_TARGET:
		*params = q->mode;
		break;
	default:
		SET_GL_ERROR_WITH_VALUE(GL_INVALID_ENUM, pname)
	}
}

void glGenVertexArrays(GLsizei n, GLuint *res) {
	THREAD_SAFE()

#ifndef SKIP_ERROR_HANDLING
	if (n < 0) {
		SET_GL_ERROR(GL_INVALID_VALUE)
	}
#endif
	for (int i = 0; i < n; i++) {
		res[i] = (GLuint)(vglMalloc(sizeof(vao)));
#ifdef LOG_ERRORS
		if (!res[i]) {
			vgl_log("%s:%d glGenVertexArrays failed to alloc a buffer (%d/%lu).\n", __FILE__, __LINE__, i, n);
		}
#endif
		reset_vao((vao *)res[i]);
	}
}

void glBindVertexArray(GLuint array) {
	THREAD_SAFE()

	cur_vao = (vao *)array;
	if (!cur_vao)
		cur_vao = &default_vao;
}

void glDeleteVertexArrays(GLsizei n, const GLuint *gl_arrays) {
	THREAD_SAFE()

#ifndef SKIP_ERROR_HANDLING
	if (n < 0) {
		SET_GL_ERROR(GL_INVALID_VALUE)
	}
#endif
	for (int j = 0; j < n; j++) {
		if (gl_arrays[j]) {
			vao *gpu_buf = (vao *)gl_arrays[j];
			mark_as_dirty(gpu_buf->vertex_attrib_pool);
			vgl_free(gpu_buf);
		}
	}
}

inline __attribute__((always_inline)) void glGenBuffers(GLsizei n, GLuint *res) {
	THREAD_SAFE()

#ifndef SKIP_ERROR_HANDLING
	if (n < 0) {
		SET_GL_ERROR(GL_INVALID_VALUE)
	}
#endif
	for (int i = 0; i < n; i++) {
		vbo *gpu_buf = (vbo *)vglMalloc(sizeof(vbo));
#ifdef LOG_ERRORS
		if (!gpu_buf)
			vgl_log("%s:%d glGenBuffers failed to alloc a buffer (%d/%lu).\n", __FILE__, __LINE__, i, n);
#endif
		vgl_memset(gpu_buf, 0, sizeof(vbo));
		gpu_buf->last_frame = OBJ_NOT_USED;
		for (int s = 0; s < VBO_RING_SIZE; s++)
			gpu_buf->ring_frame[s] = OBJ_NOT_USED;
		res[i] = (GLuint)gpu_buf;
	}
}

void glCreateBuffers(GLsizei n, GLuint *buffers) {
	glGenBuffers(n, buffers);
}

void glBindBuffer(GLenum target, GLuint buffer) {
	THREAD_SAFE()

#ifdef HAVE_DLISTS
	// Enqueueing function to a display list if one is being compiled
	if (_vgl_enqueue_list_func(glBindBuffer, DLIST_FUNC_U32_U32, target, buffer))
		return;
#endif
	switch (target) {
	case GL_ARRAY_BUFFER:
		vertex_array_unit = buffer;
		break;
	case GL_ELEMENT_ARRAY_BUFFER:
		cur_vao->index_array_unit = buffer;
		break;
	case GL_UNIFORM_BUFFER:
		uniform_array_unit = buffer;
		break;
	default:
		SET_GL_ERROR_WITH_VALUE(GL_INVALID_ENUM, target)
	}
}

void glDeleteBuffers(GLsizei n, const GLuint *gl_buffers) {
	THREAD_SAFE()

#ifndef SKIP_ERROR_HANDLING
	if (n < 0) {
		SET_GL_ERROR(GL_INVALID_VALUE)
	}
#endif
	for (int j = 0; j < n; j++) {
		if (gl_buffers[j]) {
			vbo *gpu_buf = (vbo *)gl_buffers[j];
			uint8_t cur = gpu_buf->ring_idx;
			gpu_buf->ring_frame[cur] = gpu_buf->last_frame;
			for (int s = 0; s < VBO_RING_SIZE; s++) {
				if (gpu_buf->ring_ptr[s]) {
					if (gpu_buf->ring_frame[s] != OBJ_NOT_USED && (vgl_framecount - gpu_buf->ring_frame[s] <= FRAME_PURGE_FREQ)) {
						mark_as_dirty(gpu_buf->ring_ptr[s]);
					} else {
						vgl_free(gpu_buf->ring_ptr[s]);
					}
				}
			}
			vgl_free(gpu_buf);
		}
	}
}

// Contadores de perfilado para diagnóstico de cuellos de botella en VBOs
uint32_t vgl_prof_vbo_reuse = 0;
uint32_t vgl_prof_vbo_alloc = 0;
uint32_t vgl_prof_vbo_dirty = 0;

inline void glNamedBufferData(GLuint buffer, GLsizei size, const void *data, GLenum usage) {
	THREAD_SAFE()

	vbo *gpu_buf = (vbo *)buffer;
	
#ifndef SKIP_ERROR_HANDLING
	if (size < 0) {
		SET_GL_ERROR(GL_INVALID_VALUE)
	} else if (!gpu_buf) {
		SET_GL_ERROR(GL_INVALID_OPERATION)
	} else if (size == 0) {
		vgl_log("%s:%d glNamedBufferData called with size 0. Skipping...\n", __FILE__, __LINE__);
		return;
	}
#endif
	switch (usage) {
	case GL_STREAM_DRAW:
	case GL_STREAM_COPY:
	case GL_STREAM_READ:
	case GL_DYNAMIC_DRAW:
	case GL_DYNAMIC_READ:
	case GL_DYNAMIC_COPY:
		gpu_buf->alloc_func = gpu_alloc_mapped_for_cpu;
		break;
	default:
		gpu_buf->alloc_func = gpu_buf->ring_ptr[0] ? gpu_alloc_mapped_for_cpu : gpu_alloc_mapped_for_gpu;
		break;
	}

	uint8_t s = gpu_buf->ring_idx;
	gpu_buf->ring_frame[s] = gpu_buf->last_frame;

	// Si el slot actual fue usado recientemente por la GPU, rotamos al siguiente slot del anillo
	// (solo para buffers <= 128 KB para no multiplicar RAM en mallas grandes).
	if (gpu_buf->last_frame != OBJ_NOT_USED && (vgl_framecount - gpu_buf->last_frame <= FRAME_PURGE_FREQ)) {
		if (size <= 131072) {
			s = (s + 1) % VBO_RING_SIZE;
			gpu_buf->ring_idx = s;
		}
	}

	// Si el slot elegido todavía está en vuelo en la GPU o es más chico que size, no se puede pisar:
	// lo liberamos (mark_as_dirty si está en vuelo) y asignamos uno nuevo.
	if (gpu_buf->ring_ptr[s]) {
		GLboolean in_flight = gpu_buf->ring_frame[s] != OBJ_NOT_USED && (vgl_framecount - gpu_buf->ring_frame[s] <= FRAME_PURGE_FREQ);
		if (in_flight || gpu_buf->ring_cap[s] < size) {
			if (in_flight) {
				mark_as_dirty(gpu_buf->ring_ptr[s]);
				vgl_prof_vbo_dirty++;
			} else {
				vgl_free(gpu_buf->ring_ptr[s]);
			}
			gpu_buf->ring_ptr[s] = NULL;
			gpu_buf->ring_cap[s] = 0;
		} else {
			vgl_prof_vbo_reuse++;
		}
	}

	if (!gpu_buf->ring_ptr[s]) {
		gpu_buf->ring_ptr[s] = gpu_buf->alloc_func(size);
		gpu_buf->ring_cap[s] = gpu_buf->ring_ptr[s] ? size : 0;
		vgl_prof_vbo_alloc++;
	}

	gpu_buf->ptr = gpu_buf->ring_ptr[s];
	gpu_buf->ring_frame[s] = OBJ_NOT_USED;

	if (!gpu_buf->ptr) {
#ifndef SKIP_ERROR_HANDLING
		SET_GL_ERROR(GL_OUT_OF_MEMORY)
#else
		gpu_buf->size = 0;
		gpu_buf->last_frame = OBJ_NOT_USED;
		return;
#endif
	}

	gpu_buf->size = size;
	gpu_buf->last_frame = OBJ_NOT_USED;

	if (data)
		vgl_fast_memcpy(gpu_buf->ptr, data, size);
}

void glBufferData(GLenum target, GLsizei size, const GLvoid *data, GLenum usage) {
	THREAD_SAFE()

	vbo *gpu_buf;
	switch (target) {
	case GL_ARRAY_BUFFER:
		gpu_buf = (vbo *)vertex_array_unit;
		break;
	case GL_ELEMENT_ARRAY_BUFFER:
		gpu_buf = (vbo *)cur_vao->index_array_unit;
		break;
	case GL_UNIFORM_BUFFER:
		gpu_buf = (vbo *)uniform_array_unit;
		break;
	default:
		SET_GL_ERROR_WITH_VALUE(GL_INVALID_ENUM, target)
	}
	
	glNamedBufferData((GLuint)gpu_buf, size, data, usage);
}

inline void glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void *data) {
	THREAD_SAFE()

	vbo *gpu_buf = (vbo *)buffer;

#ifndef SKIP_ERROR_HANDLING
	if (!gpu_buf) {
		SET_GL_ERROR(GL_INVALID_OPERATION)
	} else if (offset < 0 || offset + size > gpu_buf->size) {
		SET_GL_ERROR(GL_INVALID_VALUE)
	} else if (size == 0) {
		vgl_log("%s:%d glNamedBufferSubData called with size 0. Skipping...\n", __FILE__, __LINE__);
		return;
	}
#endif

#ifndef BUFFERS_SPEEDHACK
	// Allocating or rotating to a free ring slot if the buffer was used recently
	if (gpu_buf->last_frame != OBJ_NOT_USED && (vgl_framecount - gpu_buf->last_frame <= FRAME_PURGE_FREQ)) {
		uint8_t prev_s = gpu_buf->ring_idx;
		gpu_buf->ring_frame[prev_s] = gpu_buf->last_frame;
		uint8_t s = (prev_s + 1) % VBO_RING_SIZE;
		gpu_buf->ring_idx = s;
		if (gpu_buf->ring_ptr[s]) {
			GLboolean in_flight = gpu_buf->ring_frame[s] != OBJ_NOT_USED && (vgl_framecount - gpu_buf->ring_frame[s] <= FRAME_PURGE_FREQ);
			if (in_flight || gpu_buf->ring_cap[s] < gpu_buf->size) {
				if (in_flight)
					mark_as_dirty(gpu_buf->ring_ptr[s]);
				else
					vgl_free(gpu_buf->ring_ptr[s]);
				gpu_buf->ring_ptr[s] = NULL;
				gpu_buf->ring_cap[s] = 0;
			}
		}
		if (!gpu_buf->ring_ptr[s]) {
			gpu_buf->ring_ptr[s] = gpu_alloc_mapped_for_cpu(gpu_buf->size);
			gpu_buf->ring_cap[s] = gpu_buf->ring_ptr[s] ? gpu_buf->size : 0;
		}
		uint8_t *ptr = (uint8_t *)gpu_buf->ring_ptr[s];

		if (!ptr) {
#ifdef LOG_ERRORS
			vgl_log("%s:%d glBufferSubData failed to alloc a buffer of %ld bytes. Buffer content won't be updated.\n", __FILE__, __LINE__, gpu_buf->size);
#endif
			return;
		}

		// Copying up previous data combined to modified data
		if (offset > 0)
			vgl_memcpy(ptr, gpu_buf->ptr, offset);
		vgl_memcpy(ptr + offset, data, size);
		if (gpu_buf->size - size - offset > 0)
			vgl_memcpy(ptr + offset + size, (uint8_t *)gpu_buf->ptr + offset + size, gpu_buf->size - size - offset);

		gpu_buf->ptr = ptr;
		gpu_buf->ring_frame[s] = OBJ_NOT_USED;
		gpu_buf->last_frame = OBJ_NOT_USED;
	} else
#endif
	{
		vgl_memcpy((uint8_t *)gpu_buf->ptr + offset, data, size);
	}
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) {
	THREAD_SAFE()

	vbo *gpu_buf;
	switch (target) {
	case GL_ARRAY_BUFFER:
		gpu_buf = (vbo *)vertex_array_unit;
		break;
	case GL_ELEMENT_ARRAY_BUFFER:
		gpu_buf = (vbo *)cur_vao->index_array_unit;
		break;
	case GL_UNIFORM_BUFFER:
		gpu_buf = (vbo *)uniform_array_unit;
		break;
	default:
		SET_GL_ERROR_WITH_VALUE(GL_INVALID_ENUM, target)
	}
	
	glNamedBufferSubData((GLuint)gpu_buf, offset, size, data);
}

inline void *glMapNamedBuffer(GLuint buffer, GLenum access) {
	vbo *gpu_buf = (vbo *)buffer;
	
#ifndef SKIP_ERROR_HANDLING
	switch (access) {
	case GL_READ_WRITE:
	case GL_READ_ONLY:
	case GL_WRITE_ONLY:
		break;
	default:
		SET_GL_ERROR_WITH_RET(GL_INVALID_ENUM, NULL);
	}

	if (!gpu_buf) {
		SET_GL_ERROR_WITH_RET(GL_INVALID_OPERATION, NULL)
	}
#endif

	// FIXME: Current implementation doesn't take into account 'last_frame' state
	gpu_buf->mapped = GL_TRUE;
	return gpu_buf->ptr;
}

void *glMapBuffer(GLenum target, GLenum access) {
	vbo *gpu_buf;
	switch (target) {
	case GL_ARRAY_BUFFER:
		gpu_buf = (vbo *)vertex_array_unit;
		break;
	case GL_ELEMENT_ARRAY_BUFFER:
		gpu_buf = (vbo *)cur_vao->index_array_unit;
		break;
	case GL_UNIFORM_BUFFER:
		gpu_buf = (vbo *)uniform_array_unit;
		break;
	default:
		SET_GL_ERROR_WITH_RET(GL_INVALID_ENUM, NULL)
	}

	return glMapNamedBuffer((GLuint)gpu_buf, access);
}

inline void *glMapNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access) {
	vbo *gpu_buf = (vbo *)buffer;

#ifndef SKIP_ERROR_HANDLING
	if (!gpu_buf) {
		SET_GL_ERROR_WITH_RET(GL_INVALID_OPERATION, NULL)
	} else if (offset < 0 || offset + length > gpu_buf->size) {
		SET_GL_ERROR_WITH_RET(GL_INVALID_VALUE, NULL)
	}
#endif

	// FIXME: Current implementation doesn't take into account 'last_frame' state
	gpu_buf->mapped = GL_TRUE;
	return (void *)((uint8_t *)gpu_buf->ptr + offset);
}

void *glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
	vbo *gpu_buf;
	switch (target) {
	case GL_ARRAY_BUFFER:
		gpu_buf = (vbo *)vertex_array_unit;
		break;
	case GL_ELEMENT_ARRAY_BUFFER:
		gpu_buf = (vbo *)cur_vao->index_array_unit;
		break;
	case GL_UNIFORM_BUFFER:
		gpu_buf = (vbo *)uniform_array_unit;
		break;
	default:
		SET_GL_ERROR_WITH_RET(GL_INVALID_ENUM, NULL)
	}

	return glMapNamedBufferRange((GLuint)gpu_buf, offset, length, access);
}

inline GLboolean glUnmapNamedBuffer(GLuint buffer) {
	vbo *gpu_buf = (vbo *)buffer;

#ifndef SKIP_ERROR_HANDLING
	if (!gpu_buf || !gpu_buf->mapped) {
		SET_GL_ERROR_WITH_RET(GL_INVALID_OPERATION, GL_FALSE)
	}
#endif

	gpu_buf->last_frame = OBJ_NOT_USED;
	gpu_buf->mapped = GL_FALSE;
	return GL_TRUE;
}

GLboolean glUnmapBuffer(GLenum target) {
	vbo *gpu_buf;
	switch (target) {
	case GL_ARRAY_BUFFER:
		gpu_buf = (vbo *)vertex_array_unit;
		break;
	case GL_ELEMENT_ARRAY_BUFFER:
		gpu_buf = (vbo *)cur_vao->index_array_unit;
		break;
	case GL_UNIFORM_BUFFER:
		gpu_buf = (vbo *)uniform_array_unit;
		break;
	default:
		SET_GL_ERROR_WITH_RET(GL_INVALID_ENUM, GL_FALSE)
	}

	return glUnmapNamedBuffer((GLuint)gpu_buf);
}

void glFlushMappedNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length) {
	THREAD_SAFE()

#ifndef SKIP_ERROR_HANDLING
	vbo *gpu_buf = (vbo *)buffer;

	if (!gpu_buf || !gpu_buf->mapped) {
		SET_GL_ERROR(GL_INVALID_OPERATION)
	} else if (offset < 0 || offset + length > gpu_buf->size) {
		SET_GL_ERROR(GL_INVALID_VALUE)
	}
#endif
}

void glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length) {
	THREAD_SAFE()

#ifndef SKIP_ERROR_HANDLING
	vbo *gpu_buf;
	switch (target) {
	case GL_ARRAY_BUFFER:
		gpu_buf = (vbo *)vertex_array_unit;
		break;
	case GL_ELEMENT_ARRAY_BUFFER:
		gpu_buf = (vbo *)cur_vao->index_array_unit;
		break;
	case GL_UNIFORM_BUFFER:
		gpu_buf = (vbo *)uniform_array_unit;
		break;
	default:
		SET_GL_ERROR_WITH_VALUE(GL_INVALID_ENUM, target)
	}

	if (!gpu_buf || !gpu_buf->mapped) {
		SET_GL_ERROR(GL_INVALID_OPERATION)
	} else if (offset < 0 || offset + length > gpu_buf->size) {
		SET_GL_ERROR(GL_INVALID_VALUE)
	}
#endif
}

inline void glGetNamedBufferParameteriv(GLuint buffer, GLenum pname, GLint *params) {
	THREAD_SAFE()

	vbo *gpu_buf = (vbo *)buffer;
#ifndef SKIP_ERROR_HANDLING
	if (!gpu_buf) {
		SET_GL_ERROR(GL_INVALID_OPERATION)
	}
#endif

	switch (pname) {
	case GL_BUFFER_SIZE:
		*params = gpu_buf->size;
		break;
	default:
		SET_GL_ERROR_WITH_VALUE(GL_INVALID_ENUM, pname)
	}
}

void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params) {
	THREAD_SAFE()

	vbo *gpu_buf;
	switch (target) {
	case GL_ARRAY_BUFFER:
		gpu_buf = (vbo *)vertex_array_unit;
		break;
	case GL_ELEMENT_ARRAY_BUFFER:
		gpu_buf = (vbo *)cur_vao->index_array_unit;
		break;
	case GL_UNIFORM_BUFFER:
		gpu_buf = (vbo *)uniform_array_unit;
		break;
	default:
		SET_GL_ERROR_WITH_VALUE(GL_INVALID_ENUM, target)
	}
	
	glGetNamedBufferParameteriv((GLuint)gpu_buf, pname, params);
}

// VGL_EXT_gpu_objects_array extension implementation

void vglVertexPointer(GLint size, GLenum type, GLsizei stride, GLuint count, const GLvoid *pointer) {
	THREAD_SAFE()

#ifndef SKIP_ERROR_HANDLING
	if ((stride < 0) || (size < 2) || (size > 4)) {
		SET_GL_ERROR(GL_INVALID_VALUE)
	}
#endif
	SceGxmVertexAttribute *attributes = &ffp_vertex_attrib_config[FFP_ATTRIB_POSITION];
	SceGxmVertexStream *streams = &ffp_vertex_stream_config[FFP_ATTRIB_POSITION];

	unsigned short bpe;
	switch (type) {
	case GL_FLOAT:
		attributes->format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
		bpe = 4;
		break;
	case GL_SHORT:
		attributes->format = SCE_GXM_ATTRIBUTE_FORMAT_S16N;
		bpe = 2;
		break;
	default:
		SET_GL_ERROR_WITH_VALUE(GL_INVALID_ENUM, type)
	}

	attributes->componentCount = size;
	streams->stride = stride ? stride : bpe * size;

	vertex_object = gpu_alloc_mapped_temp(count * streams->stride);
	vgl_fast_memcpy(vertex_object, pointer, count * streams->stride);
}

void vglColorPointer(GLint size, GLenum type, GLsizei stride, GLuint count, const GLvoid *pointer) {
	THREAD_SAFE()

#ifndef SKIP_ERROR_HANDLING
	if ((stride < 0) || (size < 3) || (size > 4)) {
		SET_GL_ERROR(GL_INVALID_VALUE)
	}
#endif
	SceGxmVertexAttribute *attributes = &ffp_vertex_attrib_config[FFP_ATTRIB_COLOR];
	SceGxmVertexStream *streams = &ffp_vertex_stream_config[FFP_ATTRIB_COLOR];

	unsigned short bpe;
	switch (type) {
	case GL_FLOAT:
		attributes->format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
		bpe = 4;
		break;
	case GL_SHORT:
		attributes->format = SCE_GXM_ATTRIBUTE_FORMAT_S16N;
		bpe = 2;
		break;
	case GL_UNSIGNED_SHORT:
		attributes->format = SCE_GXM_ATTRIBUTE_FORMAT_U16N;
		bpe = 2;
		break;
	case GL_BYTE:
		attributes->format = SCE_GXM_ATTRIBUTE_FORMAT_S8N;
		bpe = 1;
		break;
	case GL_UNSIGNED_BYTE:
		attributes->format = SCE_GXM_ATTRIBUTE_FORMAT_U8N;
		bpe = 1;
		break;
	default:
		SET_GL_ERROR_WITH_VALUE(GL_INVALID_ENUM, type)
	}

	attributes->componentCount = size;
	streams->stride = stride ? stride : bpe * size;

	color_object = gpu_alloc_mapped_temp(count * streams->stride);
	vgl_fast_memcpy(color_object, pointer, count * streams->stride);
}

void vglTexCoordPointer(GLint size, GLenum type, GLsizei stride, GLuint count, const GLvoid *pointer) {
	THREAD_SAFE()

#ifndef SKIP_ERROR_HANDLING
	if ((stride < 0) || (size < 2) || (size > 4)) {
		SET_GL_ERROR(GL_INVALID_VALUE)
	}
#endif
	SceGxmVertexAttribute *attributes = &ffp_vertex_attrib_config[FFP_ATTRIB_TEX0];
	SceGxmVertexStream *streams = &ffp_vertex_stream_config[FFP_ATTRIB_TEX0];

	unsigned short bpe;
	switch (type) {
	case GL_FLOAT:
		attributes->format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
		bpe = 4;
		break;
	case GL_SHORT:
		attributes->format = SCE_GXM_ATTRIBUTE_FORMAT_S16N;
		bpe = 2;
		break;
	default:
		SET_GL_ERROR_WITH_VALUE(GL_INVALID_ENUM, type)
	}

	attributes->componentCount = size;
	streams->stride = stride ? stride : bpe * size;

	texture_object = gpu_alloc_mapped_temp(count * streams->stride);
	vgl_fast_memcpy(texture_object, pointer, count * streams->stride);
}

void vglIndexPointer(GLenum type, GLsizei stride, GLuint count, const GLvoid *pointer) {
	THREAD_SAFE()

#ifndef SKIP_ERROR_HANDLING
	if (stride < 0) {
		SET_GL_ERROR(GL_INVALID_VALUE)
	}
#endif
	int bpe;
	switch (type) {
	case GL_SHORT:
		bpe = sizeof(GLshort);
		break;
	default:
		SET_GL_ERROR_WITH_VALUE(GL_INVALID_ENUM, type)
	}
	index_object = gpu_alloc_mapped_temp(count * bpe);
	if (stride == 0)
		vgl_fast_memcpy(index_object, pointer, count * bpe);
	else {
		int i;
		uint8_t *dst = (uint8_t *)index_object;
		uint8_t *src = (uint8_t *)pointer;
		for (i = 0; i < count; i++) {
			vgl_fast_memcpy(dst, src, bpe);
			dst += bpe;
			src += stride;
		}
	}
}

void vglVertexPointerMapped(GLint size, const GLvoid *pointer) {
	THREAD_SAFE()

	SceGxmVertexAttribute *attributes = &ffp_vertex_attrib_config[FFP_ATTRIB_POSITION];
	SceGxmVertexStream *streams = &ffp_vertex_stream_config[FFP_ATTRIB_POSITION];

	attributes->format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
	attributes->componentCount = size;
	streams->stride = size * 4;

	vertex_object = (GLvoid *)pointer;
}

void vglColorPointerMapped(GLenum type, const GLvoid *pointer) {
	THREAD_SAFE()

	SceGxmVertexAttribute *attributes = &ffp_vertex_attrib_config[FFP_ATTRIB_COLOR];
	SceGxmVertexStream *streams = &ffp_vertex_stream_config[FFP_ATTRIB_COLOR];

	unsigned short bpe;
	switch (type) {
	case GL_FLOAT:
		attributes->format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
		bpe = 4;
		break;
	case GL_SHORT:
		attributes->format = SCE_GXM_ATTRIBUTE_FORMAT_S16N;
		bpe = 2;
		break;
	case GL_UNSIGNED_SHORT:
		attributes->format = SCE_GXM_ATTRIBUTE_FORMAT_U16N;
		bpe = 2;
		break;
	case GL_BYTE:
		attributes->format = SCE_GXM_ATTRIBUTE_FORMAT_S8N;
		bpe = 1;
		break;
	case GL_UNSIGNED_BYTE:
		attributes->format = SCE_GXM_ATTRIBUTE_FORMAT_U8N;
		bpe = 1;
		break;
	default:
		SET_GL_ERROR_WITH_VALUE(GL_INVALID_ENUM, type)
	}

	attributes->componentCount = 4;
	streams->stride = 4 * bpe;

	color_object = (GLvoid *)pointer;
}

void vglTexCoordPointerMapped(const GLvoid *pointer) {
	THREAD_SAFE()

	SceGxmVertexAttribute *attributes = &ffp_vertex_attrib_config[FFP_ATTRIB_TEX0];
	SceGxmVertexStream *streams = &ffp_vertex_stream_config[FFP_ATTRIB_TEX0];

	attributes->format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
	attributes->componentCount = 2;
	streams->stride = 8;

	texture_object = (GLvoid *)pointer;
}

void vglIndexPointerMapped(const GLvoid *pointer) {
	THREAD_SAFE()

	index_object = (GLvoid *)pointer;
}

void vglBufferData(GLenum target, const GLvoid *data) {
	THREAD_SAFE()

	vbo *gpu_buf;
	switch (target) {
	case GL_ARRAY_BUFFER:
		gpu_buf = (vbo *)vertex_array_unit;
		break;
	case GL_ELEMENT_ARRAY_BUFFER:
		gpu_buf = (vbo *)cur_vao->index_array_unit;
		break;
	case GL_UNIFORM_BUFFER:
		gpu_buf = (vbo *)uniform_array_unit;
		break;
	default:
		SET_GL_ERROR_WITH_VALUE(GL_INVALID_ENUM, target)
	}
#ifndef SKIP_ERROR_HANDLING
	if (!gpu_buf) {
		SET_GL_ERROR(GL_INVALID_OPERATION)
	}
#endif

	gpu_buf->ptr = (GLvoid *)data;
}

void vglSetVertexAttribPoolSize(uint32_t main_size, uint32_t aux_size) {
	main_vertex_attrib_pool_size = main_size;
	aux_vertex_attrib_pool_size = aux_size;
}
