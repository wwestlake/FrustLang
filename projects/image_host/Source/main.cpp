// image_host/Source/main.cpp
// C++ host for Frust procedural image generation demo.
// Provides all extern "C" host functions, runs every effect, saves PNGs.

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

// ============================================================
// SIMPLEX NOISE — Stefan Gustavson (public domain)
// ============================================================
static const int PERM[256] = {
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,
    69,142,8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,
    252,219,203,117,35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,
    168,68,175,74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,
    211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,
    216,80,73,209,76,132,187,208,89,18,169,200,196,135,130,116,188,159,86,
    164,100,109,198,173,186,3,64,52,217,226,250,124,123,5,202,38,147,118,
    126,255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,
    213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,129,22,39,
    253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,228,251,34,
    242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,107,49,
    192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
    138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
};

static int p512[512], p12[512];

static void noise_init() {
    for (int i = 0; i < 512; i++) { p512[i] = PERM[i&255]; p12[i] = p512[i] % 12; }
}

static const float G2[12][2] = {
    {1,1},{-1,1},{1,-1},{-1,-1},{1,0},{-1,0},{1,0},{-1,0},{0,1},{0,-1},{0,1},{0,-1}
};

static float dot2(const float g[2], float x, float y) { return g[0]*x + g[1]*y; }

static float simplex2(float xin, float yin) {
    const float F2 = 0.5f*(sqrtf(3.f)-1.f), G2v = (3.f-sqrtf(3.f))/6.f;
    float s = (xin+yin)*F2;
    int i = (int)floorf(xin+s), j = (int)floorf(yin+s);
    float t = (i+j)*G2v;
    float x0 = xin-(i-t), y0 = yin-(j-t);
    int i1 = x0>y0?1:0, j1 = x0>y0?0:1;
    float x1=x0-i1+G2v, y1=y0-j1+G2v, x2=x0-1.f+2.f*G2v, y2=y0-1.f+2.f*G2v;
    int ii=i&255, jj=j&255;
    int gi0=p12[ii+p512[jj]], gi1=p12[ii+i1+p512[jj+j1]], gi2=p12[ii+1+p512[jj+1]];
    float n0=0,n1=0,n2=0;
    float t0=.5f-x0*x0-y0*y0; if(t0>=0){t0*=t0; n0=t0*t0*dot2(G2[gi0],x0,y0);}
    float t1=.5f-x1*x1-y1*y1; if(t1>=0){t1*=t1; n1=t1*t1*dot2(G2[gi1],x1,y1);}
    float t2=.5f-x2*x2-y2*y2; if(t2>=0){t2*=t2; n2=t2*t2*dot2(G2[gi2],x2,y2);}
    return 70.f*(n0+n1+n2);
}

// ============================================================
// WORLEY NOISE
// ============================================================
static uint32_t whash(int cx, int cy) {
    uint32_t h = (uint32_t)(cx*1664525u ^ cy*1013904223u);
    h^=h>>16; h*=0x45d9f3bu; h^=h>>16; return h;
}

static float worley2(float x, float y) {
    int xi=(int)floorf(x), yi=(int)floorf(y);
    float mn=1e10f;
    for(int dx=-1;dx<=1;dx++) for(int dy=-1;dy<=1;dy++){
        int cx=xi+dx,cy=yi+dy;
        uint32_t h=whash(cx,cy);
        float fx=cx+(float)(h&0xFFFF)/65535.f, fy=cy+(float)((h>>16)&0xFFFF)/65535.f;
        float d=sqrtf((x-fx)*(x-fx)+(y-fy)*(y-fy));
        if(d<mn) mn=d;
    }
    return mn;
}

static float worley_hash(float x, float y) {
    int xi=(int)floorf(x), yi=(int)floorf(y);
    float mn=1e10f; int bx=0,by=0;
    for(int dx=-1;dx<=1;dx++) for(int dy=-1;dy<=1;dy++){
        int cx=xi+dx,cy=yi+dy;
        uint32_t h=whash(cx,cy);
        float fx=cx+(float)(h&0xFFFF)/65535.f, fy=cy+(float)((h>>16)&0xFFFF)/65535.f;
        float d=sqrtf((x-fx)*(x-fx)+(y-fy)*(y-fy));
        if(d<mn){mn=d; bx=cx; by=cy;}
    }
    uint32_t h2=whash(bx*2654435769u, by*1234567891u);
    h2^=h2>>17; h2*=0x45d9f3bu; h2^=h2>>16;
    return (float)(h2&0xFFFFFF)/(float)0xFFFFFF;
}

// ============================================================
// SIMULATION EFFECTS
// ============================================================
static void run_reaction_diffusion(float* buf, int w, int h) {
    int n=w*h;
    std::vector<float> A(n,1.f), B(n,0.f), nA(n), nB(n);
    int cx=w/2,cy=h/2,sr=w/15;
    for(int y=cy-sr;y<=cy+sr;y++) for(int x=cx-sr;x<=cx+sr;x++)
        if(x>=0&&x<w&&y>=0&&y<h) B[y*w+x]=1.f;
    const float dA=1.f,dB=0.5f,f=0.055f,k=0.062f,dt=1.f;
    for(int s=0;s<2000;s++){
        for(int y=1;y<h-1;y++) for(int x=1;x<w-1;x++){
            int i=y*w+x;
            float a=A[i],b=B[i];
            float lA=A[(y-1)*w+x]+A[(y+1)*w+x]+A[y*w+x-1]+A[y*w+x+1]-4*a;
            float lB=B[(y-1)*w+x]+B[(y+1)*w+x]+B[y*w+x-1]+B[y*w+x+1]-4*b;
            float rxn=a*b*b;
            nA[i]=fmaxf(0.f,fminf(1.f,a+dt*(dA*.2f*lA-rxn+f*(1-a))));
            nB[i]=fmaxf(0.f,fminf(1.f,b+dt*(dB*.2f*lB+rxn-(k+f)*b)));
        }
        A=nA; B=nB;
    }
    for(int y=0;y<h;y++) for(int x=0;x<w;x++){
        float b=B[y*w+x];
        int64_t base=(int64_t)(y*w+x)*4;
        buf[base]=0.9f-b*0.75f; buf[base+1]=0.82f-b*0.65f;
        buf[base+2]=0.65f-b*0.55f; buf[base+3]=1.f;
    }
}

static void run_flow_field(float* buf, int w, int h) {
    for(int i=0;i<w*h;i++){ buf[i*4]=0.02f; buf[i*4+1]=0.02f; buf[i*4+2]=0.05f; buf[i*4+3]=1.f; }
    for(int p=0;p<4000;p++){
        uint32_t hs=(uint32_t)(p*1234567891u); hs^=hs>>17; hs*=0x45d9f3bu; hs^=hs>>16;
        float px=(float)(hs&0xFFFF)/65535.f*w, py=(float)((hs>>16)&0xFFFF)/65535.f*h;
        float hue=(float)p/4000.f;
        float pr=0.3f+0.5f*sinf(hue*6.28f);
        float pg=0.3f+0.5f*sinf(hue*6.28f+2.094f);
        float pb=0.3f+0.5f*sinf(hue*6.28f+4.189f);
        for(int s=0;s<400;s++){
            int ix=(int)px,iy=(int)py;
            if(ix<0||ix>=w||iy<0||iy>=h) break;
            int64_t idx=(int64_t)(iy*w+ix)*4;
            buf[idx]=fminf(1.f,buf[idx]+pr*.025f);
            buf[idx+1]=fminf(1.f,buf[idx+1]+pg*.025f);
            buf[idx+2]=fminf(1.f,buf[idx+2]+pb*.025f);
            float angle=simplex2(px/w*4.f,py/h*4.f)*12.5664f;
            px+=cosf(angle)*1.8f; py+=sinf(angle)*1.8f;
        }
    }
}

static void run_clifford_attractor(float* buf, int w, int h) {
    for(int i=0;i<w*h;i++){ buf[i*4]=0.f; buf[i*4+1]=0.f; buf[i*4+2]=0.f; buf[i*4+3]=1.f; }
    const float a=-1.4f,b=1.6f,c=1.f,d=0.7f;
    float x=0.1f,y=0.1f;
    float mn_x=1e9f,mx_x=-1e9f,mn_y=1e9f,mx_y=-1e9f;
    float tx=x,ty=y;
    for(int i=0;i<20000;i++){
        float nx=sinf(a*ty)+c*cosf(a*tx), ny=sinf(b*tx)+d*cosf(b*ty);
        tx=nx; ty=ny;
        if(tx<mn_x)mn_x=tx; if(tx>mx_x)mx_x=tx;
        if(ty<mn_y)mn_y=ty; if(ty>mx_y)mx_y=ty;
    }
    float rx=mx_x-mn_x+.05f, ry=mx_y-mn_y+.05f;
    for(int i=0;i<3000000;i++){
        float nx=sinf(a*y)+c*cosf(a*x), ny=sinf(b*x)+d*cosf(b*y);
        x=nx; y=ny;
        int px=(int)((x-mn_x)/rx*(w-1));
        int py=(int)((y-mn_y)/ry*(h-1));
        if(px>=0&&px<w&&py>=0&&py<h){
            int64_t idx=(int64_t)(py*w+px)*4;
            float t=(float)i/3000000.f;
            buf[idx]=fminf(1.f,buf[idx]+.008f);
            buf[idx+1]=fminf(1.f,buf[idx+1]+.005f*(0.5f+0.5f*sinf(t*80.f)));
            buf[idx+2]=fminf(1.f,buf[idx+2]+.003f);
        }
    }
}

static void make_normal_map(float* hbuf, float* nbuf, int w, int h) {
    for(int y=1;y<h-1;y++) for(int x=1;x<w-1;x++){
        float hL=hbuf[(y*w+x-1)*4], hR=hbuf[(y*w+x+1)*4];
        float hD=hbuf[((y-1)*w+x)*4], hU=hbuf[((y+1)*w+x)*4];
        float dx=(hR-hL)*.5f, dy=(hU-hD)*.5f, dz=0.08f;
        float len=sqrtf(dx*dx+dy*dy+dz*dz);
        int64_t b=(int64_t)(y*w+x)*4;
        nbuf[b]  =(-dx/len)*.5f+.5f;
        nbuf[b+1]=(-dy/len)*.5f+.5f;
        nbuf[b+2]=(dz/len)*.5f+.5f;
        nbuf[b+3]=1.f;
    }
}

// ============================================================
// STANDARD HOST FUNCTIONS (called by Frust via extern fn)
// ============================================================
static std::vector<float*> g_arrays;

extern "C" {
    float host_sin(float v)  { return sinf(v);  }
    float host_cos(float v)  { return cosf(v);  }
    float host_abs_f32(float v) { return fabsf(v); }
    int64_t host_abs_i64(int64_t v) { return v<0?-v:v; }
    float host_sqrt(float v) { return sqrtf(v<0?0:v); }
    float host_floor_f32(float v) { return floorf(v); }
    float host_pow(float b, float e) { return powf(b,e); }
    float host_atan2(float y, float x) { return atan2f(y,x); }
    float host_fmod(float a, float b) { return fmodf(a,b); }
    float host_min_f32(float a, float b) { return a<b?a:b; }
    float host_max_f32(float a, float b) { return a>b?a:b; }
    float host_clamp_f32(float v, float lo, float hi) { return v<lo?lo:(v>hi?hi:v); }
    float host_log(float v) { return logf(v>0?v:1e-10f); }
    float host_exp(float v) { return expf(v); }

    int64_t host_f32_to_i64(float v) { return (int64_t)v; }
    float   host_i64_to_f32(int64_t v) { return (float)v; }

    float* host_alloc_f32_array(int64_t len) {
        float* a=new float[len](); g_arrays.push_back(a); return a;
    }
    void  host_write_f32_array(float* a, int64_t i, float v) { a[i]=v; }
    float host_read_f32_array(float* a, int64_t i) { return a[i]; }

    float host_simplex2(float x, float y)   { return simplex2(x,y); }
    float host_worley2(float x, float y)    { return worley2(x,y); }
    float host_worley_hash(float x, float y){ return worley_hash(x,y); }

    void host_img_set_pixel(float* buf, int64_t w, int64_t x, int64_t y,
                            float r, float g, float b, float a) {
        if(x<0||y<0||x>=w) return;
        int64_t base=(y*w+x)*4;
        buf[base]=r; buf[base+1]=g; buf[base+2]=b; buf[base+3]=a;
    }

    void host_generate_reaction_diffusion(float* buf, int64_t w, int64_t h) {
        run_reaction_diffusion(buf,(int)w,(int)h);
    }
    void host_generate_flow_field(float* buf, int64_t w, int64_t h) {
        run_flow_field(buf,(int)w,(int)h);
    }
    void host_generate_attractor(float* buf, int64_t w, int64_t h) {
        run_clifford_attractor(buf,(int)w,(int)h);
    }
}

// ============================================================
// FRUST FUNCTION DECLARATIONS
// ============================================================
extern "C" {
    void generate_hills(float*,int64_t,int64_t);
    void generate_hills_raw(float*,int64_t,int64_t);
    void generate_clouds(float*,int64_t,int64_t);
    void generate_planet(float*,int64_t,int64_t);
    void generate_caves(float*,int64_t,int64_t);
    void generate_ocean_floor(float*,int64_t,int64_t);
    void generate_wood(float*,int64_t,int64_t);
    void generate_marble(float*,int64_t,int64_t);
    void generate_cracks(float*,int64_t,int64_t);
    void generate_stained_glass(float*,int64_t,int64_t);
    void generate_rust(float*,int64_t,int64_t);
    void generate_plasma(float*,int64_t,int64_t);
    void generate_tunnel(float*,int64_t,int64_t);
    void generate_mandelbrot(float*,int64_t,int64_t);
    void generate_julia(float*,int64_t,int64_t);
    void generate_stars(float*,int64_t,int64_t);
    void generate_heatmap(float*,int64_t,int64_t);
    void generate_truchet(float*,int64_t,int64_t);
    void generate_tile(float*,int64_t,int64_t);
    void generate_album(float*,int64_t,int64_t);
    void generate_reaction_diffusion(float*,int64_t,int64_t);
    void generate_flow_field(float*,int64_t,int64_t);
    void generate_attractor(float*,int64_t,int64_t);
}

// ============================================================
// HELPERS
// ============================================================
static void save_png(float* buf, int w, int h, const std::string& path) {
    printf("  -> %s\n", path.c_str());
    std::vector<uint8_t> u8(w*h*4);
    for(int i=0;i<w*h*4;i++){
        float v=buf[i]; v=v<0?0:(v>1?1:v);
        u8[i]=(uint8_t)(v*255.f+.5f);
    }
    stbi_write_png(path.c_str(),w,h,4,u8.data(),w*4);
}

static float* alloc_buf(int w, int h){ return new float[w*h*4](); }
static void free_buf(float* b){ delete[] b; }

// ============================================================
// MAIN
// ============================================================
int main() {
    noise_init();

    const char* out = "D:\\FrustLang\\projects\\image_host\\output\\";
    printf("Generating %d effects...\n\n", 24);

    struct E { const char* name; void(*fn)(float*,int64_t,int64_t); int w,h; };
    E effects[] = {
        {"hills",              generate_hills,              1024,1024},
        {"hills_raw",          generate_hills_raw,          1024,1024},
        {"clouds",             generate_clouds,             1024,1024},
        {"planet",             generate_planet,             1024,1024},
        {"caves",              generate_caves,              1024,1024},
        {"ocean_floor",        generate_ocean_floor,        1024,1024},
        {"wood",               generate_wood,               1024,1024},
        {"marble",             generate_marble,             1024,1024},
        {"cracks",             generate_cracks,             1024,1024},
        {"stained_glass",      generate_stained_glass,      1024,1024},
        {"rust",               generate_rust,               1024,1024},
        {"plasma",             generate_plasma,             1024,1024},
        {"tunnel",             generate_tunnel,             1024,1024},
        {"mandelbrot",         generate_mandelbrot,         1200,900 },
        {"julia",              generate_julia,              1200,900 },
        {"stars",              generate_stars,              1024,1024},
        {"heatmap",            generate_heatmap,            1024,1024},
        {"truchet",            generate_truchet,            1024,1024},
        {"tile",               generate_tile,               1024,1024},
        {"album",              generate_album,              1024,1024},
        {"reaction_diffusion", generate_reaction_diffusion, 512, 512 },
        {"flow_field",         generate_flow_field,         1024,1024},
        {"attractor",          generate_attractor,          1024,1024},
    };

    for(auto& e : effects) {
        printf("[%s]\n", e.name);
        float* buf = alloc_buf(e.w, e.h);
        e.fn(buf, e.w, e.h);
        save_png(buf, e.w, e.h, std::string(out)+e.name+".png");
        free_buf(buf);
    }

    // Normal map derived from hills_raw
    printf("[normal_map]\n");
    float* hbuf = alloc_buf(1024,1024); float* nbuf = alloc_buf(1024,1024);
    generate_hills_raw(hbuf, 1024, 1024);
    make_normal_map(hbuf, nbuf, 1024, 1024);
    save_png(nbuf, 1024, 1024, std::string(out)+"normal_map.png");
    free_buf(hbuf); free_buf(nbuf);

    for(auto* a : g_arrays) delete[] a;
    printf("\nDone! All images saved to %s\n", out);
    return 0;
}
