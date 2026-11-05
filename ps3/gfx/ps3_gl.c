/*
 * libGDX GL20 menu subset for PSL1GHT. No GL library or runtime shader compiler.
 * GL buffers are CPU shadows; indexed draws expand into a fenced RSX vertex stream.
 * RGBA images become native ARGB bytes; DDS DXT blocks remain compressed in VRAM.
 * Original GLSL declarations provide libGDX reflection. Executable shader semantics
 * are the offline sprite/font/shape permutation in menu.vcg/menu.fcg.
 */
#include "ps3_gl.h"
#include "stsv_gl.h"
#include "stsvita_glue.h"
#include "stsv_ps3_platform.h"
#include "rsx_ring.h"
#include "menu_vpo.h"
#include "menu_fpo.h"
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>
#include <sysutil/video.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>

#define MAX_OBJECTS 4096
#define MAX_PROGRAMS 128
#define MAX_SHADERS 256
#define MAX_VARIABLES 64
#define STREAM_VERTICES 65536
/* Each frame fills one half while the GPU may still read the other. */
#define STREAM_HALF (STREAM_VERTICES/2)
#define FRAME_LABEL 253
/* 256 MiB RSX local memory minus ~13 MiB of display, depth and vertex buffers.
 * The menu alone uses ~124 MiB; a run adds character, map and monster art. */
#define TEXTURE_BUDGET (232u*1024u*1024u)
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_VALUE 0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_OUT_OF_MEMORY 0x0505
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_FRAMEBUFFER 0x8d40
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_BLEND 0x0be2
#define GL_SCISSOR_TEST 0x0c11
#define GL_DEPTH_TEST 0x0b71
#define GL_CULL_FACE 0x0b44

typedef struct { void *data; size_t size; int live,usage; } Buffer;
typedef struct {
    void *pixels; u32 offset,pitch,bytes; int width,height,format;
    int live,min_filter,mag_filter,wrap_s,wrap_t;
    int opaque; /* GL RGB format: sample alpha as 1 */
    unsigned generation; /* bumped by texture_changed(); keys the RSX bind cache */
} Texture;
typedef struct { int live,texture,depth_renderbuffer,stencil_renderbuffer; } Framebuffer;
typedef struct { int live,width,height,format; } Renderbuffer;
typedef struct { int live,type,compiled; char *source; } Shader;
typedef struct { char name[64]; int type,size; float value[16]; } Variable;
typedef struct {
    int live,linked,vertex,fragment,attr_count,uniform_count,textured,alpha_correction;
    Variable attrs[MAX_VARIABLES],uniforms[MAX_VARIABLES];
    int slots_ready,pos,col,uv; /* attribute and uniform lookups cached after link */
    Variable *matrix,*color,*sampler;
} Program;
typedef struct { int enabled,size,type,normalized,stride,buffer; const void *pointer; float value[4]; void *snapshot; size_t snapshot_capacity,snapshot_size; } Attribute;
typedef struct { float pos[4],color[4],uv[2]; } Vertex;
static Buffer buffers[MAX_OBJECTS];
static Texture textures[MAX_OBJECTS];
static Framebuffer framebuffers[MAX_OBJECTS];
static Renderbuffer renderbuffers[MAX_OBJECTS];
static Shader shaders[MAX_SHADERS];
static Program programs[MAX_PROGRAMS];
static Attribute attributes[16];
static gcmContextData *ctx;
static u32 width,height,pitch,back_offset[2],depth_offset,depth_pitch,depth_height,frame_index;
static void *depth_pixels;
static u32 stream_offset,stream_cursor,fragment_offset,label_value=1;
static u32 stream_half,frame_serial,half_serial[2]; /* half_serial: frame that last used each half */
static Vertex *stream;
static rsxVertexProgram *vp;
static rsxFragmentProgram *fp;
static void *vp_ucode;
static int first_flip=1,error_code,active_texture,bound_texture[8],bound_array,bound_elements;
static int bound_framebuffer,bound_renderbuffer,current_program;
static int blend_enabled,scissor_enabled,depth_enabled,cull_enabled,depth_write;
static int blend_src=1,blend_dst,blend_src_a=1,blend_dst_a,blend_equation=0x8006,blend_equation_a=0x8006;
static int view[4],scissor[4],unpack_alignment=4,color_mask=15;
static float clear_color[4],clear_depth=1,depth_near,depth_far=1;
static int clear_stencil;
static size_t texture_bytes;
static Texture white;
static int programs_bound; /* the menu's single vertex/fragment pair stays loaded across draws */
static Texture *bound_rsx_texture;
static unsigned bound_rsx_generation;
static unsigned texture_generation_counter;
static int texture_cache_dirty=1;
static void texture_changed(Texture *t){t->generation=++texture_generation_counter;texture_cache_dirty=1;}
static char texture_path[192]="?";
static int64_t upload_stats,subtex_stats;
static unsigned compressed_upload_count,draw_count;
static unsigned frame_draws,frame_vertices; /* reset at every swap */
static int64_t draw_us; /* CPU time inside draw(), reset every stats window */

static void set_error(int error) { if(!error_code) error_code=error; }
void stsv_gl_unsupported(const char *name) {
    static const char *reported[128]; static int count;
    set_error(GL_INVALID_OPERATION);
    for(int i=0;i<count;i++) if(!strcmp(reported[i],name)) return;
    if(count<128) reported[count++]=name;
    fprintf(stderr,"[PS3 GL] unsupported menu operation: %s\n",name);
}
static void fatal(const char *message) { fprintf(stderr,"[PS3 RSX] %s\n",message); fflush(stderr); exit(1); }
static int valid_id(int id) { return id>0 && id<MAX_OBJECTS; }
static Texture *current_texture(void) { int id=bound_texture[active_texture]; return valid_id(id)&&textures[id].live?&textures[id]:NULL; }
static Program *program(void) { return current_program>0&&current_program<MAX_PROGRAMS?&programs[current_program]:NULL; }
static void fence(void) {
    if(!ctx) return;
    rsx_ring_make_room(ctx);
    rsxSetWriteBackendLabel(ctx,254,label_value);
    rsxFlushBuffer(ctx);
    while(*(vu32*)gcmGetLabelAddress(254)!=label_value) usleep(30);
    label_value++;
}
static u32 remap_identity(void) {
    return GCM_TEXTURE_REMAP_MODE(GCM_TEXTURE_REMAP_ORDER_XYXY,
        GCM_TEXTURE_REMAP_COLOR_A,GCM_TEXTURE_REMAP_COLOR_R,GCM_TEXTURE_REMAP_COLOR_G,GCM_TEXTURE_REMAP_COLOR_B,
        GCM_TEXTURE_REMAP_TYPE_REMAP,GCM_TEXTURE_REMAP_TYPE_REMAP,GCM_TEXTURE_REMAP_TYPE_REMAP,GCM_TEXTURE_REMAP_TYPE_REMAP);
}
/* GL's COMPRESSED_RGB_S3TC_DXT1 decodes the 3-color mode's fourth code as
 * opaque black, but the RSX always reads DXT1 with 1-bit alpha. Forcing alpha
 * to one keeps JPG-derived layers from showing 4x4 holes. */
static u32 remap_opaque(void) {
    return GCM_TEXTURE_REMAP_MODE(GCM_TEXTURE_REMAP_ORDER_XYXY,
        GCM_TEXTURE_REMAP_COLOR_A,GCM_TEXTURE_REMAP_COLOR_R,GCM_TEXTURE_REMAP_COLOR_G,GCM_TEXTURE_REMAP_COLOR_B,
        GCM_TEXTURE_REMAP_TYPE_ONE,GCM_TEXTURE_REMAP_TYPE_REMAP,GCM_TEXTURE_REMAP_TYPE_REMAP,GCM_TEXTURE_REMAP_TYPE_REMAP);
}
static int surface_width(void) { return bound_framebuffer?textures[framebuffers[bound_framebuffer].texture].width:(int)width; }
static int surface_height(void) { return bound_framebuffer?textures[framebuffers[bound_framebuffer].texture].height:(int)height; }
static void apply_view(void) {
    int h=surface_height();
    f32 scale[4]={view[2]*0.5f,view[3]*-0.5f,(depth_far-depth_near)*0.5f,0};
    f32 bias[4]={view[0]+view[2]*0.5f,h-view[1]-view[3]*0.5f,(depth_far+depth_near)*0.5f,0};
    rsxSetViewport(ctx,view[0],h-view[1]-view[3],view[2],view[3],depth_near,depth_far,scale,bias);
    if(scissor_enabled) {
        int x=scissor[0],y=h-scissor[1]-scissor[3],w=scissor[2],sh=scissor[3];
        if(x<0){w+=x;x=0;} if(y<0){sh+=y;y=0;}
        if(x+w>surface_width())w=surface_width()-x;
        if(y+sh>h)sh=h-y;
        rsxSetScissor(ctx,x<0?0:x,y<0?0:y,w>0?w:0,sh>0?sh:0);
    } else rsxSetScissor(ctx,0,0,surface_width(),h);
}
static void apply_state(void) {
    rsxSetColorMask(ctx,((color_mask&1)?GCM_COLOR_MASK_R:0)|((color_mask&2)?GCM_COLOR_MASK_G:0)|((color_mask&4)?GCM_COLOR_MASK_B:0)|((color_mask&8)?GCM_COLOR_MASK_A:0));
    rsxSetColorMaskMrt(ctx,0);
    rsxSetDepthTestEnable(ctx,depth_enabled);
    rsxSetDepthWriteEnable(ctx,depth_write);
    rsxSetCullFaceEnable(ctx,cull_enabled);
    rsxSetShadeModel(ctx,GCM_SHADE_MODEL_SMOOTH);
    rsxSetBlendEnable(ctx,blend_enabled);
    rsxSetBlendFunc(ctx,blend_src,blend_dst,blend_src_a,blend_dst_a);
    rsxSetBlendEquation(ctx,blend_equation,blend_equation_a);
    apply_view();
}
static int ensure_depth(u32 w,u32 h) {
    u32 p=(w*4+63)&~63u;
    if(p<=depth_pitch&&h<=depth_height)return 1;
    if(p<depth_pitch)p=depth_pitch;
    if(h<depth_height)h=depth_height;
    fence();
    void *pixels=rsxMemalign(64,p*h);u32 offset;
    if(!pixels||rsxAddressToOffset(pixels,&offset)){if(pixels)rsxFree(pixels);set_error(GL_OUT_OF_MEMORY);return 0;}
    if(depth_pixels)rsxFree(depth_pixels);
    depth_pixels=pixels;depth_offset=offset;depth_pitch=p;depth_height=h;
    return 1;
}
static int bind_surface(void) {
    gcmSurface s;
    memset(&s,0,sizeof(s));
    s.type=GCM_SURFACE_TYPE_LINEAR; s.antiAlias=GCM_SURFACE_CENTER_1;
    s.colorFormat=bound_framebuffer?GCM_SURFACE_A8R8G8B8:GCM_SURFACE_X8R8G8B8;
    s.colorTarget=GCM_SURFACE_TARGET_0;
    s.colorLocation[0]=GCM_LOCATION_RSX;
    s.colorOffset[0]=back_offset[frame_index]; s.colorPitch[0]=pitch;
    s.width=width; s.height=height;
    if(bound_framebuffer) {
        int id=framebuffers[bound_framebuffer].texture;
        if(!valid_id(id)||!textures[id].pixels||textures[id].format!=GCM_TEXTURE_FORMAT_A8R8G8B8) {set_error(GL_INVALID_OPERATION);return 0;}
        Texture *t=&textures[id];
        s.colorOffset[0]=t->offset;s.colorPitch[0]=t->pitch;s.width=t->width;s.height=t->height;
    }
    for(int i=1;i<GCM_MAX_MRT_COUNT;i++){s.colorLocation[i]=GCM_LOCATION_RSX;s.colorPitch[i]=64;}
    s.depthFormat=GCM_SURFACE_ZETA_Z24S8;s.depthLocation=GCM_LOCATION_RSX;
    if(!ensure_depth(s.width,s.height))return 0;
    s.depthOffset=depth_offset;s.depthPitch=depth_pitch;
    rsxSetSurface(ctx,&s);
    apply_state();
    for(int i=0;i<8;i++)rsxSetViewportClip(ctx,i,s.width,s.height);
    return 1;
}
void stsv_gfx_init(void) {
    if(ctx)return;
    void *host=memalign(1024*1024,8*1024*1024);
    if(!host||rsxInit(&ctx,0x80000,8*1024*1024,host))fatal("rsxInit failed");
    if(rsx_ring_attach(ctx,(char*)host+0x100000,4*1024*1024))fatal("command ring failed");
    videoState state;videoResolution res;videoConfiguration config;
    if(videoGetState(0,0,&state))fatal("videoGetState failed");
    u8 mode=state.displayMode.resolution;
    if(videoGetResolutionAvailability(0,VIDEO_RESOLUTION_720,VIDEO_ASPECT_AUTO,0)==1)mode=VIDEO_RESOLUTION_720;
    if(videoGetResolution(mode,&res))fatal("video resolution failed");
    width=res.width;height=res.height;pitch=(width*4+63)&~63u;
    memset(&config,0,sizeof(config));config.resolution=mode;config.format=VIDEO_BUFFER_FORMAT_XRGB;
    config.aspect=state.displayMode.aspect;config.pitch=pitch;
    if(videoConfigure(0,&config,NULL,0))fatal("videoConfigure failed");
    for(int i=0;i<2;i++) {
        void *p=rsxMemalign(64,pitch*height);
        if(!p||rsxAddressToOffset(p,&back_offset[i]))fatal("display memory exhausted");
        if(gcmSetDisplayBuffer(i,back_offset[i],pitch,width,height))fatal("display registration failed");
    }
    depth_pixels=rsxMemalign(64,pitch*height);depth_pitch=pitch;depth_height=height;
    stream=rsxMemalign(128,STREAM_VERTICES*sizeof(Vertex));
    *(vu32*)gcmGetLabelAddress(FRAME_LABEL)=0;
    if(!depth_pixels||!stream||rsxAddressToOffset(depth_pixels,&depth_offset)||rsxAddressToOffset(stream,&stream_offset))fatal("render memory exhausted");
    vp=(rsxVertexProgram*)menu_vpo;fp=(rsxFragmentProgram*)menu_fpo;
    void *code;u32 size;
    rsxVertexProgramGetUCode(vp,&vp_ucode,&size);
    rsxFragmentProgramGetUCode(fp,&code,&size);
    void *fragment=rsxMemalign(64,size);
    if(!fragment||rsxAddressToOffset(fragment,&fragment_offset))fatal("fragment memory exhausted");
    memcpy(fragment,code,size);
    white.pixels=rsxMemalign(128,128);white.width=white.height=1;white.pitch=64;white.format=GCM_TEXTURE_FORMAT_A8R8G8B8;
    if(!white.pixels||rsxAddressToOffset(white.pixels,&white.offset))fatal("white texture failed");
    memset(white.pixels,255,128);white.min_filter=white.mag_filter=0x2601;
    white.wrap_s=white.wrap_t=0x812f;texture_changed(&white);
    for(int i=0;i<16;i++){attributes[i].value[3]=1;attributes[i].size=4;attributes[i].type=GL_FLOAT;}
    view[2]=scissor[2]=width;view[3]=scissor[3]=height;
    gcmSetFlipMode(GCM_FLIP_VSYNC);gcmResetFlipStatus();
    programs_bound=0;bound_rsx_texture=NULL;
    bind_surface();
    printf("[PS3 RSX] menu adapter %ux%u, sprite shader offline, %u MiB texture budget\n",width,height,TEXTURE_BUDGET/(1024*1024));
}
int stsv_gfx_width(void){return width;}
int stsv_gfx_height(void){return height;}
int stsv_is_host_harness(void){return 0;}
void stsv_set_vsync(int on){if(ctx)gcmSetFlipMode(on?GCM_FLIP_VSYNC:GCM_FLIP_HSYNC);}
/* The test harness lists frame numbers, one per line, in this USRDIR file. */
#define CAPTURE_REQUEST_PATH "/dev_hdd0/game/STSP00001/USRDIR/sts-capture-menu"
#define CAPTURE_MAX 64
int stsv_gfx_dump_frame(const char *path);
static void capture_if_requested(unsigned presented){
    static unsigned frames[CAPTURE_MAX];
    static int count=-1;
    if(count<0){
        unsigned frame;
        FILE *request=fopen(CAPTURE_REQUEST_PATH,"r");
        count=0;
        if(!request)return;
        while(count<CAPTURE_MAX&&fscanf(request,"%u",&frame)==1)frames[count++]=frame;
        fclose(request);
        fprintf(stderr,"[PS3 capture] %d frame(s) requested\n",count);
    }
    for(int i=0;i<count;i++){
        char path[64];
        if(frames[i]!=presented)continue;
        snprintf(path,sizeof(path),"/dev_hdd0/tmp/sts-frame-%u.ppm",presented);
        stsv_gfx_dump_frame(path);
    }
}
/* Mark this frame's half as in flight, switch halves, and wait only until the
 * GPU has finished the frame that last used the half we are about to fill. */
static void finish_stream_half(void){
    vu32 *done=(vu32*)gcmGetLabelAddress(FRAME_LABEL);
    rsxSetWriteBackendLabel(ctx,FRAME_LABEL,++frame_serial);
    half_serial[stream_half]=frame_serial;
    stream_half^=1;stream_cursor=0;
    rsxFlushBuffer(ctx);
    while((s32)(*done-half_serial[stream_half])<0)usleep(30);
}
void stsv_swap_buffers(void){
    stsv_ps3_pump_callbacks();
    if(!ctx)return;
    rsx_ring_make_room(ctx);
    int64_t fence_start=stsv_ps3_monotonic_us();
    finish_stream_half();
    static unsigned presented;
    static int64_t window_start,fence_us;
    static unsigned window_draws,window_vertices;
    int64_t now=stsv_ps3_monotonic_us();
    fence_us+=now-fence_start;window_draws+=frame_draws;window_vertices+=frame_vertices;frame_draws=frame_vertices=0;
    presented++;
    stsv_ps3_input_next_frame();
    if(presented==1||presented==600||presented%1800==0){char tag[32];snprintf(tag,sizeof(tag),"frame %u",presented);stsv_ps3_log_memory(tag);}
    if(presented%300==0){
        double frames=300.0;
        fprintf(stderr,"[PS3 frame] %u: %.2f ms/frame, gpu wait %.2f ms, draw cpu %.2f ms, %.0f draws, %.0f vertices per frame\n",presented,
                window_start?(now-window_start)/1000.0/frames:0.0,fence_us/1000.0/frames,draw_us/1000.0/frames,window_draws/frames,window_vertices/frames);
        fflush(stderr);window_start=now;fence_us=0;draw_us=0;window_draws=window_vertices=0;
    }
    capture_if_requested(presented);
    if(!first_flip)while(gcmGetFlipStatus())usleep(200);
    first_flip=0;gcmResetFlipStatus();
    gcmSetFlip(ctx,frame_index);rsxFlushBuffer(ctx);gcmSetWaitFlip(ctx);
    frame_index^=1;
    if(!bound_framebuffer)bind_surface();
}
void glFinish(void){fence();}
void glFlush(void){if(ctx)rsxFlushBuffer(ctx);}
int glGetError(void){int result=error_code;error_code=0;return result;}
void *glGetString(int name){
    switch(name){case 0x1f00:return "PSL1GHT";case 0x1f01:return "RSX menu adapter";
    case 0x1f02:return "OpenGL ES 2.0 STSV menu subset";case 0x8b8c:return "OpenGL ES GLSL ES 1.00 offline sprite mapping";
    case 0x1f03:return "GL_EXT_texture_compression_s3tc GL_EXT_texture_format_BGRA8888";default:set_error(GL_INVALID_ENUM);return "";}
}
void glGetIntegerv(int name,void *out){
    int *p=out;if(!p)return;
    switch(name){case 0x0ba2:memcpy(p,view,sizeof(view));return;case 0x0c10:memcpy(p,scissor,sizeof(scissor));return;
    case 0x0d33:*p=4096;break;case 0x8869:*p=16;break;case 0x8872:case 0x8b4d:*p=8;break;
    case 0x8dfb:*p=128;break;case 0x8dfd:*p=64;break;case 0x8dfc:*p=8;break;
    case 0x0d57:*p=0;break;case 0x0d56:*p=24;break;case 0x0d52:case 0x0d53:case 0x0d54:case 0x0d55:*p=8;break;
    case 0x8ca6:*p=bound_framebuffer;break;case 0x8069:*p=bound_texture[active_texture];break;
    case 0x8894:*p=bound_array;break;case 0x8895:*p=bound_elements;break;case 0x8b8d:*p=current_program;break;
    case 0x84e0:*p=0x84c0+active_texture;break;case 0x0cf5:*p=unpack_alignment;break;
    case 0x0be1:*p=blend_src;break;case 0x0be0:*p=blend_dst;break;case 0x80a9:case 0x80a8:*p=0;break;
    default:*p=0;set_error(GL_INVALID_ENUM);break;}
}
void glViewport(int x,int y,int w,int h){if(w<0||h<0){set_error(GL_INVALID_VALUE);return;}view[0]=x;view[1]=y;view[2]=w;view[3]=h;if(ctx)apply_view();}
void glScissor(int x,int y,int w,int h){if(w<0||h<0){set_error(GL_INVALID_VALUE);return;}scissor[0]=x;scissor[1]=y;scissor[2]=w;scissor[3]=h;if(ctx)apply_view();}
void glEnable(int cap){switch(cap){case GL_BLEND:blend_enabled=1;break;case GL_SCISSOR_TEST:scissor_enabled=1;break;case GL_DEPTH_TEST:depth_enabled=1;break;case GL_CULL_FACE:cull_enabled=1;break;case 0x0bd0:return;default:set_error(GL_INVALID_ENUM);return;}if(ctx)apply_state();}
void glDisable(int cap){switch(cap){case GL_BLEND:blend_enabled=0;break;case GL_SCISSOR_TEST:scissor_enabled=0;break;case GL_DEPTH_TEST:depth_enabled=0;break;case GL_CULL_FACE:cull_enabled=0;break;case 0x0bd0:return;default:set_error(GL_INVALID_ENUM);return;}if(ctx)apply_state();}
int glIsEnabled(int cap){switch(cap){case GL_BLEND:return blend_enabled;case GL_SCISSOR_TEST:return scissor_enabled;case GL_DEPTH_TEST:return depth_enabled;case GL_CULL_FACE:return cull_enabled;default:return 0;}}
void glBlendFunc(int src,int dst){blend_src=blend_src_a=src;blend_dst=blend_dst_a=dst;if(ctx)apply_state();}
void glBlendFuncSeparate(int sr,int dr,int sa,int da){blend_src=sr;blend_dst=dr;blend_src_a=sa;blend_dst_a=da;if(ctx)apply_state();}
void glBlendEquation(int mode){blend_equation=blend_equation_a=mode;if(ctx)apply_state();}
void glBlendEquationSeparate(int rgb,int a){blend_equation=rgb;blend_equation_a=a;if(ctx)apply_state();}
void glColorMask(int r,int g,int b,int a){color_mask=(r?1:0)|(g?2:0)|(b?4:0)|(a?8:0);if(ctx)apply_state();}
void glDepthMask(int on){depth_write=on;if(ctx)rsxSetDepthWriteEnable(ctx,on);}
void glDepthFunc(int f){if(ctx)rsxSetDepthFunc(ctx,f);}
void glFrontFace(int f){if(ctx)rsxSetFrontFace(ctx,f);}
void glCullFace(int f){if(ctx)rsxSetCullFace(ctx,f);}
void glClearColor(float r,float g,float b,float a){clear_color[0]=r;clear_color[1]=g;clear_color[2]=b;clear_color[3]=a;}
void glClearDepthf(float d){clear_depth=d;}
void glClearStencil(int s){clear_stencil=s;}
static unsigned channel(float x){return (unsigned)(fmaxf(0,fminf(1,x))*255+0.5f);}
void glClear(int mask){
    if(!ctx)return;
    rsx_ring_make_room(ctx);
    rsxSetClearColor(ctx,(channel(clear_color[3])<<24)|(channel(clear_color[0])<<16)|(channel(clear_color[1])<<8)|channel(clear_color[2]));
    rsxSetClearDepthStencil(ctx,((u32)(fmaxf(0,fminf(1,clear_depth))*16777215)<<8)|(clear_stencil&255));
    u32 flags=0;if(mask&0x4000)flags|=GCM_CLEAR_R|GCM_CLEAR_G|GCM_CLEAR_B|GCM_CLEAR_A;
    if(mask&0x0100)flags|=GCM_CLEAR_Z;
    if(mask&0x0400)flags|=GCM_CLEAR_S;
    rsxClearSurface(ctx,flags);
}

static int new_object(void *base,size_t stride,int cap){
    for(int i=1;i<cap;i++)if(!*(int*)((char*)base+i*stride)){*(int*)((char*)base+i*stride)=1;return i;}
    set_error(GL_OUT_OF_MEMORY);return 0;
}
/* Texture.live is not the first field, unlike other object tables. */
static int new_texture(void){for(int i=1;i<MAX_OBJECTS;i++)if(!textures[i].live){Texture *t=&textures[i];memset(t,0,sizeof(*t));t->live=1;t->min_filter=t->mag_filter=0x2601;t->wrap_s=t->wrap_t=0x2901;return i;}set_error(GL_OUT_OF_MEMORY);return 0;}
void glGenTextures(int n,void *out){int *ids=out;if(n<0){set_error(GL_INVALID_VALUE);return;}for(int i=0;i<n;i++)ids[i]=new_texture();}
void glDeleteTextures(int n,void *in){int *ids=in;if(n<0){set_error(GL_INVALID_VALUE);return;}fence();for(int i=0;i<n;i++)if(valid_id(ids[i])){Texture *t=&textures[ids[i]];if(t->pixels){rsxFree(t->pixels);texture_bytes-=t->bytes;}memset(t,0,sizeof(*t));texture_changed(t);for(int j=0;j<8;j++)if(bound_texture[j]==ids[i])bound_texture[j]=0;}}
int stsv_gl_gen_texture(void){return new_texture();}
void stsv_gl_delete_texture(int id){glDeleteTextures(1,&id);}
int glIsTexture(int id){return valid_id(id)&&textures[id].live;}
void glActiveTexture(int unit){unit-=0x84c0;if(unit<0||unit>=8){set_error(GL_INVALID_ENUM);return;}active_texture=unit;}
void glBindTexture(int target,int id){if(target!=0x0de1){set_error(GL_INVALID_ENUM);return;}if(id && (!valid_id(id)||!textures[id].live)){set_error(GL_INVALID_VALUE);return;}bound_texture[active_texture]=id;}
void glTexParameteri(int target,int name,int value){
    Texture *t=current_texture();if(target!=0x0de1||!t){set_error(GL_INVALID_OPERATION);return;}
    switch(name){case 0x2800:t->mag_filter=value;break;case 0x2801:t->min_filter=value;break;case 0x2802:t->wrap_s=value;break;case 0x2803:t->wrap_t=value;break;default:set_error(GL_INVALID_ENUM);return;}
    texture_changed(t);
}
void glTexParameterf(int target,int name,float value){glTexParameteri(target,name,(int)value);}
void glTexParameteriv(int t,int n,void *v){glTexParameteri(t,n,*(int*)v);}
void glTexParameterfv(int t,int n,void *v){glTexParameterf(t,n,*(float*)v);}
void glGetTexParameteriv(int target,int name,void *out){int *v=out;Texture *t=current_texture();if(!t){*v=0;set_error(GL_INVALID_OPERATION);return;}switch(name){case 0x2800:*v=t->mag_filter;break;case 0x2801:*v=t->min_filter;break;case 0x2802:*v=t->wrap_s;break;case 0x2803:*v=t->wrap_t;break;default:*v=0;set_error(GL_INVALID_ENUM);}(void)target;}
void glGetTexParameterfv(int t,int n,void *out){int v;glGetTexParameteriv(t,n,&v);*(float*)out=v;}
void glPixelStorei(int name,int value){if((name==0x0cf5||name==0x0d05)&&(value==1||value==2||value==4||value==8)){if(name==0x0cf5)unpack_alignment=value;return;}set_error(GL_INVALID_VALUE);}
static int allocate_texture(Texture *t,int w,int h,int format,u32 byte_count,u32 row_pitch){
    if(w<=0||h<=0||w>4096||h>4096){set_error(GL_INVALID_VALUE);return 0;}
    fence();
    if(t->pixels){rsxFree(t->pixels);texture_bytes-=t->bytes;t->pixels=NULL;t->bytes=0;}
    if(byte_count>TEXTURE_BUDGET-texture_bytes){fprintf(stderr,"[PS3 texture] budget exceeded: %dx%d (%u), %s\n",w,h,byte_count,texture_path);set_error(GL_OUT_OF_MEMORY);return 0;}
    t->pixels=rsxMemalign(128,(byte_count+127)&~127u);
    if(!t->pixels||rsxAddressToOffset(t->pixels,&t->offset)){fprintf(stderr,"[PS3 texture] RSX allocation failed: %dx%d (%u, %u in use), %s\n",w,h,byte_count,texture_bytes,texture_path);if(t->pixels)rsxFree(t->pixels);t->pixels=NULL;set_error(GL_OUT_OF_MEMORY);return 0;}
    t->width=w;t->height=h;t->format=format;t->pitch=row_pitch;t->bytes=byte_count;t->opaque=0;texture_bytes+=byte_count;
    texture_changed(t);
    return 1;
}
static int components(int f){switch(f){case 0x1908:return 4;case 0x1907:return 3;case 0x190a:return 2;case 0x1909:case 0x1906:return 1;default:return 0;}}
static void upload_pixels(Texture *t,int x,int y,int w,int h,int format,const unsigned char *data,int alignment){
    int c=components(format);size_t src_pitch=((size_t)w*c+alignment-1)&~(size_t)(alignment-1);
    for(int row=0;row<h;row++){
        unsigned char *dst=(unsigned char*)t->pixels+(y+row)*t->pitch+x*4;
        const unsigned char *src=data?data+row*src_pitch:NULL;
        for(int col=0;col<w;col++){
            unsigned char r=0,g=0,b=0,a=0;
            if(src){const unsigned char *p=src+col*c;a=255;
                switch(format){case 0x1908:r=p[0];g=p[1];b=p[2];a=p[3];break;case 0x1907:r=p[0];g=p[1];b=p[2];break;
                case 0x1906:r=g=b=255;a=p[0];break;case 0x1909:r=g=b=p[0];break;case 0x190a:r=g=b=p[0];a=p[1];break;}
            }
            dst[col*4]=a;dst[col*4+1]=r;dst[col*4+2]=g;dst[col*4+3]=b;
        }
    }
    texture_changed(t);
}
void glTexImage2D(int target,int level,int internal,int w,int h,int border,int format,int type,void *data){
    (void)internal;
    if(target!=0x0de1||type!=GL_UNSIGNED_BYTE||!components(format)){set_error(GL_INVALID_ENUM);return;}
    if(border||level<0){set_error(GL_INVALID_VALUE);return;}
    /* The menu uses base-level sampling. Higher authored mips do not replace level zero. */
    if(level)return;
    Texture *t=current_texture();if(!t){set_error(GL_INVALID_OPERATION);return;}
    u32 p=(w*4+63)&~63u;
    if(allocate_texture(t,w,h,GCM_TEXTURE_FORMAT_A8R8G8B8,p*h,p)){upload_pixels(t,0,0,w,h,format,data,unpack_alignment);upload_stats++;}
}
void stsv_tex_image_rgba(int target,int level,int w,int h,const void *data){
    int old=unpack_alignment;unpack_alignment=1;glTexImage2D(target,level,0x1908,w,h,0,0x1908,GL_UNSIGNED_BYTE,(void*)data);unpack_alignment=old;
}
void glTexSubImage2D(int target,int level,int x,int y,int w,int h,int format,int type,void *data){
    Texture *t=current_texture();if(target!=0x0de1||!t||t->format!=GCM_TEXTURE_FORMAT_A8R8G8B8||type!=GL_UNSIGNED_BYTE||!components(format)){set_error(GL_INVALID_OPERATION);return;}
    if(level||x<0||y<0||w<0||h<0||x+w>t->width||y+h>t->height){set_error(GL_INVALID_VALUE);return;}
    fence();upload_pixels(t,x,y,w,h,format,data,unpack_alignment);subtex_stats++;
}
void stsv_tex_sub_image_rgba(int target,int level,int x,int y,int w,int h,const void *data){int old=unpack_alignment;unpack_alignment=1;glTexSubImage2D(target,level,x,y,w,h,0x1908,GL_UNSIGNED_BYTE,(void*)data);unpack_alignment=old;}
void glCompressedTexImage2D(int target,int level,int format,int w,int h,int border,int size,void *data){
    unsigned serial=++compressed_upload_count;
    int diagnose=serial<=16||size>=4*1024*1024||serial%50==0;
    if(diagnose){fprintf(stderr,"[PS3 DDS] begin #%u id=%d %dx%d fmt=%04x bytes=%d vram=%uKiB path=%s\n",serial,bound_texture[active_texture],w,h,format,size,(unsigned)(texture_bytes>>10),texture_path);fflush(stderr);}
    if(target!=0x0de1){set_error(GL_INVALID_ENUM);return;}if(level>0)return;
    Texture *t=current_texture();int native,block;
    switch(format){case 0x83f0:case 0x83f1:native=GCM_TEXTURE_FORMAT_DXT1;block=8;break;case 0x83f2:native=GCM_TEXTURE_FORMAT_DXT23;block=16;break;case 0x83f3:native=GCM_TEXTURE_FORMAT_DXT45;block=16;break;default:set_error(GL_INVALID_ENUM);return;}
    if(!t||!data){set_error(GL_INVALID_OPERATION);return;}
    if(level<0||border||w<=0||h<=0||size<((w+3)/4)*((h+3)/4)*block){set_error(GL_INVALID_VALUE);return;}
    /* DXT storage consists of byte-defined blocks; do not byteswap as u32 texels. */
    u32 bytes=((w+3)/4)*((h+3)/4)*block;
    /* Blocks are stored row by row: bind as linear with one block row per pitch. */
    if(allocate_texture(t,w,h,native,bytes,(u32)((w+3)/4)*block)){
        memcpy(t->pixels,data,bytes);upload_stats++;t->opaque=format==0x83f0;texture_changed(t);
        if(diagnose){fprintf(stderr,"[PS3 DDS] done #%u offset=%08x copy=%u vram=%uKiB\n",serial,t->offset,bytes,(unsigned)(texture_bytes>>10));fflush(stderr);}
    }
}
void stsv_tex_image_compressed(int target,int level,int format,int width,int height,int border,int size,const void *data){
    int block=(format==0x83f0||format==0x83f1)?8:16;
    int upload_w=width,upload_h=height,matched=0;
    /* Cooked DDS keeps layout dimensions in its header while reducing the blocks.
     * The texture's actual size may change; normalized SpriteBatch UVs do not. */
    for(int reduction=1;reduction<=4;reduction*=2){
        if(width%reduction||height%reduction)continue;
        int w=width/reduction,h=height/reduction;
        size_t need=(size_t)((w+3)/4)*((h+3)/4)*block;
        if(size>=0&&(size_t)size==need){upload_w=w;upload_h=h;matched=1;break;}
    }
    if(!matched||!data){
        static const unsigned char blank[64]={0};
        fprintf(stderr,"[PS3 texture] empty/truncated DDS %dx%d %d bytes: %s\n",width,height,size,texture_path);
        stsv_tex_image_rgba(target,level,4,4,blank);return;
    }
    glCompressedTexImage2D(target,level,format,upload_w,upload_h,border,size,(void*)data);
}
void glGenerateMipmap(int target){if(target!=0x0de1)set_error(GL_INVALID_ENUM);/* Filters are restricted to level zero in bind_texture. */}
static int texture_wrap(int value){switch(value){case 0x2901:return GCM_TEXTURE_REPEAT;case 0x8370:return GCM_TEXTURE_MIRRORED_REPEAT;default:return GCM_TEXTURE_CLAMP_TO_EDGE;}}
static void bind_texture(Texture *t){
    if(t==bound_rsx_texture&&t->generation==bound_rsx_generation&&!texture_cache_dirty)return;
    gcmTexture d;memset(&d,0,sizeof(d));
    d.format=t->format|GCM_TEXTURE_FORMAT_LIN; /* every upload path stores rows linearly */
    d.mipmap=1;d.dimension=GCM_TEXTURE_DIMS_2D;d.cubemap=GCM_FALSE;d.remap=t->opaque?remap_opaque():remap_identity();
    d.width=t->width;d.height=t->height;d.depth=1;d.location=GCM_LOCATION_RSX;d.pitch=t->pitch;d.offset=t->offset;
    if(texture_cache_dirty){rsxInvalidateTextureCache(ctx,GCM_INVALIDATE_TEXTURE);texture_cache_dirty=0;}
    rsxLoadTexture(ctx,0,&d);
    rsxTextureControl(ctx,0,GCM_TRUE,0,0,GCM_TEXTURE_MAX_ANISO_1);
    int min=(t->min_filter==0x2600||t->min_filter==0x2700||t->min_filter==0x2702)?GCM_TEXTURE_NEAREST:GCM_TEXTURE_LINEAR;
    int mag=t->mag_filter==0x2600?GCM_TEXTURE_NEAREST:GCM_TEXTURE_LINEAR;
    rsxTextureFilter(ctx,0,0,min,mag,GCM_TEXTURE_CONVOLUTION_QUINCUNX);
    rsxTextureWrapMode(ctx,0,texture_wrap(t->wrap_s),texture_wrap(t->wrap_t),GCM_TEXTURE_CLAMP_TO_EDGE,0,GCM_TEXTURE_ZFUNC_LESS,0);
    bound_rsx_texture=t;bound_rsx_generation=t->generation;
}
void stsv_tex_note_path(const void *s,int32_t n){if(n<0)n=0;if(n>191)n=191;memcpy(texture_path,s,n);texture_path[n]=0;}
int32_t stsv_tex_would_fit(int32_t n){return n>=0&&(size_t)n<TEXTURE_BUDGET-texture_bytes;}
void stsv_tex_spill_enable(void){}
void stsv_transition_diag(const void *s,int32_t n){fprintf(stderr,"[PS3 transition] %.*s, texture VRAM %u KiB\n",n,(const char*)s,(unsigned)(texture_bytes>>10));}
int64_t stsv_subtex_frame_stats(void){int64_t n=subtex_stats;subtex_stats=0;return n;}
int64_t stsv_upload_frame_stats(void){int64_t n=upload_stats;upload_stats=0;return n;}
int64_t stsv_dxt_frame_stats(void){return 0;}

void glGenBuffers(int n,void *out){int *ids=out;if(n<0){set_error(GL_INVALID_VALUE);return;}for(int i=0;i<n;i++){int id=0;for(int j=1;j<MAX_OBJECTS;j++)if(!buffers[j].live){id=j;buffers[j].live=1;break;}if(!id)set_error(GL_OUT_OF_MEMORY);ids[i]=id;}}
void glDeleteBuffers(int n,void *in){int *ids=in;if(n<0){set_error(GL_INVALID_VALUE);return;}for(int i=0;i<n;i++)if(valid_id(ids[i])){free(buffers[ids[i]].data);memset(&buffers[ids[i]],0,sizeof(Buffer));if(bound_array==ids[i])bound_array=0;if(bound_elements==ids[i])bound_elements=0;}}
int stsv_gl_gen_buffer(void){int id;glGenBuffers(1,&id);return id;}
void stsv_gl_delete_buffer(int id){glDeleteBuffers(1,&id);}
int glIsBuffer(int id){return valid_id(id)&&buffers[id].live;}
void glBindBuffer(int target,int id){if(id&&(!valid_id(id)||!buffers[id].live)){set_error(GL_INVALID_VALUE);return;}if(target==GL_ARRAY_BUFFER)bound_array=id;else if(target==GL_ELEMENT_ARRAY_BUFFER)bound_elements=id;else set_error(GL_INVALID_ENUM);}
static Buffer *get_buffer(int target){int id=target==GL_ARRAY_BUFFER?bound_array:target==GL_ELEMENT_ARRAY_BUFFER?bound_elements:0;return valid_id(id)?&buffers[id]:NULL;}
void glBufferData(int target,int size,void *data,int usage){Buffer *b=get_buffer(target);if(!b){set_error(GL_INVALID_OPERATION);return;}if(size<0){set_error(GL_INVALID_VALUE);return;}void *p=size?malloc(size):NULL;if(size&&!p){set_error(GL_OUT_OF_MEMORY);return;}free(b->data);b->data=p;b->size=size;b->usage=usage;if(size&&data)memcpy(p,data,size);else if(size)memset(p,0,size);}
void glBufferSubData(int target,int offset,int size,void *data){Buffer *b=get_buffer(target);if(!b){set_error(GL_INVALID_OPERATION);return;}if(offset<0||size<0||(size_t)offset+size>b->size||(!data&&size)){set_error(GL_INVALID_VALUE);return;}if(size)memcpy((char*)b->data+offset,data,size);}
void glGetBufferParameteriv(int target,int name,void *out){Buffer *b=get_buffer(target);if(!b){set_error(GL_INVALID_OPERATION);*(int*)out=0;return;}if(name==0x8764)*(int*)out=b->size;else if(name==0x8765)*(int*)out=b->usage;else{*(int*)out=0;set_error(GL_INVALID_ENUM);}}

void glGenFramebuffers(int n,void *out){int *ids=out;if(n<0){set_error(GL_INVALID_VALUE);return;}for(int i=0;i<n;i++)ids[i]=new_object(framebuffers,sizeof(Framebuffer),MAX_OBJECTS);}
void glDeleteFramebuffers(int n,void *in){int *ids=in;for(int i=0;i<n;i++)if(valid_id(ids[i])){memset(&framebuffers[ids[i]],0,sizeof(Framebuffer));if(bound_framebuffer==ids[i]){bound_framebuffer=0;bind_surface();}}}
int stsv_gl_gen_framebuffer(void){int id;glGenFramebuffers(1,&id);return id;}
void stsv_gl_delete_framebuffer(int id){glDeleteFramebuffers(1,&id);}
int glIsFramebuffer(int id){return valid_id(id)&&framebuffers[id].live;}
void glBindFramebuffer(int target,int id){if(target!=GL_FRAMEBUFFER){set_error(GL_INVALID_ENUM);return;}if(id&&(!valid_id(id)||!framebuffers[id].live)){set_error(GL_INVALID_VALUE);return;}bound_framebuffer=id;if(ctx&&(!id||framebuffers[id].texture))bind_surface();}
void glFramebufferTexture2D(int target,int attachment,int textarget,int id,int level){if(target!=GL_FRAMEBUFFER||attachment!=0x8ce0||textarget!=0x0de1||level||!bound_framebuffer||(id&&!glIsTexture(id))){set_error(GL_INVALID_OPERATION);return;}framebuffers[bound_framebuffer].texture=id;if(id)bind_surface();}
int glCheckFramebufferStatus(int target){
    if(target!=GL_FRAMEBUFFER){set_error(GL_INVALID_ENUM);return 0;}
    if(!bound_framebuffer)return 0x8cd5;
    Framebuffer *f=&framebuffers[bound_framebuffer];int id=f->texture;
    if(!valid_id(id)||!textures[id].pixels)return 0x8cd7;
    Texture *t=&textures[id];
    if(t->format!=GCM_TEXTURE_FORMAT_A8R8G8B8)return 0x8cdd;
    int attachments[]={f->depth_renderbuffer,f->stencil_renderbuffer};
    for(int i=0;i<2;i++)if(attachments[i]){
        Renderbuffer *r=&renderbuffers[attachments[i]];
        if(!r->live||r->width!=t->width||r->height!=t->height)return 0x8cd9;
    }
    if(!ensure_depth(t->width,t->height))return 0x8cdd;
    return 0x8cd5;
}
void glGenRenderbuffers(int n,void *out){int *ids=out;if(n<0){set_error(GL_INVALID_VALUE);return;}for(int i=0;i<n;i++)ids[i]=new_object(renderbuffers,sizeof(Renderbuffer),MAX_OBJECTS);}
void glDeleteRenderbuffers(int n,void *in){int *ids=in;for(int i=0;i<n;i++)if(valid_id(ids[i])){memset(&renderbuffers[ids[i]],0,sizeof(Renderbuffer));if(bound_renderbuffer==ids[i])bound_renderbuffer=0;}}
int stsv_gl_gen_renderbuffer(void){int id;glGenRenderbuffers(1,&id);return id;}
void stsv_gl_delete_renderbuffer(int id){glDeleteRenderbuffers(1,&id);}
int glIsRenderbuffer(int id){return valid_id(id)&&renderbuffers[id].live;}
void glBindRenderbuffer(int target,int id){if(target!=0x8d41){set_error(GL_INVALID_ENUM);return;}if(id&&(!valid_id(id)||!renderbuffers[id].live)){set_error(GL_INVALID_VALUE);return;}bound_renderbuffer=id;}
void glRenderbufferStorage(int target,int format,int w,int h){
    if(target!=0x8d41||!bound_renderbuffer){set_error(GL_INVALID_OPERATION);return;}
    if(w<0||h<0||w>4096||h>4096){set_error(GL_INVALID_VALUE);return;}
    if(format!=0x81a5&&format!=0x81a6&&format!=0x8d48&&format!=0x88f0){set_error(GL_INVALID_ENUM);return;}
    Renderbuffer *r=&renderbuffers[bound_renderbuffer];r->width=w;r->height=h;r->format=format;
}
void glFramebufferRenderbuffer(int target,int attachment,int rbtarget,int id){
    if(target!=GL_FRAMEBUFFER||!bound_framebuffer||rbtarget!=0x8d41||(id&&!glIsRenderbuffer(id))){set_error(GL_INVALID_OPERATION);return;}
    Framebuffer *f=&framebuffers[bound_framebuffer];
    if(attachment==0x8d00)f->depth_renderbuffer=id;
    else if(attachment==0x8d20)f->stencil_renderbuffer=id;
    else if(attachment==0x821a)f->depth_renderbuffer=f->stencil_renderbuffer=id;
    else set_error(GL_INVALID_ENUM);
}

int glCreateShader(int type){if(type!=0x8b31&&type!=0x8b30){set_error(GL_INVALID_ENUM);return 0;}for(int i=1;i<MAX_SHADERS;i++)if(!shaders[i].live){shaders[i].live=1;shaders[i].type=type;return i;}set_error(GL_OUT_OF_MEMORY);return 0;}
void stsv_glShaderSource(int id,const void *source){if(id<=0||id>=MAX_SHADERS||!shaders[id].live){set_error(GL_INVALID_VALUE);return;}free(shaders[id].source);shaders[id].source=source?strdup(source):NULL;}
void glCompileShader(int id){if(id<=0||id>=MAX_SHADERS||!shaders[id].live){set_error(GL_INVALID_VALUE);return;}shaders[id].compiled=shaders[id].source!=NULL;}
void glGetShaderiv(int id,int name,void *out){int *p=out;*p=0;if(id<=0||id>=MAX_SHADERS){set_error(GL_INVALID_VALUE);return;}switch(name){case 0x8b81:*p=shaders[id].compiled;break;case 0x8b80:*p=!shaders[id].live;break;case 0x8b4f:*p=shaders[id].type;break;case 0x8b84:*p=1;break;case 0x8b88:*p=shaders[id].source?strlen(shaders[id].source)+1:0;break;default:set_error(GL_INVALID_ENUM);}}
void glGetShaderInfoLog(int id,int cap,void *length,void *log){(void)id;if(length)*(int*)length=0;if(cap>0&&log)*(char*)log=0;}
void glDeleteShader(int id){if(id>0&&id<MAX_SHADERS){free(shaders[id].source);memset(&shaders[id],0,sizeof(Shader));}}
int glIsShader(int id){return id>0&&id<MAX_SHADERS&&shaders[id].live;}
int glCreateProgram(void){for(int i=1;i<MAX_PROGRAMS;i++)if(!programs[i].live){memset(&programs[i],0,sizeof(Program));programs[i].live=1;return i;}set_error(GL_OUT_OF_MEMORY);return 0;}
void glAttachShader(int pid,int sid){if(pid<=0||pid>=MAX_PROGRAMS||sid<=0||sid>=MAX_SHADERS){set_error(GL_INVALID_VALUE);return;}if(shaders[sid].type==0x8b31)programs[pid].vertex=sid;else if(shaders[sid].type==0x8b30)programs[pid].fragment=sid;else set_error(GL_INVALID_ENUM);}
void glDetachShader(int p,int s){if(p>0&&p<MAX_PROGRAMS){if(programs[p].vertex==s)programs[p].vertex=0;if(programs[p].fragment==s)programs[p].fragment=0;}}
static int variable_type(const char *s){if(!strcmp(s,"float"))return GL_FLOAT;if(!strcmp(s,"vec2"))return 0x8b50;if(!strcmp(s,"vec3"))return 0x8b51;if(!strcmp(s,"vec4"))return 0x8b52;if(!strcmp(s,"mat2"))return 0x8b5a;if(!strcmp(s,"mat3"))return 0x8b5b;if(!strcmp(s,"mat4"))return 0x8b5c;if(!strcmp(s,"sampler2D"))return 0x8b5e;if(!strcmp(s,"int"))return 0x1404;if(!strcmp(s,"bool"))return 0x8b56;return GL_FLOAT;}
static const char *token(const char *s,char *out,int cap){
    for(;;){while(*s&&isspace((unsigned char)*s))s++;if(s[0]=='/'&&s[1]=='/'){while(*s&&*s!='\n')s++;continue;}if(s[0]=='/'&&s[1]=='*'){s+=2;while(*s&&!(s[0]=='*'&&s[1]=='/'))s++;if(*s)s+=2;continue;}break;}
    int i=0;if(isalpha((unsigned char)*s)||*s=='_'){while(isalnum((unsigned char)*s)||*s=='_'){if(i<cap-1)out[i++]=*s;s++;}}
    else if(*s){out[i++]=*s++;}
    out[i]=0;return s;
}
static void add_declarations(Program *p,const char *source){
    if(!source)return;
    char t[64],type[64],name[64];const char *s=source;
    while(*s){s=token(s,t,sizeof(t));int attr=!strcmp(t,"attribute"),uniform=!strcmp(t,"uniform");if(!attr&&!uniform)continue;
        s=token(s,type,sizeof(type));if(!strcmp(type,"lowp")||!strcmp(type,"mediump")||!strcmp(type,"highp"))s=token(s,type,sizeof(type));
        s=token(s,name,sizeof(name));if(!name[0])break;
        Variable *vars=attr?p->attrs:p->uniforms;int *count=attr?&p->attr_count:&p->uniform_count;int exists=0;
        for(int i=0;i<*count;i++)if(!strcmp(vars[i].name,name))exists=1;
        if(exists||*count>=MAX_VARIABLES)continue;
        Variable *v=&vars[(*count)++];memset(v,0,sizeof(*v));memcpy(v->name,name,strlen(name)+1);v->type=variable_type(type);v->size=1;
        if(v->type==0x8b5c){v->value[0]=v->value[5]=v->value[10]=v->value[15]=1;}
        while(*s&&isspace((unsigned char)*s))s++;
        if(*s=='['){s++;int n=atoi(s);if(n>0)v->size=n;}
    }
}
void glLinkProgram(int id){
    if(id<=0||id>=MAX_PROGRAMS){set_error(GL_INVALID_VALUE);return;}
    Program *p=&programs[id];Shader *v=&shaders[p->vertex],*f=&shaders[p->fragment];
    p->linked=v->compiled&&f->compiled;p->attr_count=p->uniform_count=0;p->slots_ready=0;
    if(!p->linked)return;
    add_declarations(p,v->source);add_declarations(p,f->source);
    p->textured=strstr(f->source,"texture2D")!=NULL;
    p->alpha_correction=strstr(v->source,"255.0")&&strstr(v->source,"254.0");
    if(p->textured&&(strstr(f->source,"sin(")||strstr(f->source,"u_texture2")||strstr(f->source,"pow(")))
        fprintf(stderr,"[PS3 shader] program %d uses offline sprite approximation for custom fragment effects\n",id);
    printf("[PS3 shader] linked %d: %d attributes, %d uniforms, %s\n",id,p->attr_count,p->uniform_count,p->textured?"sprite":"solid");
}
void glGetProgramiv(int id,int name,void *out){int *p=out;*p=0;if(id<=0||id>=MAX_PROGRAMS){set_error(GL_INVALID_VALUE);return;}Program *pr=&programs[id];switch(name){case 0x8b82:case 0x8b83:*p=pr->linked;break;case 0x8b84:*p=1;break;case 0x8b85:*p=(pr->vertex?1:0)+(pr->fragment?1:0);break;case 0x8b89:*p=pr->attr_count;break;case 0x8b86:*p=pr->uniform_count;break;case 0x8b87:case 0x8b8a:*p=64;break;case 0x8b80:*p=!pr->live;break;default:set_error(GL_INVALID_ENUM);}}
void glGetProgramInfoLog(int id,int cap,void *length,void *log){(void)id;if(length)*(int*)length=0;if(cap>0&&log)*(char*)log=0;}
static void active_variable(int id,int index,int cap,void *length,void *size,void *type,void *name,int attr){
    if(length)*(int*)length=0;
    if(size)*(int*)size=0;
    if(type)*(int*)type=0;
    if(cap>0&&name)*(char*)name=0;
    if(id<=0||id>=MAX_PROGRAMS){set_error(GL_INVALID_VALUE);return;}Program *p=&programs[id];int count=attr?p->attr_count:p->uniform_count;
    if(index<0||index>=count){set_error(GL_INVALID_VALUE);return;}Variable *v=(attr?p->attrs:p->uniforms)+index;
    if(size)*(int*)size=v->size;
    if(type)*(int*)type=v->type;
    if(cap>0&&name){int n=strlen(v->name);if(n>cap-1)n=cap-1;memcpy(name,v->name,n);((char*)name)[n]=0;if(length)*(int*)length=n;}
}
void glGetActiveAttrib(int p,int i,int cap,void *len,void *size,void *type,void *name){active_variable(p,i,cap,len,size,type,name,1);}
void glGetActiveUniform(int p,int i,int cap,void *len,void *size,void *type,void *name){active_variable(p,i,cap,len,size,type,name,0);}
int glGetAttribLocation(int p,void *name){if(p<=0||p>=MAX_PROGRAMS||!name)return -1;for(int i=0;i<programs[p].attr_count;i++)if(!strcmp(programs[p].attrs[i].name,name))return i;return -1;}
int glGetUniformLocation(int p,void *name){if(p<=0||p>=MAX_PROGRAMS||!name)return -1;for(int i=0;i<programs[p].uniform_count;i++)if(!strcmp(programs[p].uniforms[i].name,name))return i;return -1;}
void glBindAttribLocation(int p,int index,void *name){(void)p;(void)index;(void)name;/* libGDX obtains reflected locations after link. */}
void glUseProgram(int id){if(id&&(id<0||id>=MAX_PROGRAMS||!programs[id].linked)){set_error(GL_INVALID_OPERATION);return;}current_program=id;}
int glIsProgram(int id){return id>0&&id<MAX_PROGRAMS&&programs[id].live;}
void glDeleteProgram(int id){if(id>0&&id<MAX_PROGRAMS){memset(&programs[id],0,sizeof(Program));if(current_program==id)current_program=0;}}
void glValidateProgram(int id){if(!glIsProgram(id))set_error(GL_INVALID_VALUE);}
void glReleaseShaderCompiler(void){}
static void set_uniform(int loc,int count,const float *data){Program *p=program();if(loc==-1)return;if(!p||loc<0||loc>=p->uniform_count){set_error(GL_INVALID_OPERATION);return;}if(count>16)count=16;if(count>0&&data)memcpy(p->uniforms[loc].value,data,count*sizeof(float));}
void glUniform1f(int l,float x){set_uniform(l,1,&x);}
void glUniform2f(int l,float x,float y){float v[]={x,y};set_uniform(l,2,v);}
void glUniform3f(int l,float x,float y,float z){float v[]={x,y,z};set_uniform(l,3,v);}
void glUniform4f(int l,float x,float y,float z,float w){float v[]={x,y,z,w};set_uniform(l,4,v);}
void glUniform1i(int l,int x){float v=x;set_uniform(l,1,&v);}
void glUniform2i(int l,int x,int y){glUniform2f(l,x,y);}
void glUniform3i(int l,int x,int y,int z){glUniform3f(l,x,y,z);}
void glUniform4i(int l,int x,int y,int z,int w){glUniform4f(l,x,y,z,w);}
void glUniform1fv(int l,int count,void *v){set_uniform(l,count,v);}
void glUniform2fv(int l,int count,void *v){set_uniform(l,count*2,v);}
void glUniform3fv(int l,int count,void *v){set_uniform(l,count*3,v);}
void glUniform4fv(int l,int count,void *v){set_uniform(l,count*4,v);}
static void uniform_ints(int l,int count,const int *v){float f[16];if(count>16)count=16;for(int i=0;i<count;i++)f[i]=v[i];set_uniform(l,count,f);}
void glUniform1iv(int l,int count,void *v){uniform_ints(l,count,v);}
void glUniform2iv(int l,int count,void *v){uniform_ints(l,count*2,v);}
void glUniform3iv(int l,int count,void *v){uniform_ints(l,count*3,v);}
void glUniform4iv(int l,int count,void *v){uniform_ints(l,count*4,v);}
void glUniformMatrix2fv(int l,int count,int transpose,void *v){if(transpose){set_error(GL_INVALID_VALUE);return;}set_uniform(l,count*4,v);}
void glUniformMatrix3fv(int l,int count,int transpose,void *v){if(transpose){set_error(GL_INVALID_VALUE);return;}set_uniform(l,count*9,v);}
void glUniformMatrix4fv(int l,int count,int transpose,void *v){if(transpose){set_error(GL_INVALID_VALUE);return;}set_uniform(l,count*16,v);}
void glGetUniformfv(int id,int loc,void *out){if(id<=0||id>=MAX_PROGRAMS||loc<0||loc>=programs[id].uniform_count){set_error(GL_INVALID_VALUE);return;}Variable *v=&programs[id].uniforms[loc];int count=v->type==0x8b5c?16:v->type==0x8b5b?9:v->type==0x8b5a?4:v->type==0x8b52?4:v->type==0x8b51?3:v->type==0x8b50?2:1;memcpy(out,v->value,count*sizeof(float));}
void glGetUniformiv(int id,int loc,void *out){if(id<=0||id>=MAX_PROGRAMS||loc<0||loc>=programs[id].uniform_count){set_error(GL_INVALID_VALUE);return;}*(int*)out=(int)programs[id].uniforms[loc].value[0];}

void glEnableVertexAttribArray(int id){if(id<0||id>=16){set_error(GL_INVALID_VALUE);return;}attributes[id].enabled=1;}
void glDisableVertexAttribArray(int id){if(id<0||id>=16){set_error(GL_INVALID_VALUE);return;}attributes[id].enabled=0;}
static int set_attribute(int id,int size,int type,int normalized,int stride,const void *pointer){
    if(id<0||id>=16||size<1||size>4||stride<0){set_error(GL_INVALID_VALUE);return 0;}
    Attribute *a=&attributes[id];a->size=size;a->type=type;a->normalized=normalized;a->stride=stride;a->pointer=pointer;a->buffer=bound_array;
    return 1;
}
void glVertexAttribPointer(int id,int size,int type,int normalized,int stride,void *pointer){
    if(!set_attribute(id,size,type,normalized,stride,pointer))return;
    Attribute *a=&attributes[id];free(a->snapshot);a->snapshot=NULL;a->snapshot_size=a->snapshot_capacity=0;
}
void stsv_glVertexAttribPointerSnapshot(int id,int size,int type,int normalized,int stride,const void *pointer,int available_bytes){
    if(bound_array){glVertexAttribPointer(id,size,type,normalized,stride,(void*)pointer);return;}
    if(id<0||id>=16||available_bytes<0||(!pointer&&available_bytes)){set_error(GL_INVALID_VALUE);return;}
    Attribute *a=&attributes[id];
    if((size_t)available_bytes>a->snapshot_capacity){
        void *copy=realloc(a->snapshot,available_bytes);
        if(!copy){set_error(GL_OUT_OF_MEMORY);return;}
        a->snapshot=copy;a->snapshot_capacity=available_bytes;
    }
    if(available_bytes)memcpy(a->snapshot,pointer,available_bytes);
    a->snapshot_size=available_bytes;
    set_attribute(id,size,type,normalized,stride,a->snapshot);
}
void glVertexAttrib1f(int id,float x){if(id<0||id>=16){set_error(GL_INVALID_VALUE);return;}float *v=attributes[id].value;v[0]=x;v[1]=v[2]=0;v[3]=1;}
void glVertexAttrib2f(int id,float x,float y){glVertexAttrib1f(id,x);if(id>=0&&id<16)attributes[id].value[1]=y;}
void glVertexAttrib3f(int id,float x,float y,float z){glVertexAttrib2f(id,x,y);if(id>=0&&id<16)attributes[id].value[2]=z;}
void glVertexAttrib4f(int id,float x,float y,float z,float w){glVertexAttrib3f(id,x,y,z);if(id>=0&&id<16)attributes[id].value[3]=w;}
void glVertexAttrib1fv(int id,void *p){float *v=p;glVertexAttrib1f(id,v[0]);}
void glVertexAttrib2fv(int id,void *p){float *v=p;glVertexAttrib2f(id,v[0],v[1]);}
void glVertexAttrib3fv(int id,void *p){float *v=p;glVertexAttrib3f(id,v[0],v[1],v[2]);}
void glVertexAttrib4fv(int id,void *p){float *v=p;glVertexAttrib4f(id,v[0],v[1],v[2],v[3]);}
static int type_bytes(int type){switch(type){case GL_FLOAT:case GL_UNSIGNED_INT:case 0x1404:return 4;case GL_UNSIGNED_SHORT:case 0x1402:return 2;case GL_UNSIGNED_BYTE:case 0x1400:return 1;default:return 0;}}
static void read_attribute(int loc,unsigned vertex,float *out,int packed_color){
    out[0]=out[1]=out[2]=0;out[3]=1;
    if(loc<0||loc>=16)return;
    Attribute *a=&attributes[loc];
    if(!a->enabled){memcpy(out,a->value,4*sizeof(float));return;}
    int bytes=type_bytes(a->type),stride=a->stride?a->stride:bytes*a->size;
    if(!bytes){set_error(GL_INVALID_ENUM);return;}
    const unsigned char *src;
    if(a->buffer){Buffer *b=&buffers[a->buffer];size_t offset=(uintptr_t)a->pointer+(size_t)vertex*stride;if(offset+(size_t)bytes*a->size>b->size){set_error(GL_INVALID_OPERATION);return;}src=(const unsigned char*)b->data+offset;}
    else {
        size_t offset=(size_t)vertex*stride;
        if(!a->pointer||(a->snapshot&&offset+(size_t)bytes*a->size>a->snapshot_size)){set_error(GL_INVALID_OPERATION);return;}
        src=(const unsigned char*)a->pointer+offset;
    }
    /* libGDX packs ABGR into Float.floatToRawIntBits, independent of CPU endianness.
     * On the big-endian PPE its four bytes are A B G R, unlike the GLES RGBA layout. */
    if(packed_color&&a->type==GL_UNSIGNED_BYTE&&a->size==4){u32 abgr;memcpy(&abgr,src,4);for(int i=0;i<4;i++)out[i]=((abgr>>(8*i))&255)/255.0f;return;}
    for(int i=0;i<a->size;i++)switch(a->type){
        case GL_FLOAT:memcpy(out+i,src+i*4,4);break;
        case GL_UNSIGNED_BYTE:out[i]=a->normalized?src[i]/255.0f:src[i];break;
        case 0x1400:out[i]=a->normalized?fmaxf(-1,((const signed char*)src)[i]/127.0f):((const signed char*)src)[i];break;
        case GL_UNSIGNED_SHORT:{unsigned short n;memcpy(&n,src+i*2,2);out[i]=a->normalized?n/65535.0f:n;break;}
        case 0x1402:{short n;memcpy(&n,src+i*2,2);out[i]=a->normalized?fmaxf(-1,n/32767.0f):n;break;}
        default:set_error(GL_INVALID_ENUM);break;
    }
}
static Variable *find_uniform(Program *p,const char *name){for(int i=0;i<p->uniform_count;i++)if(!strcmp(p->uniforms[i].name,name))return &p->uniforms[i];return NULL;}
static void transform(const float *m,const float *v,float *out){for(int i=0;i<4;i++)out[i]=m[i]*v[0]+m[4+i]*v[1]+m[8+i]*v[2]+m[12+i]*v[3];}
/* Attribute and uniform locations never change after link; look them up once. */
static void resolve_slots(Program *p){
    p->pos=p->col=p->uv=-1;
    for(int i=0;i<p->attr_count;i++){if(!strcmp(p->attrs[i].name,"a_position"))p->pos=i;else if(!strcmp(p->attrs[i].name,"a_color"))p->col=i;else if(!strcmp(p->attrs[i].name,"a_texCoord0"))p->uv=i;}
    p->matrix=find_uniform(p,"u_projTrans");if(!p->matrix)p->matrix=find_uniform(p,"u_projModelView");if(!p->matrix)p->matrix=find_uniform(p,"u_projectionViewMatrix");
    p->color=find_uniform(p,"u_color");p->sampler=find_uniform(p,"u_texture");
    p->slots_ready=1;
}
typedef struct { const unsigned char *base; size_t stride,limit; } AttributeView;
/* A direct view of an attribute with the exact type and size, or 0 for the generic path. */
static int view_attribute(int loc,int type,int size,AttributeView *view){
    if(loc<0||loc>=16)return 0;
    Attribute *a=&attributes[loc];
    if(!a->enabled||a->type!=type||a->size!=size)return 0;
    size_t bytes=(size_t)type_bytes(type)*size;
    view->stride=a->stride?(size_t)a->stride:bytes;
    if(a->buffer){
        Buffer *b=&buffers[a->buffer];size_t offset=(uintptr_t)a->pointer;
        if(offset+bytes>b->size)return 0;
        view->base=(const unsigned char*)b->data+offset;view->limit=(b->size-offset-bytes)/view->stride+1;return 1;
    }
    if(!a->pointer)return 0;
    view->base=a->pointer;
    view->limit=a->snapshot?(a->snapshot_size>=bytes?(a->snapshot_size-bytes)/view->stride+1:0):(size_t)-1;
    return 1;
}
/* SpriteBatch/PolygonSpriteBatch layout: float2 position, packed ABGR colour, float2 UV.
 * Returns how many vertices were written; the generic loop finishes the rest. */
static int fill_sprite_vertices(Program *p,const Variable *matrix,const Variable *color,int first,int count,int index_type,const unsigned char *index_data,Vertex *out){
    AttributeView pos,col,uv;
    if(!matrix||!view_attribute(p->pos,GL_FLOAT,2,&pos)||!view_attribute(p->col,GL_UNSIGNED_BYTE,4,&col)||!view_attribute(p->uv,GL_FLOAT,2,&uv))return 0;
    const float *m=matrix->value;
    float tint[4];for(int j=0;j<4;j++)tint[j]=(color?color->value[j]:1)/255.0f;
    if(p->alpha_correction)tint[3]*=255.0f/254.0f;
    size_t limit=pos.limit<col.limit?pos.limit:col.limit;if(uv.limit<limit)limit=uv.limit;
    for(int i=0;i<count;i++){
        unsigned ix=first+i;
        if(index_type==GL_UNSIGNED_SHORT){unsigned short v;memcpy(&v,index_data+i*2,2);ix=v;}
        else if(index_type==GL_UNSIGNED_BYTE)ix=index_data[i];
        else if(index_type)memcpy(&ix,index_data+i*4,4);
        if(ix>=limit)return i;
        float xy[2],st[2];u32 abgr;
        memcpy(xy,pos.base+ix*pos.stride,8);memcpy(&abgr,col.base+ix*col.stride,4);memcpy(st,uv.base+ix*uv.stride,8);
        for(int r=0;r<4;r++)out[i].pos[r]=m[r]*xy[0]+m[4+r]*xy[1]+m[12+r];
        for(int j=0;j<4;j++)out[i].color[j]=((abgr>>(8*j))&255)*tint[j];
        out[i].uv[0]=st[0];out[i].uv[1]=st[1];
    }
    return count;
}
static void draw_vertices(int mode,int first,int count,int index_type,const void *indices);
static void draw(int mode,int first,int count,int index_type,const void *indices){
    int64_t start=stsv_ps3_monotonic_us();
    draw_vertices(mode,first,count,index_type,indices);
    draw_us+=stsv_ps3_monotonic_us()-start;
}
static void draw_vertices(int mode,int first,int count,int index_type,const void *indices){
    if(!ctx||count==0)return;
    Program *p=program();
    if(!p||!p->linked||count<0||first<0){set_error(GL_INVALID_OPERATION);return;}
    if(++draw_count<=3){fprintf(stderr,"[PS3 draw] #%u program=%d mode=%d vertices=%d client=%d\n",draw_count,current_program,mode,count,bound_array==0);fflush(stderr);}
    if(mode<0||mode>6){set_error(GL_INVALID_ENUM);return;}
    if(count>STREAM_HALF){set_error(GL_OUT_OF_MEMORY);return;}
    if(stream_cursor+(unsigned)count>STREAM_HALF){fence();stream_cursor=0;}
    rsx_ring_make_room(ctx);
    if(!p->slots_ready)resolve_slots(p);
    int pos=p->pos,col=p->col,uv=p->uv;
    if(pos<0){set_error(GL_INVALID_OPERATION);return;}
    Variable *matrix=p->matrix,*color=p->color,*sampler=p->sampler;
    int texunit=sampler?(int)sampler->value[0]:0;Texture *t=NULL;
    if(p->textured&&texunit>=0&&texunit<8&&valid_id(bound_texture[texunit]))t=&textures[bound_texture[texunit]];
    if(!t||!t->pixels)t=&white;
    bind_texture(t);
    const unsigned char *index_data=indices;
    int ib=type_bytes(index_type);
    if(index_type&&index_type!=GL_UNSIGNED_BYTE&&index_type!=GL_UNSIGNED_SHORT&&index_type!=GL_UNSIGNED_INT){set_error(GL_INVALID_ENUM);return;}
    if(index_type&&bound_elements){Buffer *b=&buffers[bound_elements];size_t offset=(uintptr_t)indices;if(offset+(size_t)count*ib>b->size){set_error(GL_INVALID_OPERATION);return;}index_data=(const unsigned char*)b->data+offset;}
    u32 first_vertex=stream_half*STREAM_HALF+stream_cursor;
    Vertex *out=stream+first_vertex;
    int done=fill_sprite_vertices(p,matrix,color,first,count,index_type,index_data,out);
    for(int i=done;i<count;i++){
        unsigned ix=first+i;
        if(index_type){if(index_type==GL_UNSIGNED_BYTE)ix=index_data[i];else if(index_type==GL_UNSIGNED_SHORT){unsigned short v;memcpy(&v,index_data+i*2,2);ix=v;}else memcpy(&ix,index_data+i*4,4);}
        float v[4],c[4],tc[4];read_attribute(pos,ix,v,0);
        if(matrix)transform(matrix->value,v,out[i].pos);else memcpy(out[i].pos,v,sizeof(v));
        if(col>=0)read_attribute(col,ix,c,1);else c[0]=c[1]=c[2]=c[3]=1;
        if(p->alpha_correction)c[3]*=255.0f/254.0f;
        for(int j=0;j<4;j++)out[i].color[j]=c[j]*(color?color->value[j]:1);
        read_attribute(uv,ix,tc,0);out[i].uv[0]=tc[0];out[i].uv[1]=tc[1];
    }
    u32 base=stream_offset+first_vertex*sizeof(Vertex);
    if(!programs_bound){rsxLoadVertexProgram(ctx,vp,vp_ucode);rsxLoadFragmentProgramLocation(ctx,fp,fragment_offset,GCM_LOCATION_RSX);programs_bound=1;}
    rsxBindVertexArrayAttrib(ctx,GCM_VERTEX_ATTRIB_POS,0,base+offsetof(Vertex,pos),sizeof(Vertex),4,GCM_VERTEX_DATA_TYPE_F32,GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx,GCM_VERTEX_ATTRIB_COLOR0,0,base+offsetof(Vertex,color),sizeof(Vertex),4,GCM_VERTEX_DATA_TYPE_F32,GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx,GCM_VERTEX_ATTRIB_TEX0,0,base+offsetof(Vertex,uv),sizeof(Vertex),2,GCM_VERTEX_DATA_TYPE_F32,GCM_LOCATION_RSX);
    rsxInvalidateVertexCache(ctx);
    rsxDrawVertexArray(ctx,mode+1,0,count);stream_cursor+=count;
    frame_draws++;frame_vertices+=count;
    /* Rendering into a framebuffer writes texture memory the GPU may sample later. */
    if(bound_framebuffer)texture_cache_dirty=1;
}
void glDrawArrays(int mode,int first,int count){draw(mode,first,count,0,NULL);}
void glDrawElements(int mode,int count,int type,void *indices){draw(mode,0,count,type,indices);}
/* State accepted by libGDX startup but irrelevant to the 2D menu. */
void glHint(int target,int mode){(void)target;(void)mode;}
void glLineWidth(float value){if(value<=0)set_error(GL_INVALID_VALUE);else if(value!=1)stsv_gl_unsupported("glLineWidth(non-unit)");}
void glDepthRangef(float near,float far){depth_near=fmaxf(0,fminf(1,near));depth_far=fmaxf(0,fminf(1,far));if(ctx)apply_view();}
void glPolygonOffset(float factor,float units){if(ctx)rsxSetPolygonOffset(ctx,factor,units);}
void glGetShaderPrecisionFormat(int type,int precision,void *range,void *out){(void)type;(void)precision;((int*)range)[0]=127;((int*)range)[1]=127;*(int*)out=23;}


/* Copy the GPU framebuffer into mapped XDR before reading it on the PPE. */
int stsv_gfx_dump_frame(const char *path) {
    if(!ctx)return -1;
    u32 size=(pitch*height+0xfffff)&~0xfffffu,offset;
    unsigned char *pixels=memalign(0x100000,size);
    if(!pixels)return -1;
    if(gcmMapMainMemory(pixels,size,&offset)){free(pixels);return -1;}
    rsx_ring_make_room(ctx);
    rsxSetTransferData(ctx,GCM_TRANSFER_LOCAL_TO_MAIN,offset,pitch,back_offset[frame_index],pitch,width*4,height);
    fence();
    FILE *file=fopen(path,"wb");
    unsigned char *row=malloc(width*3);
    int result=-1;
    if(file&&row){
        fprintf(file,"P6\n%u %u\n255\n",width,height);
        result=0;
        for(u32 y=0;y<height;y++){
            for(u32 x=0;x<width;x++){
                const unsigned char *p=pixels+y*pitch+x*4;
                row[x*3]=p[1];row[x*3+1]=p[2];row[x*3+2]=p[3];
            }
            if(fwrite(row,3,width,file)!=width){result=-1;break;}
        }
        fprintf(stderr,"[PS3 capture] %s (%ux%u): %s\n",path,width,height,result?"failed":"saved");
    }
    free(row);if(file)fclose(file);
    gcmUnmapEaIoAddress(pixels);free(pixels);
    return result;
}
