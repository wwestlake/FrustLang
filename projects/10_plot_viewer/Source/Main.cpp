
#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <atomic>

// ============================================================
// NOISE & MATH
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
    for(int s=0;s<20;s++){ // run fewer steps per frame so it animates
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
    for(int p=0;p<500;p++){
        uint32_t hs=(uint32_t)(p*1234567891u); hs^=hs>>17; hs*=0x45d9f3bu; hs^=hs>>16;
        float px=(float)(hs&0xFFFF)/65535.f*w, py=(float)((hs>>16)&0xFFFF)/65535.f*h;
        float hue=(float)p/4000.f;
        float pr=0.3f+0.5f*sinf(hue*6.28f);
        float pg=0.3f+0.5f*sinf(hue*6.28f+2.094f);
        float pb=0.3f+0.5f*sinf(hue*6.28f+4.189f);
        for(int s=0;s<100;s++){
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
    for(int i=0;i<30000;i++){ // fewer per frame
        float nx=sinf(a*y)+c*cosf(a*x), ny=sinf(b*x)+d*cosf(b*y);
        x=nx; y=ny;
        int px=(int)((x-mn_x)/rx*(w-1));
        int py=(int)((y-mn_y)/ry*(h-1));
        if(px>=0&&px<w&&py>=0&&py<h){
            int64_t idx=(int64_t)(py*w+px)*4;
            float t=(float)i/3000000.f;
            buf[idx]=fminf(1.f,buf[idx]+.028f);
            buf[idx+1]=fminf(1.f,buf[idx+1]+.015f*(0.5f+0.5f*sinf(t*80.f)));
            buf[idx+2]=fminf(1.f,buf[idx+2]+.013f);
        }
    }
}

// ============================================================
// EXTERNAL BINDINGS
// ============================================================
extern "C" void render_graph(int32_t* pixels, int64_t width, int64_t height, int64_t frame_count);

extern "C" void generate_hills(float*,int64_t,int64_t);
extern "C" void generate_clouds(float*,int64_t,int64_t);
extern "C" void generate_planet(float*,int64_t,int64_t);
extern "C" void generate_caves(float*,int64_t,int64_t);
extern "C" void generate_ocean_floor(float*,int64_t,int64_t);
extern "C" void generate_wood(float*,int64_t,int64_t);
extern "C" void generate_marble(float*,int64_t,int64_t);
extern "C" void generate_cracks(float*,int64_t,int64_t);
extern "C" void generate_stained_glass(float*,int64_t,int64_t);
extern "C" void generate_rust(float*,int64_t,int64_t);
extern "C" void generate_plasma(float*,int64_t,int64_t);
extern "C" void generate_tunnel(float*,int64_t,int64_t);
extern "C" void generate_mandelbrot(float*,int64_t,int64_t);
extern "C" void generate_julia(float*,int64_t,int64_t);
extern "C" void generate_stars(float*,int64_t,int64_t);
extern "C" void generate_heatmap(float*,int64_t,int64_t);
extern "C" void generate_truchet(float*,int64_t,int64_t);
extern "C" void generate_tile(float*,int64_t,int64_t);
extern "C" void generate_album(float*,int64_t,int64_t);


static std::vector<float*> g_allocations;
extern "C" {
    int64_t host_f32_to_i64(float v) { return static_cast<int64_t>(v); }
    float host_i64_to_f32(int64_t v) { return static_cast<float>(v); }
    void host_set_pixel(int32_t* pixels, int64_t idx, int32_t color) { pixels[idx] = color; }

    float* host_alloc_f32_array(int64_t len) {
        float* arr = new float[len];
        g_allocations.push_back(arr);
        return arr;
    }
    void host_write_f32_array(float* arr, int64_t idx, float val) { arr[idx] = val; }
    float host_read_f32_array(float* arr, int64_t idx) { return arr[idx]; }

    float host_sin(float theta) { return std::sin(theta); }
    float host_cos(float theta) { return std::cos(theta); }
    float host_abs_f32(float v) { return std::abs(v); }
    int64_t host_abs_i64(int64_t v) { return std::abs(v); }
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

// Plot viewer specifics
static FILE* g_stream_pipe = nullptr;

extern "C" void host_open_stream() {
    if (g_stream_pipe) fclose(g_stream_pipe);
    g_stream_pipe = fopen("D:\\000 LLM Data System\\research\\physics\\Dimuon_DoubleMu.txt", "r");
    if (g_stream_pipe) {
        char buf[2048];
        fgets(buf, sizeof(buf), g_stream_pipe); // skip header row
    }
}

extern "C" int64_t host_read_stream_chunk(float* buffer, int64_t max_len) {
    if (!g_stream_pipe) return 0;
    int64_t count = 0;
    char buf[2048];
    while (count < max_len && fgets(buf, sizeof(buf), g_stream_pipe)) {
        char* p = buf;
        char* last_val = p;
        while (*p) {
            if (*p == ',') last_val = p + 1;
            p++;
        }
        float m = strtof(last_val, nullptr);
        buffer[count++] = m;
    }
    return count;
}

extern "C" void host_close_stream() {
    if (g_stream_pipe) {
        fclose(g_stream_pipe);
        g_stream_pipe = nullptr;
    }
}

static float g_bins[60] = {0};
static int g_stream_state = 0;
extern "C" int64_t host_get_stream_state() { return g_stream_state; }
extern "C" void host_set_stream_state(int64_t state) { g_stream_state = state; }

static float g_stream_buffer[5000];
extern "C" float* host_get_stream_buffer() { return g_stream_buffer; }

extern "C" void host_write_bin(int64_t idx, float val) { 
    if (idx >= 0 && idx < 60) g_bins[idx] = val; 
}
extern "C" float host_read_bin(int64_t idx) { 
    if (idx >= 0 && idx < 60) return g_bins[idx];
    return 0.0f;
}
extern "C" float* host_get_bin_ptr() { return g_bins; }

uint64_t get_glyph(char c) {
    switch(c) {
        case '0': return 0b01110100011001110101110011000101110;
        case '1': return 0b00100011000010000100001000010001110;
        case '2': return 0b01110100010000100110010001000011111;
        case '3': return 0b01110100010000100110000011000101110;
        case '4': return 0b00010001100101010010111110001000010;
        case '5': return 0b11111100001111000001000011000101110;
        case '6': return 0b00110010001000011110100011000101110;
        case '7': return 0b11111000010001000100010000100001000;
        case '8': return 0b01110100011000101110100011000101110;
        case '9': return 0b01110100011000101111000010001001100;
        case '.': return 0b00000000000000000000000000110001100;
        case '-': return 0b00000000000000001110000000000000000;
        default:  return 0;
    }
}

extern "C" void host_draw_number(int32_t* pixels, int64_t w, int64_t h, int64_t x, int64_t y, float val, int32_t color) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", val);
    int curr_x = static_cast<int>(x);
    for (int i=0; buf[i] != '\0'; i++) {
        uint64_t glyph = get_glyph(buf[i]);
        for (int row=0; row<7; row++) {
            for (int col=0; col<5; col++) {
                int bit_idx = (6 - row) * 5 + (4 - col);
                if ((glyph >> bit_idx) & 1) {
                    int px = curr_x + col;
                    int py = static_cast<int>(y) + row;
                    if (px >= 0 && px < w && py >= 0 && py < h) {
                        pixels[py * w + px] = color;
                    }
                }
            }
        }
        curr_x += 6;
    }
}

// ============================================================
// GLOBAL STATE
// ============================================================
static int g_active_effect = 0; // 0 = plot viewer, 1-20 = graphics
static const int NUM_EFFECTS = 20;
static const char* EFFECT_NAMES[] = {
    "Graph (PlotViewer)", "Hills", "Clouds", "Planet", "Caves", "Ocean Floor",
    "Wood", "Marble", "Cracks", "Stained Glass", "Rust", "Plasma", "Tunnel",
    "Mandelbrot", "Julia", "Stars", "Heatmap", "Truchet", "Tile", "Album"
};

// ============================================================
// COMPONENTS
// ============================================================
class PlotComponent : public juce::Component, public juce::Thread
{
public:
    PlotComponent() : juce::Thread("FrustRenderThread") {
        setWantsKeyboardFocus(true);
        noise_init();
        
        for (int i = 0; i < 3; ++i) {
            m_raw_buffers[i].resize(1200 * 800, 0xFF000000); 
        }
        m_img_buffer = new float[1200 * 800 * 4]();
        
        openGLContext.attachTo(*this);
        openGLContext.setContinuousRepainting(true);
        
        startThread();
    }
    
    ~PlotComponent() override {
        stopThread(2000);
        openGLContext.detach();
        host_close_stream();
        for (auto* arr : g_allocations) { delete[] arr; }
        g_allocations.clear();
        delete[] m_img_buffer;
    }
    
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::rightKey || key == juce::KeyPress::upKey) {
            g_active_effect = (g_active_effect + 1) % NUM_EFFECTS;
            return true;
        } else if (key == juce::KeyPress::leftKey || key == juce::KeyPress::downKey) {
            g_active_effect = (g_active_effect - 1 + NUM_EFFECTS) % NUM_EFFECTS;
            return true;
        }
        return false;
    }
    
    void paint(juce::Graphics& g) override {
        int ready = m_readyIdx.exchange(-1, std::memory_order_acq_rel);
        if (ready != -1) {
            m_frontIdx.store(ready, std::memory_order_release);
        }
        
        int front = m_frontIdx.load(std::memory_order_acquire);
        
        juce::Image img(juce::Image::ARGB, 1200, 800, false);
        {
            juce::Image::BitmapData data(img, juce::Image::BitmapData::readWrite);
            memcpy(data.data, m_raw_buffers[front].data(), 1200 * 800 * sizeof(int32_t));
        }
        
        g.drawImageAt(img, 0, 0);
        
        g.setColour(juce::Colours::white);
        g.setFont(20.0f);
        g.drawText(juce::String(EFFECT_NAMES[g_active_effect]) + " (Use Left/Right Arrows to switch)", 10, 10, 800, 30, juce::Justification::topLeft, true);
    }
    
    void run() override {
        int64_t frame_count = 0;
        while (!threadShouldExit()) {
            
            int active = g_active_effect;
            
            if (active == 0) {
                render_graph(m_raw_buffers[m_backIdx].data(), 1200, 800, frame_count);
            } else {
                // Call the appropriate procedural generator
                switch(active) {
                    case 1: generate_hills(m_img_buffer, 1200, 800); break;
                    case 2: generate_clouds(m_img_buffer, 1200, 800); break;
                    case 3: generate_planet(m_img_buffer, 1200, 800); break;
                    case 4: generate_caves(m_img_buffer, 1200, 800); break;
                    case 5: generate_ocean_floor(m_img_buffer, 1200, 800); break;
                    case 6: generate_wood(m_img_buffer, 1200, 800); break;
                    case 7: generate_marble(m_img_buffer, 1200, 800); break;
                    case 8: generate_cracks(m_img_buffer, 1200, 800); break;
                    case 9: generate_stained_glass(m_img_buffer, 1200, 800); break;
                    case 10: generate_rust(m_img_buffer, 1200, 800); break;
                    case 11: generate_plasma(m_img_buffer, 1200, 800); break;
                    case 12: generate_tunnel(m_img_buffer, 1200, 800); break;
                    case 13: generate_mandelbrot(m_img_buffer, 1200, 800); break;
                    case 14: generate_julia(m_img_buffer, 1200, 800); break;
                    case 15: generate_stars(m_img_buffer, 1200, 800); break;
                    case 16: generate_heatmap(m_img_buffer, 1200, 800); break;
                    case 17: generate_truchet(m_img_buffer, 1200, 800); break;
                    case 18: generate_tile(m_img_buffer, 1200, 800); break;
                    case 19: generate_album(m_img_buffer, 1200, 800); break;
                }
                
                // Convert float* to int32_t* (ARGB)
                int32_t* dest = m_raw_buffers[m_backIdx].data();
                for (int i=0; i<1200*800; i++) {
                    float r = m_img_buffer[i*4];
                    float g = m_img_buffer[i*4+1];
                    float b = m_img_buffer[i*4+2];
                    
                    int32_t ir = (int32_t)(r * 255.0f) & 0xFF;
                    int32_t ig = (int32_t)(g * 255.0f) & 0xFF;
                    int32_t ib = (int32_t)(b * 255.0f) & 0xFF;
                    
                    dest[i] = (0xFF << 24) | (ir << 16) | (ig << 8) | ib;
                }
            }
            
            frame_count++;
            
            m_readyIdx.store(m_backIdx, std::memory_order_release);
            
            int next_back = 0;
            int front = m_frontIdx.load(std::memory_order_acquire);
            int ready = m_readyIdx.load(std::memory_order_acquire);
            
            while (next_back == front || next_back == ready) {
                next_back = (next_back + 1) % 3;
            }
            m_backIdx = next_back;
            
            // Slow down procedural effects slightly to save CPU if not rendering graph
            if (active > 0) juce::Thread::sleep(50);
            else juce::Thread::sleep(1);
        }
    }
    
private:
    juce::OpenGLContext openGLContext;
    std::vector<int32_t> m_raw_buffers[3];
    float* m_img_buffer;
    std::atomic<int> m_frontIdx{0};
    std::atomic<int> m_readyIdx{-1};
    int m_backIdx = 1;
};

class PlotViewerWindow : public juce::DocumentWindow
{
public:
    PlotViewerWindow(const juce::String& name)
        : DocumentWindow(name, juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(ResizableWindow::backgroundColourId), DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new PlotComponent(), true);
        setResizable(false, false);
        centreWithSize(1200, 800);
        setVisible(true);
    }
    
    void closeButtonPressed() override {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class PlotViewerApplication : public juce::JUCEApplication
{
public:
    PlotViewerApplication() {}
    const juce::String getApplicationName() override       { return "PlotViewer Accelerated"; }
    const juce::String getApplicationVersion() override    { return "0.6.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }
    void initialise(const juce::String&) override          { mainWindow.reset(new PlotViewerWindow(getApplicationName())); }
    void shutdown() override                               { mainWindow = nullptr; }
    void systemRequestedQuit() override                    { quit(); }
    
private:
    std::unique_ptr<PlotViewerWindow> mainWindow;
};

START_JUCE_APPLICATION(PlotViewerApplication)
