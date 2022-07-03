// #define HANDMADE_MATH_NO_SSE
#define HANDMADE_MATH_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define SOKOL_IMPL
#define SOKOL_D3D11

#include "HandmadeMath.h"
#include "sokol_gfx.h"
#include "sokol_app.h"
#include "sokol_fetch.h"
#include "sokol_glue.h"
#include "stb_image.h"
#include "fileutil.h"
#include "loadpng-sapp.glsl.h"

#include "spine/spine.h"
#include "spine/extension.h"

void spine_setup();
void spine_dispose();
void spine_draw_skeleton(spSkeleton*);
void spine_texture_callback(const sfetch_response_t*);

uint8_t file_buffer[1024 * 1024];
spAtlas* atlas;
spSkeletonData* skeletonData;
spAnimationStateData* animationStateData;
spSkeleton* skeletons[5];
spAnimationState* animationStates[5];
char* animationNames[] = { "walk", "run", "shot" };

static struct {
    float rotation;
    sg_pass_action pass_action;
    sg_pipeline pip;
    sg_bindings bind;
    uint8_t file_buffer[512 * 1024];
} state;

typedef struct {
    float x, y;
    int16_t u, v;
} vertex_t;

static void init(void) {
    sg_setup(&(sg_desc){
        .context = sapp_sgcontext()
    });

    sfetch_setup(&(sfetch_desc_t){
        .max_requests = 1,
        .num_channels = 1,
        .num_lanes = 1
    });

    spine_setup();

    state.pass_action = (sg_pass_action){
        .colors[0] = {
            .action = SG_ACTION_CLEAR,
            .value = {
                0.0f, 0.0f, 0.0f, 1.0f
            }
        }
    };

    state.bind.fs_images[SLOT_tex] = sg_alloc_image();

    const vertex_t vertices[] = {{0}};

    state.bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
        .data = SG_RANGE(vertices),
        .label = "quad-vertices"
    });

    const uint16_t indices[] = { 0 };

    state.bind.index_buffer = sg_make_buffer(&(sg_buffer_desc){
        .type = SG_BUFFERTYPE_INDEXBUFFER,
        .data = SG_RANGE(indices),
        .label = "quad-indices"
    });

    state.pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = sg_make_shader(loadpng_shader_desc(sg_query_backend())),
        .layout = {
            .attrs = {
                [ATTR_vs_pos].format = SG_VERTEXFORMAT_FLOAT2,
                [ATTR_vs_texcoord0].format = SG_VERTEXFORMAT_SHORT2N,
            }
        },
        .index_type = SG_INDEXTYPE_UINT16,
        .label = "quad-pipeline"
    });
}

static void frame(void) {
    sfetch_dowork();

    sg_begin_default_pass(&state.pass_action, sapp_width(), sapp_height());
    sg_apply_pipeline(state.pip);
    sg_apply_bindings(&state.bind);

    const float dt = (float)(sapp_frame_duration() * 60.0);

    for (int i = 0; i < 5; i++) {
       spSkeleton* skeleton = skeletons[i];
       spAnimationState* animationState = animationStates[i];
       spAnimationState_update(animationState, dt);
       spAnimationState_apply(animationState, skeleton);
       spSkeleton_updateWorldTransform(skeleton);
       spine_draw_skeleton(skeleton);
    }

    // sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_vs_params, &SG_RANGE(vs_params));
    sg_draw(0, 6, 1);
    sg_end_pass();
    sg_commit();
}

static void cleanup(void) {
    spine_dispose();
    sfetch_shutdown();
    sg_shutdown();
}

sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    return (sapp_desc){
        .init_cb = init,
        .frame_cb = frame,
        .cleanup_cb = cleanup,
        .width = 1200,
        .height = 1200,
        .sample_count = 4,
        .gl_force_gles2 = true,
        .window_title = "Async PNG Loading (sokol-app)",
        .icon.sokol_default = true,
    };
}

void spine_setup() {
   atlas = spAtlas_createFromFile("..\\spine-examples\\spineboy\\spineboy.atlas", 0);
   if (!atlas) {
      fprintf(stderr, "Failed to load atlas\n");
      exit(-1);
   }
   printf("cool?\n");

   spSkeletonJson* json = spSkeletonJson_create(atlas);
   skeletonData = spSkeletonJson_readSkeletonDataFile(json, "..\\spine-examples\\spineboy\\spineboy.json");
   if (!skeletonData) {
      fprintf(stderr, "Failed to load skeleton data\n");
      spAtlas_dispose(atlas);
      exit(-1);
   }
   spSkeletonJson_dispose(json);

   animationStateData = spAnimationStateData_create(skeletonData);
   animationStateData->defaultMix = 0.5f;
   spAnimationStateData_setMixByName(animationStateData, "walk", "run", 0.2f);
   spAnimationStateData_setMixByName(animationStateData, "walk", "shot", 0.1f);

   for (int i = 0; i < 5; i++) {
        spSkeleton* skeleton = spSkeleton_create(skeletonData);    
        skeleton->x = 200;
        skeleton->y = 200;
        spAnimationState* animationState = spAnimationState_create(animationStateData);
        spAnimation* animation = spSkeletonData_findAnimation(skeletonData, animationNames[0]);
        spAnimationState_setAnimation(animationState, 0, animation, 1);
   }
}

void spine_dispose() {
   spAtlas_dispose(atlas);
   spSkeletonData_dispose(skeletonData);
   spAnimationStateData_dispose(animationStateData);
}

void spine_draw_skeleton(spSkeleton* skeleton) {

}

char* _spUtil_readFile(const char* path, int* length) {
    return _spReadFile(path, length);
}

void _spAtlasPage_createTexture(spAtlasPage *self, const char *path) {
    sfetch_send(&(sfetch_request_t){
        .path = path,
        .callback = spine_texture_callback,
        .buffer_ptr = file_buffer,
        .buffer_size = sizeof(file_buffer),
        .user_data_ptr = self,
        .user_data_size = sizeof(spAtlasPage)
    });
}

void spine_texture_callback(const sfetch_response_t* response) {
    if (response->failed) {
        fprintf(stderr, "spine_texture_callback: fail\n");
        return;
    }

    int png_width, png_height, num_channels;
    const int desired_channels = 4;
    stbi_set_flip_vertically_on_load(1);
    stbi_uc* pixels = stbi_load_from_memory(
        response->buffer_ptr,
        (int)response->fetched_size,
        &png_width,
        &png_height,
        &num_channels,
        desired_channels
    );

    if (pixels == NULL) {
        fprintf(stderr, "No pixels, LOL.\n");
        return;
    }

    sg_image texture;
    sg_init_image(texture, &(sg_image_desc){
        .width = png_width,
        .height = png_height,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .data.subimage[0][0] = {
            .ptr = pixels,
            .size = (size_t)(png_width * png_height * 4),
        }
    });
    // stbi_image_free(pixels);

    spAtlasPage* atlasPage = response->user_data;
    atlasPage->rendererObject = &texture;
    atlasPage->width = png_width;
    atlasPage->height = png_height;
}

void _spAtlasPage_disposeTexture(spAtlasPage *self) {
    // sg_image texture = (sg_image *)self->rendererObject;
    // sg_dealloc_image(texture->id);
}
