/*
MIT License

Copyright (c) 2023 Svetoslav Kolev

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <util/mathutil.h>
#include <util/args.h>
#include <util/common.h>
#include <util/timer.h>
#include <util/helper.h>
#include <util/display.h>
#include <util/unixcommon.h>

#include <filesystem>

#include <set>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <algorithm>
#include <array>

#include <stb_truetype.h>
#include <pangolin/pangolin.h>

#include "shaders.h"
#include <font.h>

namespace fs=std::filesystem;


struct Sample
  {
  using T=double;
  T t,x;
  };

struct OneVertex
  {
  struct { float x,y,z,w;} vertex;
  struct { float x,y,z;} normal;
  struct { float s,t;} texcoord;
  };


struct ChanInfo; 
struct WindowInfo;
struct FrameInfo;

struct TextImage 
  {
  double framex,framey,desired_size,angle,r,g,b;
  std::string text;
  
  void ensure_texture(double size);
  void render_font(const std::string& text, double size);
  
  pangolin::ManagedImage<uint32_t> image;
  pangolin::GlTexture tex;
  
  double rendered_size=-1;
  std::string rendered_text;
  };
  
void TextImage::ensure_texture(double size)
  {
  if(rendered_text!=text || size!=rendered_size)
    {
    render_font(text,size);
    tex.Reinitialise(image.w,image.h,GL_RGBA8,true,0,GL_RGBA,GL_UNSIGNED_BYTE,image.ptr);
    }
  }

struct STBTT_Font
  {
  STBTT_Font(const std::string& file)
    {
    fontdata.resize(fs::file_size(file));
    FILE*fn=fopen(file.c_str(),"rb");
    fread(fontdata.data(),1,fontdata.size(),fn);
    fclose(fn);
    
    if(!stbtt_InitFont(&info, fontdata.data(), 0)){printf("STB failed to load font %s\n",file.c_str());exit(11);}
    }
  
  STBTT_Font(std::vector<unsigned char> font) : fontdata(std::move(font))
    {
    if(!stbtt_InitFont(&info, fontdata.data(), 0)){printf("STB failed to load font with length %zu\n",fontdata.size());exit(11);}
    }
  
  std::vector<unsigned char> fontdata;
  stbtt_fontinfo info;
  
  };

void TextImage::render_font(const std::string& txt, double size)
  {
  static std::string font_folder="./config/fonts/";
  //static STBTT_Font ft(font_folder+"Helvetica.ttf");
  static STBTT_Font ft(std::vector<unsigned char>(binary_font, binary_font+binary_font_len));
  
  
  int line_height=std::round(size+0.5);
  
  int w=text.size()*line_height;
  int h=line_height;
  
  pangolin::ManagedImage<unsigned char> im(w,h);
  im.Fill(0);
  
  float scale = stbtt_ScaleForPixelHeight(&ft.info, line_height);
  int x = 0;
  
  int ascent, descent, lineGap;
  stbtt_GetFontVMetrics(&ft.info, &ascent, &descent, &lineGap);
  
  ascent = roundf(ascent * scale);
  descent = roundf(descent * scale);
  
  for(size_t n=0;n<text.size();n++)
    {
    int ax;
    int lsb;
    stbtt_GetCodepointHMetrics(&ft.info, text[n], &ax, &lsb);
    
    int c_x1, c_y1, c_x2, c_y2;
    stbtt_GetCodepointBitmapBox(&ft.info, text[n], scale, scale, &c_x1, &c_y1, &c_x2, &c_y2);
    int y = ascent + c_y1;
    
    int byteOffset = x + roundf(lsb * scale) + (y * w);
    stbtt_MakeCodepointBitmap(&ft.info, im.ptr + byteOffset, c_x2 - c_x1, c_y2 - c_y1, w, scale, scale, text[n]);
    
    x += roundf(ax * scale);
    
    int kern;
    kern = stbtt_GetCodepointKernAdvance(&ft.info, text[n], text[n + 1]);
    x += roundf(kern * scale);
    }
  
  int minx=w-1;
  int maxx=0;
  
  for(int q2=0;q2<h;q2++)for(int q1=0;q1<w;q1++)if(im(q1,q2))
    {
    minx=std::min(minx,q1);
    maxx=std::max(maxx,q1);
    }
  
  pangolin::ManagedImage<uint32_t> im2(maxx-minx+1,h);
  
  for(int q2=0;q2<h;q2++)for(int q1=minx;q1<=maxx;q1++)
    {
    auto c1=im(q1,h-1-q2);
    uint32_t data=0;
    data+=(1u<<24)*(c1?255:0);
    data+=(1u<<16)*(c1?255:0);
    data+=(1u<< 8)*(c1?255:0);
    data+=(1u<< 0)*255;
    im2(q1-minx,q2)=data;
    }
  
  image=std::move(im2);
  rendered_text=text;
  rendered_size=size;
  }
  


struct ChanInfo
  {
  int active=0;
  int window=0;
  int used=0;
  int showshadow=1;
  double samplesperpixel=10;
  int wintab=1;
  
  struct Segment
    {
    std::vector < Sample > data;
    double vastart=sw_qnan();
    
    
    double get_data_at_time(double t);
    int lefttime (double t);
    int righttime(double t);
    void findtime(double lt, double rt, double samplespp, double width);
    
    //runtime
    ChanInfo* parent;
    int c1,c2;
    int toprint,stride;
    double alpha;
    };
  
  struct ImageData
    {
    std::vector<float> data;
    std::vector<pangolin::GlTexture> tex;
    int totalfill=0;
    
    int maxtexture=4096;
    
    float t1=1e10,t2=-1e10;
    float x1=1e10,x2=-1e10;
    float dx=0,dt=0;
    int h=0,w=0;
    bool fixtex=false;
    
    void clear() 
      {
      for(auto&e1:tex)
        {
        glDeleteTextures(1,&e1.tid);
        e1.internal_format = 0;
        e1.tid = 0;
        e1.width = 0;
        e1.height = 0;
        }
      *this=ImageData();
      }
    
    };
  
  std::vector<Segment> data;
  ImageData data2;
  
  struct {double r=1,g=1,b=1,a=1,width=1;int style=0;} style;
  
  std::string name,dname,label;
  
  int displayname=1;
  WindowInfo*win=nullptr;
  
  
  TextImage im_name;
  TextImage im_label;
  
  };

struct ScaleInfo 
  {
  struct Element { double label; double pos; int size; };
  std::vector < Element > points;
  std::vector < Element > lines;
  int dec;
  };

struct FrameInfo
  {
  int reconfigured=1;
  int mode=0;
  int right_label=0;
  int win_basic_color=0;
  int used=0;
  int active=0;
  int mousedraw=1;
  double x1=0,y1=0,x2=1,y2=1;
  double textratio=0.25;
  double labelratio=1;
  double offsetlabel=0;
  double textsize=8;
  double timespan=10;
  double endtime=10;
  double mt=0.02;
  double mb=0.03;
  double ml=0.04;
  double mr=0.00;
  
  
  //runtime
  double da_sx,da_sy,da_xc,da_yc,da_uyc;
  double lsizex,lsizey;
  double scalea=-1,scaleb=-1;
  
  double lasttime,firsttime;
  
  std::vector < OneVertex > pts;
  pangolin::GlBuffer vbo;
  
  //mouse
  struct {double x,y; int inside;}mouse;
  double mx,my;
  double drag_end,drag_span,drag_x;
  
  
  std::string name;
  std::set<int> windows;
  
  std::vector<std::string> linked_frames_time;
  
  std::map < std::string , TextImage > Images;
  
  };



struct WindowInfo
  {
  std::string name;
  int autorange=0;
  int minskip=0;
  double pos_top=1;
  double pos_bottom=1;
  double r=1,g=1,b=1;
  int frame=0;
  int used=0;
  int names=1;
  int logsc=0;
  
  double& top   (int t=1) { return top_   [1]; }
  double& bottom(int t=1) { return bottom_[1]; }
  
  double top_   [2]={10,1.0};
  double bottom_[2]={10,0.0};
  
  std::set<int> channels;
  
  std::vector < OneVertex > pts;
  pangolin::GlBuffer vbo;
  
  //mouse
  struct {double x,y; int inside;} mouse;
  double mx,my;
  double drag_y=-1;
  double drag_bottom;
  double drag_top; 
  
  int reconfigured=1;
  int curtab=1;
  
  FrameInfo*fr=nullptr;
  
  };



template<typename T>
struct svector
  {
  std::vector < T > data;
  svector() = default;
  svector(int a) : data(a) {}
  T& operator[](int a){if(a>=(int)data.size())data.resize(a+1);return data[a];}
  auto begin(){return data.begin();}
  auto end(){return data.end();}
  };

///////////////////////////////////////////////////////////////////////////////////////////////////////



double ChanInfo::Segment::get_data_at_time(double t)
  {
  int n=data.size();
  
  if(data.size()==0)return sw_qnan(); 
  if(data.size()==1)return sw_qnan();
  
  
  if(t<data[0].t)return sw_qnan();
  if(t>data[n-1].t)return sw_qnan();
  
  int l,m,r;
  l=0;
  r=data.size()-2;
  while(l<r)
    {
    m=(l+r+1)/2;
    if(data[m].t>=t)r=m-1;else l=m;
    }
  
  
  
  return data[l].x+(data[l+1].x-data[l].x)*(t-data[l].t)/(data[l+1].t-data[l].t);
  
  //if(l==0)return channels[a].data[l].x;
  //if(l==channels[a].data.size()-1)return channels[a].data[l].x;
  //return l;
  }


int ChanInfo::Segment::lefttime(double t)
  {
  int l,m,r;
  l=0;
  r=data.size()-1;
  while(l<r)
    {
    m=(l+r+1)/2;
    if(data[m].t>=t)r=m-1;
    else l=m;
    }
  return l;
  }

int ChanInfo::Segment::righttime(double t)
  {
  int l,m,r;
  l=0;
  r=data.size()-1;
  while(l<r)
    {
    m=(l+r)/2;
    if(data[m].t<=t)l=m+1;
    else r=m;
    }
  return l>r?-1:l;
  }


void ChanInfo::Segment::findtime(double lt, double rt, double samplespp, double width)
  {
  c1=lefttime(lt);
  c2=righttime(rt);
  //printf("%30s SIZE: %4d %10lf %10lf %4d %4d\n",parent->name.c_str(),(int)data.size(),lt,rt,c1,c2);
  toprint=(int)std::round(width*samplespp+0.5);
  stride=(c2-c1+1)/toprint;
  if(stride==0)stride=1;
  toprint=(c2-c1+stride)/stride;
  //if(c2<c1)printf("Unsorted Data detected: (%s)  %d %d\n",parent->name.c_str(),c1,c2);
  alpha=0.3*pow(1-std::min(1.0,toprint/width*10),4);
  }


////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Instance
  {
  
  
  svector < ChanInfo > channels{1<<16};
  svector < WindowInfo > windows{1<<12};
  svector < FrameInfo > frames{1<<8};
  
  
  FrameInfo*cf=nullptr;
  
  std::unordered_map < ChanInfo::Segment*, std::unique_ptr<pangolin::GlBuffer> > vbos;
  std::unordered_map < std::string , int > uchannels,uframes,uwindows;
  
  pangolin::View* drawing_area=nullptr;
  pangolin::WindowInterface* pango_window=nullptr;
  //InputHandler handler;
  
  int size_request=0;
  int size_request_x=0;
  int size_request_y=0;
  
  struct MouseInfo
    {
    struct ButtonInfo {double x=0,y=0; int pressed=0;};
    
    double x=0,y=0,sx=0,sy=0;
    int inside=0;
    ButtonInfo button[4];
    } mi;
  
  struct ScreenshotRequest
    {
    int take=0;
    int sizex=0,sizey=0;
    int x=0,y=0;
    bool blocking=false;
    bool precise=false;
    std::string dest;
    } screenshot;
  
  struct FW_Motion
    {
    static constexpr double motion_time=0.5;
    Timer t;
    
    FW_Motion() { for(int q1=0;q1<4;q1++)t.set(q1,motion_time*2); }
    
    } fw_motion;
  
  
  ChanInfo::Segment* chanselect=nullptr;
  int sizex=0,sizey=0;
  Timer maint;
  std::recursive_mutex configdata;
  
  std::mutex input_queue_mutex;
  std::queue<std::function<void()>> input_queue;
  std::atomic<bool> stopped{false};
  
  //counters
  int totalprint=0;
  int totallinepts=0;
  
  //opengl timers
  //GLuint query[3]={}; // The unique query id
  //GLuint queryres[3]={}; // Save the time, in nanoseconds
  
  //shaders
  //GLuint pshader=0,lshader=0;
  pangolin::GlSlProgram point_shader,line_shader;
  std::map < std::string , GLuint > attribpos{{"vertex",0},{"normal",2},{"texcoord",4}};
  
  double bg_col[4]={0,0,0,0};
  double fg_col[4]={1,1,1,1};
  
  
  //options
  int shaderuse=1;
  int displayfonts=1;
  int displaylists=1;
  int iconify=0;
  int usevsync=1;
  bool draw_curtab=true;
  bool use_dynamic_range=true;
  bool print_stats=false;
  //844
  std::vector<std::string> autocolors={"800","080","008","400","040","004","880","808","088","440","404","044","884","848","884"};
  
  std::string iname,wname,shmname;
  
  void add_input_event(std::function<void()>&& f)
    {
    std::lock_guard LG(input_queue_mutex);
    input_queue.push(std::move(f));
    }
  
  void flush_input_event_queue()
    {
    std::lock_guard LG(input_queue_mutex);
    while(!input_queue.empty())
      {
      auto f=input_queue.front();
      input_queue.pop();
      f();
      }
    }

void doshaders()
  {
  point_shader.AddShader(pangolin::GlSlVertexShader  ,RawShaders::point_vert);
  point_shader.AddShader(pangolin::GlSlFragmentShader,RawShaders::point_frag);
  point_shader.Link();
  
  line_shader.AddShader(pangolin::GlSlVertexShader  ,RawShaders::line_vert);
  line_shader.AddShader(pangolin::GlSlFragmentShader,RawShaders::line_frag);
  for(auto&i:attribpos)glBindAttribLocation(line_shader.ProgramId(),i.second,i.first.c_str());
  line_shader.Link();
  
  }


bool accept(double a)
  {
  static constexpr double maxsample=1e36;
  
  if(std::isnan((double)a))return false;
  if(a<-maxsample || a>maxsample)return false;
  return true;
  }

void clear_data(int q1)
  {
  channels[q1].data.clear();
  channels[q1].data2.clear();
  }
void clear_data(const std::string& a)
  {
  if(uchannels.find(a)==uchannels.end())return;
  clear_data(uchannels[a]);
  //printf("clearing %s\n",a.c_str());
  }

void clear_all_data()
  {
  for(auto&i:uchannels)clear_data(i.second);
  }

int firstfree(const std::unordered_map < std::string , int >& a)
  {
  std::set<int>all;
  for(auto&i:a)all.insert(i.second);
  for(int q1=1;;q1++)if(all.find(q1)==all.end())return q1;
  }


void newsamples(const char*name,Sample* a,int num,bool newsegment=false)
  {
  int chnum=uchannels[name];
  if(chnum==0){uchannels.erase(name);return;}
  ChanInfo& ch=channels[chnum];
  
  if(ch.data.size()==0 || newsegment)ch.data.emplace_back();
  
  ch.data.back().vastart=std::nan("");
  ch.data.back().parent=&ch;
  
  //printf("%s %zu\n",ch.name.c_str(),ch.data.back().data.size());
  //for(int q1=0;q1<num;q1++)if(accept(a->x))ch->data.push_back(*(a++));
  
  for(int q1=0;q1<num;q1++)if(accept(a[q1].x))ch.data.back().data.push_back(a[q1]);
  //else printf("%lf %lf\n",a[q1].x,a[q1].t);
  
  //while(ch->data.size()>maxxx*2)ch->data.erase(ch->data.begin(),ch->data.begin()+maxxx);
  //printf("%s %zu\n",ch.name.c_str(),ch.data.back().data.size());
  }

void newimage(const char* name, float* data)
  {
  int chnum=uchannels[name];
  if(chnum==0){uchannels.erase(name);return;}
  ChanInfo& ch=channels[chnum];
  auto& d=ch.data2;
  
  
  int w=data[4];
  int h=data[5];
  
  if(h!=d.h)d.clear();
  
  d.h=h;
  d.w+=w;
  
  d.t1=std::min(d.t1,data[0]);
  d.t2=std::max(d.t2,data[1]);
  d.x1=std::min(d.x1,data[2]);
  d.x2=std::max(d.x2,data[3]);
  
  d.dt=data[6];
  d.dx=data[7];
  
  ch.data.clear();
  ch.data.emplace_back();
  ch.data[0].parent=&ch;
  ch.data[0].data={{d.t1,d.x1},{d.t2,d.x2}};
  
  //printf("%f %f %f %f %f %f   %d %d %d\n",d.x1,d.x2,d.t1,d.t2,d.dx,d.dt,d.h,d.w,w);
  
  for(int q1=0;q1<w*h*3;q1++)d.data.push_back(data[16+q1]);
  //printf("%zu\n",d.data.size());
  //d.image.Reinitialise(w,h,pangolin::PixelFormatFromString("RGB96F"));
  //memcpy(d.image.ptr,data+16,d.image.SizeBytes());
  d.fixtex=true;
  //d.gltex.Load(d.image);
  
  }



void print_status()
  {
  for(auto&f:uframes)
    {
    printf("Frame: %d \"%s\" \"%s\"  Windows:",f.second,f.first.c_str(),frames[f.second].name.c_str());
    for(auto&w:frames[f.second].windows)printf(" %d",w);
    printf("   Used: %d",frames[f.second].used);
    printf("\n");
    }
  for(auto&w:uwindows)
    {
    printf("Window: %d \"%s\" \"%s\"  Channels:",w.second,w.first.c_str(),windows[w.second].name.c_str());
    for(auto&c:windows[w.second].channels)printf(" %d",c);
    printf("   Used: %d Frame: %d",windows[w.second].used,windows[w.second].frame);
    printf("\n");
    }
  for(auto&c:uchannels)
    {
    printf("Channel: %d \"%s\" \"%s\"",c.second,c.first.c_str(),channels[c.second].name.c_str());
    printf("   Used: %d Window: %d",channels[c.second].used,channels[c.second].window);
    printf("\n");
    }
  }


void remove_window(const std::string& name)
  {
  int w=uwindows[name];
  if(!w){uwindows.erase(name);return;}
  WindowInfo& win=windows[w];
  win.used=0;
  win.fr->windows.erase(w);
  uwindows.erase(name);
  win=WindowInfo{};
  }

void remove_channel(const std::string& name)
  {
  int c=uchannels[name];
  if(!c){uchannels.erase(name);return;}
  channels[c].used=0;
  channels[c].win->channels.erase(c);
  //if(channels[c].win->channels.size()==0)remove_window(channels[c].win->name);
  uchannels.erase(name);
  }

void deactivate_channel(const std::string& name)
  {
  int c=uchannels[name];
  if(!c){uchannels.erase(name);return;}
  channels[c].active=0;
  }

void sort_channel(const std::string& name)
  {
  int c=uchannels[name];if(!c){uchannels.erase(name);return;}
  //  bool operator < (const Sample& other) const {return t<other.t;}
  for(auto&s:channels[c].data)sort(s.data.begin(),s.data.end(),[](const Sample& a, const Sample& b){return a.t<b.t;});
  }

void remove_window2(const std::string& name)
  {
  int w=uwindows[name];
  if(!w){uwindows.erase(name);return;}
  WindowInfo& win=windows[w];
  auto cc=win.channels;
  for(auto&c:cc)remove_channel(channels[c].name);
  win=WindowInfo{};
  uwindows.erase(name);
  }
  

void remove_frame(const std::string& name)
  {
  int f=uframes[name];
  if(!f){uframes.erase(name);return;}
  frames[f].used=0;
  //for(auto&c:uchannels)if(channels[c.second].win->fr->name==name)remove_channel(channels[c.second].name);
  for(auto&w:frames[f].windows)remove_window2(windows[w].name);
  uframes.erase(name);
  }
void delete_text_frame(const std::string& name)
  {
  int f=uframes[name];
  if(!f){uframes.erase(name);return;}
  frames[f].Images.clear();
  }

void show_frame(const std::string& name, int value)
  {
  int f=uframes[name];
  if(!f){uframes.erase(name);return;}
  frames[f].active=value;
  }

void remove_all_channels()
  {
  std::vector<std::string> all;
  for(auto&i:uchannels)all.push_back(i.first);
  for(auto&i:all)remove_channel(i);
  }

void deactivate_all_channels()
  {
  std::vector<std::string> all;
  for(auto&i:uchannels)all.push_back(i.first);
  for(auto&i:all)deactivate_channel(i);
  }

void remove_all_frames()
  {
  std::vector<std::string> all;
  for(auto&i:uframes)all.push_back(i.first);
  for(auto&i:all)remove_frame(i);
  }

void follow_all_frames()
  {
  findlasttimes();
  for(auto&i:uframes)frames[i.second].endtime=frames[i.second].lasttime;
  }

void show_all_frames_prefix(const std::string& prefix)
  {
  std::vector<std::string> all;
  for(auto&i:uframes)if(i.first.find(prefix)==0)all.push_back(i.first);
  for(auto&i:all)show_frame(i,1);
  }


void hide_all_frames()
  {
  std::vector<std::string> all;
  for(auto&i:uframes)all.push_back(i.first);
  for(auto&i:all)show_frame(i,0);
  }

auto extract(const char*a)
  {
  std::vector < std::pair < std::string , double > > ret;
  for(int q1=0;q1<a[1];q1++)
    {
    std::string e1=(char*)a+256+q1*16;
    double c1=*((double*)(a+256+q1*16+8));
    ret.push_back(make_pair(e1,c1));
    }
  return ret;
  }

std::vector<std::string> extractVS(const char*a)
  {
  std::vector<std::string> e1;
  std::istringstream e(a+32);
  while(!e.eof()){std::string e2;e >> e2;e1.push_back(e2);}
  return e1;
  }

void configchannel(const char*a)
  {
  maint.start(105);
  auto e1=extract(a);
  auto e2=extractVS(a);
  maint.stop(105);
  std::string frame,window,name,dname,label;
  
  for(size_t q1=0;q1<e2.size();q1++)if(e2[q1]=="#name")if(q1<e2.size()-1)if(e2[q1+1].size()&&e2[q1+1][0]=='@')name=e2[q1+1].substr(1);
  for(size_t q1=0;q1<e2.size();q1++)if(e2[q1]=="#window")if(q1<e2.size()-1)if(e2[q1+1].size()&&e2[q1+1][0]=='@')window=e2[q1+1].substr(1);
  for(size_t q1=0;q1<e2.size();q1++)if(e2[q1]=="#frame")if(q1<e2.size()-1)if(e2[q1+1].size()&&e2[q1+1][0]=='@')frame=e2[q1+1].substr(1);
  for(size_t q1=0;q1<e2.size();q1++)if(e2[q1]=="#dname")if(q1<e2.size()-1)if(e2[q1+1].size()&&e2[q1+1][0]=='@')dname=e2[q1+1].substr(1);
  for(size_t q1=0;q1<e2.size();q1++)if(e2[q1]=="#label")if(q1<e2.size()-1)if(e2[q1+1].size()&&e2[q1+1][0]=='@')label=e2[q1+1].substr(1);
  
  //printf("%s %s %s %s %s\n",frame.c_str(),window.c_str(),name.c_str(),dname.c_str(),label.c_str());
  
  if(frame==""||name=="")return;
  if(uframes.find(frame)==uframes.end())return;
  
  if(dname=="")dname=name;
  if(window=="")window=name;
  
  int c,w,f;
  f=uframes[frame];
  
  
  if(uchannels.find(name)==uchannels.end()){c=uchannels[name]=firstfree(uchannels);channels[c]=ChanInfo();}else c=uchannels[name];
  if(channels[c].win!=nullptr)if(channels[c].win->name!=window)
    { printf("Channel \"%s\" has non-existing window \"%s\"\n",name.c_str(),window.c_str()); return; } 
  
  if(uwindows.find(window)==uwindows.end()){w=uwindows[window]=firstfree(uwindows);windows[w]=WindowInfo();}else w=uwindows[window];
  
  
  WindowInfo& win=windows[w];
  ChanInfo& chan=channels[c];
  
  
  //printf("%s    %s    %s\n",name.c_str(),window.c_str(),frame.c_str());
  if(channels[c].win)assert(channels[c].win==&win && "cannot reassign channel window");
  if(win.fr)  assert(win.fr==&frames[f]    && "cannot reassign window frame  ");
  
  chan.used=1;
  chan.name=name;
  chan.dname=dname;
  chan.label=label;
  chan.window=w;
  chan.win=&win;
  
  win.channels.insert(c);
  win.used=1;
  win.name=window;
  win.frame=f;
  win.fr=&frames[f];
  
  frames[f].windows.insert(w);
  
  for(auto&i:e1)
    {
    if(i.first=="top")   if(!win.autorange)win.top()=i.second;
    if(i.first=="bottom")if(!win.autorange)win.bottom()=i.second;
    if(i.first=="posbot")win.pos_bottom=i.second;
    if(i.first=="postop")win.pos_top=i.second;
    if(i.first=="minskp")win.minskip=i.second;
    if(i.first=="autor") {win.autorange=(int)i.second;printf("%s %d\n",name.c_str(),win.autorange);}
    if(i.first=="names") win.names=(int)i.second;
    if(i.first=="logsc") win.logsc=(int)i.second;
    
    if(i.first=="red")  chan.style.r=i.second;
    if(i.first=="green")chan.style.g=i.second;
    if(i.first=="blue" )chan.style.b=i.second;
    if(i.first=="alpha")chan.style.a=i.second;
    if(i.first=="style")chan.style.style=(int)i.second;
    if(i.first=="width")chan.style.width=i.second;
    
    if(i.first=="wintab")chan.wintab=(int)i.second;
    if(i.first=="shownm")chan.displayname=(int)i.second;
    if(i.first=="showsh")chan.showshadow=(int)i.second;
    if(i.first=="active")chan.active=(int)i.second;
    
    if(i.first=="perpix")chan.samplesperpixel=i.second;
    
    if(i.first=="clear")if((int)i.second==1)clear_data(c);
    
    }
  
  if(!win.autorange)win.reconfigured=1;
  chan.im_name.text=dname;
  chan.im_label.text=label;
  
  
  //printf("NEW CHANNEL: %s(%d) %s(%d) %s(%d) \n",name.c_str(),c,window.c_str(),w,frame.c_str(),f);
  //for(auto&i:e1)printf("%s: %lf\n",i.first.c_str(),i.second);
  
  }

void configframe(const char*a)
  {
  auto e1=extract(a);
  auto e2=extractVS(a);
  
  std::string name;
  for(size_t q1=0;q1<e2.size();q1++)if(e2[q1]=="#name")if(q1<e2.size()-1)if(e2[q1+1].size()&&e2[q1+1][0]=='@')name=e2[q1+1].substr(1);
  
  int f;
  
  if(uframes.find(name)==uframes.end()){f=uframes[name]=firstfree(uframes);frames[f]=FrameInfo();}else f=uframes[name];
  
  
  frames[f].used=1;
  frames[f].name=name;
  
  
  for(auto&i:e1)
    {
    if(i.first=="texts")frames[f].textsize=i.second;
    if(i.first=="textr")frames[f].textratio=i.second;
    if(i.first=="labelr")frames[f].labelratio=i.second;
    if(i.first=="offlab")frames[f].offsetlabel=i.second;
    if(i.first=="span")frames[f].timespan=i.second;
    if(i.first=="end")frames[f].endtime=i.second;
    
    if(i.first=="mt")frames[f].mt=i.second;
    if(i.first=="mb")frames[f].mb=i.second;
    if(i.first=="mr")frames[f].mr=i.second;
    if(i.first=="ml")frames[f].ml=i.second;
    
    if(i.first=="mouse")frames[f].mousedraw=(int)i.second;
    if(i.first=="mode")frames[f].mode=(int)i.second;
    if(i.first=="rightl")frames[f].right_label=(int)i.second;
    if(i.first=="wincol")frames[f].win_basic_color=(int)i.second;
    if(i.first=="active")frames[f].active=(int)i.second;
    if(i.first=="x1")frames[f].x1=i.second;
    if(i.first=="y1")frames[f].y1=i.second;
    if(i.first=="x2")frames[f].x2=i.second;
    if(i.first=="y2")frames[f].y2=i.second;
    
    }
  
  //if(!(old==frames[f]))frames[f].reconfigured=1;
  //printf("NEW FRAME: %s %d\n",name.c_str(),f);for(auto&i:e1)printf("%s: %lf\n",i.first.c_str(),i.second);
  }

void removetext(const char*a)
  {
  std::string fname=a+64;
  std::string tname=a+192;
  
  
  
  }

void addtexttoframe(const char*a)
  {
  TextImage i1;
  std::string frame_name=a+64;
  std::string text2render=a+128;
  std::string text_name=a+192;
  auto e1=extract(a);
  std::map < std::string , double > e2;
  for(auto&i:e1)e2[i.first]=i.second;
  i1.framex=e2["framex"];
  i1.framey=e2["framey"];
  i1.angle=e2["angle"];
  i1.desired_size=e2["size"];
  i1.r=e2["r"];
  i1.g=e2["g"];
  i1.b=e2["b"];
  
  if(uframes.find(frame_name)==uframes.end()){printf("no such frame: %s .... exiting \n",frame_name.c_str());exit(112);}
  
  int f=uframes[frame_name];
  frames[f].Images[text_name]=std::move(i1);
  frames[f].Images[text_name].text=text2render;
  
  //printf("%s(%d) %s %lf %lf %lf %lf\n",name.c_str(),f,frames[f].Images[tname].text.c_str(),frames[f].Images[tname].framex,frames[f].Images[tname].framey,frames[f].Images[tname].angle,frames[f].Images[tname].size);
  }

struct CommHandler
  {
  SharedMemoryOne smc;
  CommStruct s{1<<24};
  
  SharedMemoryOne sms;
  CommStruct ss{1<<10};
  
  int samples=0;
  int packets=0;
  int cnt=0;
  
  CommHandler(const std::string& name) : smc(name,1<<24,false), sms(name+"-feedback",1<<16,false) {}
  };

std::unique_ptr<CommHandler> comm;

void listen_main()
  {
  
  SharedMemoryOne& smc=comm->smc;
  CommStruct& s=comm->s;
  
  while(1) 
    {
    int c1=smc.receive2(s.d,false);
    
    //if(c1)printf("%d\n",c1);
    
    if(c1==0)break;
    
    std::lock_guard LG(configdata);
    
    maint.start(100);
    
    /// add flash functionality
    
     
    if(s.d[0]==11)remove_all_channels();
    if(s.d[0]==12)remove_channel(s.c+32);
    if(s.d[0]==15)deactivate_all_channels();
    
    if(s.d[0]==21)clear_all_data();
    if(s.d[0]==22)clear_data(s.c+32);
    
    
    if(s.d[0]==31)remove_all_frames();
    if(s.d[0]==32)remove_frame(s.c+32);
    if(s.d[0]==33)hide_all_frames();
    if(s.d[0]==35)show_all_frames_prefix(s.c+32);
    if(s.d[0]==37)follow_all_frames();
    
    if(s.d[0]==41)remove_window2(s.c+32);
    
    if(s.d[0]==51)sort_channel(s.c+32);
    
    if(s.d[0]==61)
      {
      while(screenshot.take==1)
        {
        configdata.unlock();
        usleep(1000);
        configdata.lock();
        }
      screenshot.precise=true;
      screenshot.blocking=s.i[1];
      screenshot.dest=s.c+32;
      screenshot.take=1;
      screenshot.sizex=sizex;
      screenshot.sizey=sizey;
      screenshot.x=screenshot.y=0;
      }
    if(s.d[0]==65)
      {
      for(int q1=0;q1<4;q1++)bg_col[q1]=s.data[4+q1];
      for(int q1=0;q1<3;q1++)fg_col[q1]=1-s.data[4+q1];
      }
    
    
    if(s.d[0]==71)addtexttoframe(s.c);
    if(s.d[0]==72)delete_text_frame(s.c+32);
    
    if(s.d[0]==81)
      {
      size_request=1;
      size_request_x=s.i[1];
      size_request_y=s.i[2];
      }
    if(s.d[0]==82)
      {
      if(std::string(s.c+8)=="display_lines")displaylists=s.i[1];
      if(std::string(s.c+8)=="display_fonts")displayfonts=s.i[1];
      if(std::string(s.c+8)=="iconify")iconify=s.i[1];
      }
    
    int& cnt=comm->cnt;
    int& samples=comm->samples;
    int& packets=comm->packets;
    
    if(s.d[0]==91)
      {
      while(cnt){configdata.unlock();cnt--;}
      configdata.lock();
      samples=packets=0;
      cnt++;
      maint.start(201);
      }
    if(s.d[0]==92)
      {
      maint.stop(201);
      if(print_stats)printf("Time to unlock: %lf   \t  %d packets   %d samples\n",maint(201),packets,samples);
      if(cnt){cnt--;configdata.unlock();}
      }
    
    if(s.d[0]==111)
      {
      maint.start(104);
      configframe(s.c);
      maint.stop(104);
      }
  
    if(s.d[0]==101)
      {
      maint.start(103);
      configchannel(s.c);
      maint.stop(103);
      }
    
    if(s.d[0]==151 && s.d[1]==1)
      {
      //printf("LINKING: %s %s\n",s.c+32,s.c+128);
      
      frames[uframes[s.c+32]].linked_frames_time.push_back(s.c+128);
      }
    
    if(s.d[0]==201) draw_curtab=s.i[1];
    if(s.d[0]==202) use_dynamic_range=s.i[1];
    
    
    if(s.d[0]==3)
      {
      printf("Single sample method unsupported\n");
      }
    
    if(s.d[0]==4)
      {
      maint.start(102);
      
      packets++;
      //printf("++++++++++++%s %d %d\n",s.c+8,chnum,s.i[1]);
      if(s.d[1])clear_data(s.c+8);
      //printf("------------%s %d %d\n",s.c+8,chnum,s.i[1]);
      newsamples(s.c+8,(Sample*)(s.c+64),s.i[1],s.d[2]);
      
      //printf("============%s %d %d\n",s.c+8,chnum,s.i[1]);
      samples+=s.i[1];
      maint.stop(102);
      }
    
    if(s.d[0]==5)
      {
      maint.start(102);
      if(s.d[1])clear_data(s.c+8);
      
      newimage(s.c+8,s.f+16);
      
      
      
      maint.stop(102);
      }
    
    maint.stop(100);
    
    while(maint(110)>1.0)
      {
      break;
      //maint.start(110);
      //printf("total: %lf \t",maint.acc(100)*1000);
      //printf("single: %lf \t",maint.acc(101)*1000);
      //printf("multiple: %lf \t",maint.acc(102)*1000);
      //printf("confchan: %lf \t",maint.acc(103)*1000);
      //printf("conffr: %lf \t",maint.acc(104)*1000);
      //printf("chanprep: %lf \t",maint.acc(105)*1000);
      //printf("\n");
      
      }
    
    }
  
  }



void findminmax(WindowInfo& win)
  {
  static constexpr double maxsample=1e36;
  
  double mn=maxsample;
  double mx=-maxsample;
  for(auto&c:win.channels)if(channels[c].active)for(auto&segment:channels[c].data)
    {
    static constexpr int numsamplesminmax=128;
    
    int c2=segment.c2;
    int c1=segment.c1;
    int skip=(c2-c1+1)/numsamplesminmax;
    if(skip==0)skip=1;
    if(win.minskip)skip=std::min(skip,win.minskip);
    //if(win.name=="eit-hp.12.eit-hp.12")
    //printf("%30s:  %6d %6d %6d\n",channels[c].name.c_str(),c1,c2,skip);
    for(int q1=c2;q1>=c1;q1-=skip)
      {
      if(mn>segment.data[q1].x)mn=segment.data[q1].x;
      if(mx<segment.data[q1].x)mx=segment.data[q1].x;
      }
    }
  if(mn==maxsample||mx==-maxsample){mn=0;mx=1;}
  else if(mn==mx){mn-=0.5,mx+=0.5;}
  
  double newtop=win.top();
  double newbottom=win.bottom();
  //double middle=(newtop+newbottom)/2;
  double dif=newtop-newbottom;
  
  
  //if(mx>newtop||mx<middle+dif/4||mn<newbottom||mn>middle-dif/4)
  if(mx>newtop||mn<newbottom||(mx-mn)<dif/3)
    {
    //if(win.name=="eit-hp.12.eit-hp.12")printf("%s: %15lf %15lf %15lf %15lf   %4d %6d      \n",win.name.c_str(),mx,mn,newtop,newbottom,cnt,tcnt);
    
    newtop=mx+(mx-mn)/20.0;
    newbottom=mn-(mx-mn)/20.0;
    
    }
  
  //printf("%s: %.12lf %.12lf: %.12lf %.12lf   %.12lf %.12lf : %d %d\n",mn,mx,win.name.c_str(),newtop,newbottom,win.top,win.bottom,newtop!=win.top,newbottom!=win.bottom);
  
  if(newtop!=win.top()||newbottom!=win.bottom())
    {
    win.top()=newtop;
    win.bottom()=newbottom;
    win.reconfigured=1;
    }
  }



void findminmax_total(WindowInfo& win)
  {
  static constexpr double maxsample=1e36;
  
  double mn=maxsample;
  double mx=-maxsample;
  for(auto&c:win.channels)if(channels[c].active)if(channels[c].wintab==win.curtab)
    {
    for(auto&e2:channels[c].data)for(auto&e1:e2.data)
      {
      if(mn>e1.x)mn=e1.x;
      if(mx<e1.x)mx=e1.x;
      }
    }
  //printf("%d: %lf %lf\n",w,mx,mn);
  if(mn==maxsample||mx==-maxsample){mn=0;mx=1;}
  else if(mn==mx){mn-=0.5,mx+=0.5;}
  
  double newtop=win.top();
  double newbottom=win.bottom();
  //double middle=(newtop+newbottom)/2;
  double dif=newtop-newbottom;
  
  
  //if(mx>newtop||mx<middle+dif/4||mn<newbottom||mn>middle-dif/4)
  if(mx>newtop||mn<newbottom||(mx-mn)<dif)
    {
    newtop=mx+(mx-mn)/20.0;
    newbottom=mn-(mx-mn)/20.0;
    }
  
  //printf("%s: %.12lf %.12lf: %.12lf %.12lf   %.12lf %.12lf : %d %d\n",mn,mx,win.name.c_str(),newtop,newbottom,win.top,win.bottom,newtop!=win.top,newbottom!=win.bottom);
  
  if(newtop!=win.top()||newbottom!=win.bottom())
    {
    win.top()=newtop;
    win.bottom()=newbottom;
    win.reconfigured=1;
    }
  }



void findlasttimes()
  {
  constexpr double inf=1e100;
  
  for(auto&i:uframes){frames[i.second].lasttime=-inf;frames[i.second].firsttime=inf;}
  for(auto&i:uchannels)
    {
    const ChanInfo& c=channels[i.second];
    if(!c.active)continue;
    for(auto&s:c.data)
      {
      if(!s.data.size())continue;
      if(s.data.back().t > c.win->fr->lasttime )c.win->fr->lasttime =s.data.back().t;
      if(s.data[0].t     < c.win->fr->firsttime)c.win->fr->firsttime=s.data[0].t;
      }
    }
    
  for(auto&i:uframes)if(frames[i.second].lasttime==-inf)frames[i.second].lasttime=1;
  for(auto&i:uframes)if(frames[i.second].firsttime==inf)frames[i.second].firsttime=-1;
  for(auto&i:uframes)if(frames[i.second].lasttime==frames[i.second].firsttime)frames[i.second].firsttime+=0.1;
  for(auto&i:uframes)if(0)
    {
    auto&f=frames[i.second];
    //if(f.name=="est.cyl")printf("%lf %lf %lf %lf\n",f.endtime,f.timespan,f.firsttime,f.lasttime);
    
         if(f.endtime-f.timespan>f.lasttime){f.endtime=f.lasttime;f.timespan=f.lasttime-f.firsttime;}
    else if(f.endtime<f.firsttime){f.endtime=f.lasttime;f.timespan=f.lasttime-f.firsttime;}
    
    if(f.endtime>f.lasttime)f.endtime=f.lasttime;
    if(f.endtime-f.timespan<f.firsttime)f.timespan=f.lasttime-f.firsttime;
    
    
    //if(f.name=="est.cyl")printf("%lf %lf %lf %lf\n\n",f.endtime,f.timespan,f.firsttime,f.lasttime);
    }
  }






///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void compute_number(char*w,double num,int decimal,int zeroes=0)
  {
  
  
  maint.start(99);
  int ptr=0,neg=0,q1;

  long long c1=1,c2,c3;
  for(q1=0;q1<decimal;q1++)c1*=10;
  c2=(long long)round((double)c1*num);
  if(c2<0){neg=1;c2=-c2;}
  c3=c2%c1;
  if(c3)
    {
    int c4=0;
    while(c3%10==0){c3/=10;c4++;w[ptr]=zeroes?'0':' ';}
    for(;c4<decimal;c4++){w[ptr++]=c3%10+48;c3/=10;}
    w[ptr++]='.';
    }
  else
    {
    for(int c4=0;c4<=decimal;c4++)w[ptr]=' ';
    }
  c3=c2/c1;
  if(c3==0)w[ptr++]=48;
  else 
    {
    while(c3)
      {
      w[ptr++]=48+c3%10;
      c3/=10;
      }
    }
  if(neg)w[ptr++]='-';
  
  maint.stop(99);
  }

double scale_interval(int zoom)
  {
  double c1;
  if(zoom%3==0)c1=10.0;
  else if(zoom%3==1)c1=5.0;
  else c1=2.0;
  return c1*pow(10,5.0-(double)(zoom/3));
  }

std::vector < double > scale_range(double a,double b,int zoom)
  {
  std::vector < double > r;
  double c1=scale_interval(zoom);
  double c2=round((a-c1*3)/c1)*c1;
  
  for(;c2<b+c1/2.0;c2+=c1)if(c2>a-c1/2.0)r.push_back(c2);
  
  return r;
  }

int scale_belong_zoom(double a,int b)
  {
  constexpr double eps=1e-7;
  
  double c=scale_interval(b);
  double d=std::abs(std::round(a/c)-a/c);
 // printf("%lf %lf %lf\n",d,a/c,round(a/c));
  if(d<eps)return 1;
  return 0;
  }

ScaleInfo scale_construct(double a, double b, double fontsize, double pixels, double ratio)
  {
  int q1;
  
  for(q1=-30;q1<60;q1++)
    {  
    auto r=scale_range(a,b,q1);
    if(fontsize*r.size()>pixels*ratio)break;
    }
  auto r=scale_range(a,b,q1+2);
  
  std::vector < int > count(r.size());
  for(int q2=0;q2<(int)r.size();q2++)
    {
    if(scale_belong_zoom(r[q2],q1))count[q2]++;
    if(scale_belong_zoom(r[q2],q1-2))count[q2]++;
    }
 // for(q2=0;q2<r.size();q2++)printf("%lf: %d\n",r[q2],count[q2]);
  double d1=scale_interval(q1);
  int c1=0;
  while(d1<1.0){d1=d1*10.0;c1++;}
  
  ScaleInfo scale;
  scale.dec=c1;
  //printf("Dec: %d %.15lf %.15lf\n",c1,a,b);
  for(int q2=0;q2<(int)r.size();q2++)
    {
    if(count[q2])scale.points.push_back({r[q2],r[q2],count[q2]>1});
    scale.lines.push_back({r[q2],r[q2],count[q2]});
    }
  //printf("scale %10lld  \t%4.3lf %4.3lf, %4.3lf: %d: ",maint(21),a,b,pixels,r.size());
  return scale;
  }

ScaleInfo log_scale_construct(double a, double b, double fontsize, double pixels, double ratio)
  {
  int upper=std::ceil(b);
  int lower=std::floor(a);
  int total=(upper-lower+1);
  
  int level=0;
  if(fontsize*total*2<pixels*ratio)level=1;
  if(fontsize*total*3<pixels*ratio)level=2;
  
  ScaleInfo scale;
  for(int q1=lower;q1<=upper;q1++)for(int q2=1;q2<10;q2++)
    {
    double l=q1+std::log10(q2);
    
    double val=std::pow(10,q1)*q2;
    
    if(l<a || l>b) continue;
    
    int size=0;
    if(level==0)size=(q2==1)?2:0;
    if(level==1)size=(q2==1)?2:(q2==3);
    if(level==2)size=(q2==1)?2:(q2==2||q2==5);
    
    scale.lines.push_back({val,l,size});
    if(size)scale.points.push_back({val,l,size>1});
    }
  
  scale.dec=10;
  
  return scale;
  }


ScaleInfo hscale_construct(double a,double b,double fontsize,double pixels,double ratio)
  {
  int q1,q2,c1,c2;
  std::vector < double > r;
  std::vector < int > count;
  char w[24];
  double d1;
  
  ScaleInfo scale;
  
  for(q1=0;q1<44;q1++)
    {  
    r=scale_range(a,b,q1);
    d1=scale_interval(q1);
    c1=c2=0;
    while(d1<1.0){d1=d1*10.0;c1++;}
    for(q2=0;q2<(int)r.size();q2++)
      {
      memset(w,0,sizeof(w));
      compute_number(w,r[q2],c1);
      c2+=strlen(w);
      }
    //printf("%lf %lf %d %d\n",a,b,q1,c2);
    if((double)c2*fontsize>pixels*ratio*0.8)break;
    }
  r=scale_range(a,b,q1+2);
  count=std::vector < int > (r.size());
  for(q2=0;q2<(int)r.size();q2++)
    {
    if(scale_belong_zoom(r[q2],q1))count[q2]++;
    if(scale_belong_zoom(r[q2],q1-2))count[q2]++;
    }
 // for(q2=0;q2<r.size();q2++)printf("%lf: %d\n",r[q2],count[q2]);
  d1=scale_interval(q1);
  c1=0;
  while(d1<1.0){d1=d1*10.0;c1++;}
  scale.dec=c1;
  for(q2=0;q2<(int)r.size();q2++)
    {
    if(count[q2])scale.points.push_back({r[q2],r[q2],count[q2]>1});
    scale.lines.push_back({r[q2],r[q2],count[q2]});
    }
  return scale;
  }

static constexpr GLubyte rasters[13*43]={
  0xe0, 0xa0, 0xa0, 0xa0, 0xe0, 0x20, 0x20, 0x20, 0x60, 0x20, 0xe0, 0x80, 0xe0, 0x20, 0xe0, 0xe0, 0x20, 0xe0, 0x20, 0xe0, 0x20, 0x20, 0xe0, 0xa0, 0xa0, 0xe0, 0x20, 0xe0, 0x80, 0xe0, 0xe0, 0xa0, 0xe0, 0x80, 0xe0, 0x20, 0x20, 0x20, 0x20, 0xe0, 0xe0, 0xa0, 0xe0, 0xa0, 0xe0, 0xe0, 0x20, 0xe0, 0xa0, 0xe0, 0x00, 0x00, 0x60, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x60, 0x90, 0x90, 0x90, 0x90, 0x90, 0x60, 0x20, 0x20, 0x20, 0x20, 0x20, 0x60, 0x20, 0xf0, 0x80, 0x40, 0x20, 0x10, 0x10, 0xe0, 0xe0, 0x10, 0x10, 0x60, 0x10, 0x10, 0xe0, 0x10, 0x10, 0x10, 0x70, 0x90, 0x90, 0x90, 0xe0, 0x10, 0x10, 0xe0, 0x80, 0x80, 0x70, 0x60, 0x90, 0x90, 0xe0, 0x80, 0x80, 0x60, 0x40, 0x40, 0x20, 0x20, 0x10, 0x10, 0xe0, 0x60, 0x90, 0x90, 0x60, 0x90, 0x90, 0x60, 0x60, 0x10, 0x10, 0x70, 0x90, 0x90, 0x60, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x70, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0xe0, 0x20, 0xf8, 0x80, 0x40, 0x20, 0x10, 0x08, 0x08, 0x88, 0x70, 0x70, 0x88, 0x08, 0x08, 0x30, 0x08, 0x08, 0x88, 0x70, 0x10, 0x10, 0xf8, 0x90, 0x50, 0x50, 0x30, 0x30, 0x10, 0x70, 0x88, 0x08, 0x08, 0x08, 0xf0, 0x80, 0x80, 0xf8, 0x70, 0x88, 0x88, 0x88, 0xf0, 0x80, 0x80, 0x88, 0x70, 0x40, 0x40, 0x40, 0x20, 0x20, 0x10, 0x10, 0x08, 0xf8, 0x70, 0x88, 0x88, 0x88, 0x70, 0x88, 0x88, 0x88, 0x70, 0x70, 0x88, 0x08, 0x08, 0x78, 0x88, 0x88, 0x88, 0x70, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x70, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0xe0, 0x20, 0xf8, 0x80, 0x80, 0x40, 0x20, 0x10, 0x08, 0x88, 0x88, 0x70, 0x70, 0x88, 0x08, 0x08, 0x08, 0x30, 0x08, 0x08, 0x88, 0x70, 0x08, 0x08, 0x08, 0xf8, 0x88, 0x48, 0x28, 0x28, 0x18, 0x08, 0x70, 0x88, 0x08, 0x08, 0x08, 0xf0, 0x80, 0x80, 0x80, 0xf8, 0x70, 0x88, 0x88, 0x88, 0xc8, 0xb0, 0x80, 0x80, 0x88, 0x70, 0x40, 0x40, 0x40, 0x20, 0x20, 0x10, 0x10, 0x08, 0x08, 0xf8, 0x70, 0x88, 0x88, 0x88, 0x88, 0x70, 0x88, 0x88, 0x88, 0x70, 0x70, 0x88, 0x08, 0x08, 0x68, 0x98, 0x88, 0x88, 0x88, 0x70, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x38, 0x44, 0x44, 0x82, 0x82, 0x82, 0x82, 0x82, 0x82, 0x44, 0x44, 0x38, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x70, 0x10, 0xfe, 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x82, 0x82, 0x44, 0x38, 0x38, 0x44, 0x82, 0x82, 0x02, 0x04, 0x38, 0x04, 0x82, 0x82, 0x44, 0x38, 0x04, 0x04, 0x04, 0xfe, 0x84, 0x44, 0x24, 0x24, 0x14, 0x0c, 0x0c, 0x04, 0x38, 0x44, 0x82, 0x02, 0x02, 0x02, 0x04, 0xf8, 0x80, 0x80, 0x80, 0xfe, 0x38, 0x44, 0x82, 0x82, 0x82, 0xc6, 0xbc, 0x80, 0x80, 0x82, 0x44, 0x38, 0x10, 0x10, 0x10, 0x10, 0x08, 0x08, 0x08, 0x04, 0x04, 0x02, 0x02, 0xfe, 0x38, 0x44, 0x82, 0x82, 0x82, 0x44, 0x38, 0x44, 0x82, 0x82, 0x44, 0x38, 0x38, 0x44, 0x82, 0x02, 0x02, 0x3a, 0x46, 0x82, 0x82, 0x82, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  };
struct FontInfo {int width,height,space;};
static constexpr FontInfo fonts[5]={{3,5,1},{4,7,1},{5,9,1},{5,10,2},{7,12,2}};


static constexpr GLdouble nums[13][64*2]={
  {0,3,  2,3,  0,13,  2,13,  3,16,   3,14,  7,16,  7,14,  10,13,  8,13,  10,3,  8,3,  7,0,  7,2,  3,0,  3,2,  0,3,  2,3,  -1},
  {1,2,  1,0,  4,2,  9,0,  9,2,  6,2,  4,2,  6,16,  4,13,  5,16,  2,11,  1,12,  -1},
  {0,13,  1,12,  3,16,  3,14,  7,16,  7,14,  10,13,  8,13,  10,11,  8,11,  2,3,  0,3,  2,2,  0,0,  10,2,  10,0,  -1},
  {0,13,  1,12,  3,16,  3,14,  7,16,  7,14,  10,13,  8,13,  10,11,  8,11,  7,9,  5,10,  5,8,  7,9,  8,7,  10,7,  8,3,  10,3,  7,2,  7,0,  3,2,  3,0,  1,4,  0,3,  -1},
  {4,16,  3,16,  2,9,  2,15,  0,8,  0,8,  0,7,  2,9,  6,7,  10,9,  10,7,  8,7,  8,7,  8,0,  8,0,  6,0,  8,16,  6,16,  -1},
  {10,15,  9,16,  10,14,  3,16,  3,14,  0,13,  2,13,  0,9,  2,10,  1,8,  7,10,  7,8,  10,7,  8,7,  10,3,  8,3,  7,0,  7,2,  3,0,  3,2,  0,3,  1,4,  -1},
  {9,12,  10,13,  7,14,  7,16,  3,14,  3,16,  2,13,  0,13,  2,3,  0,3,  3,2,  3,0,  7,2,  7,0,  8,3,  10,3,  8,7,  10,7,  7,8,  7,10,  3,8,  3,10,  2,7,  2,9,  -1},
  {1,12,  0,13,  3,14,  3,16,  3,14,  9,16,  8,14,  10,15,  3,0,  5,1,  4,0,  -1},
  {7,9,  8,8,  8,10,  10,10,  8,13,  10,13,  7,14,  7,16,  3,14,  3,16,  2,13,  0,13,  2,10,  0,10,  3,9,  2,8,  7,9,  8,8,  8,8,  8,6,  10,6,  8,3,  10,3,  7,2,  7,0,  3,2,  3,0,  2,3,  0,3,  2,6,  0,6,  3,7,  2,8,  7,7,  8,8,  8,6,  -1},
  {1,4,  0,3,  3,2,  3,0,  7,2,  7,0,  8,3,  10,3,  8,13,  10,13,  7,14,  7,16,  3,14,  3,16,  2,13,  0,13,  2,9,  0,9,  3,8,  3,6,  7,8,  7,6,  8,9,  8,7,  -1},
  {4,0,  6,0,  6,2,  4,0,  4,2,  -1},
  {3,6,  9,6,  9,8,  3,6,  3,8,  -1}
  };  
  
static constexpr GLdouble numsl[13][64*2]={
  {1,13,  3,15,  7,15,  9,13,  9,3,  7,1,  3,1,  1,3,  1,13,  -1},
  {2,12,  5,15,  5,1,  8,1,  2,1,  -1},
  {1,13,  3,15,  7,15,  9,13,  9,11,  1,3,  1,1,  9,1, -1},
  {1,13,  3,15,  7,15,  9,13,  9,11,  7,9,  4,9,  7,9,  9,7,  9,3,  7,1,  3,1,  1,3, -1},
  {3,15,  1,8,  9,8,  7,8,  7,15,  7,1,  -1},
  {9,15,  3,15,  1,13,  1,9,  7,9,  9,7,  9,3,  7,1,  3,1,  1,3,  -1},
  {9,13,  7,15,  3,15,  1,13,  1,3,  3,1,  7,1,  9,3,  9,7,  7,9,  3,9,  1,7,  -1},
  {1,13,  3,15,  9,15,  4,1, -1},
  {3,8,  7,8,  9,10,  9,13,  7,15,  3,15,  1,13,  1,10,  3,8,  1,6,  1,3,  3,1,  7,1,  9,3,  9,6,  7,8,  -1},
  {1,3,  3,1,  7,1,  9,3,  9,13,  7,15,  3,15,  1,13,  1,9,  3,6,  7,6,  9,9,  -1},
  {4,0,  6,0,  6,2,  4,2,  4,0,  -1},
  {3,7,  9,7,  -1}
  };


void draw_number(double num,int decimal,int font,double x,double y)
  {
  int q1,q2,ptr2;
  char w[24];
  memset(w,0,sizeof(w));
  compute_number(w,num,decimal);
  //printf("%s\n",w);
  
  glPixelStorei(GL_UNPACK_ALIGNMENT,1);
  glColor3f(1.0, 1.0, 1.0);
  glRasterPos2d(x,y);
  
  for(q1=0;w[q1];q1++)
    {
    if(w[q1]=='.')ptr2=11;
    else if(w[q1]=='-')ptr2=10;
    else if(w[q1]==' ')ptr2=12;
    else ptr2=w[q1]-48;
    ptr2=fonts[font].height*ptr2;
    for(q2=0;q2<font;q2++)ptr2+=13*fonts[q2].height;
    glBitmap(fonts[font].width,fonts[font].height,(double)(fonts[font].width+2),(double)fonts[font].width/2.0,-(double)(fonts[font].width+fonts[font].space),0.0,rasters+ptr2);
    }
  }

void draw_number_single(int a){glBegin(GL_TRIANGLE_STRIP);for(int q1=0;nums [a][q1]>-0.5;q1+=2)glVertex2d(nums[a][q1],nums[a][q1+1]);glEnd();}
void draw_number_singleline(int a)
  {
  glBegin(GL_LINE_STRIP);
  for(int q1=0;numsl[a][q1]>-0.5;q1+=2)glVertex2d(numsl[a][q1],numsl[a][q1+1]);
  glEnd();
  //glBegin(GL_POINTS);
  //for(int q1=0;numsl[a][q1]>-0.5;q1+=2)glVertex2d(numsl[a][q1],numsl[a][q1+1]);
  //glEnd();
  }


void draw_number2(double num,int decimal,double font,double x,double y,int position,int root ,int zeroes,double linewidth)
  {
  auto&f=*cf;
  
  glLineWidth(linewidth);
  glPointSize(linewidth);
  
  
  int q1;
  char w[24];
  memset(w,0,sizeof(w));
  compute_number(w,num,decimal,zeroes);
  
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glTranslated(x,y,0.0);
  glScaled(1.0/sizex,1.0/sizey,1.0);
  if(root==0)glScaled(1/(f.x2-f.x1),1/(f.y2-f.y1),1);
  glScaled(font/16.0,font/16.0,1.0);
  
  if(position==0)glTranslated(-12.0,-8.0,0.0);
  if(position==1)glTranslated(6.0*strlen(w)-12,-18.0,0.0);
  if(position==2)glTranslated(0.0,2.0,0.0);
  
  for(q1=0;w[q1];q1++)
    {
    int c1;
    if(w[q1]=='.')c1=10;else if(w[q1]=='-')c1=11;else c1=w[q1]-48;
    
    draw_number_singleline(c1);
    //draw_triangulate(c1,1);
    
    if(w[q1+1]=='.' || w[q1]=='.')glTranslated(-9.0,0.0,0.0);
    else glTranslated(-12.0,0.0,0.0);
    }
  
  glPopMatrix();
  }


void draw_number2v(std::vector < std::vector<double> >& lines,double num,int decimal,double font,double x,double y,int position,int root ,int zeroes,double linewidth)
  {
  auto&f=*cf;
  int q1;
  char w[24];
  memset(w,0,sizeof(w));
  compute_number(w,num,decimal,zeroes);
  
  double sx=1,sy=1;
  double tx=0,ty=0;
  
  sx/=sizex;
  sy/=sizey;
  if(root==0){sx/=(f.x2-f.x1);sy/=(f.y2-f.y1);}
  sx*=font/16.0;
  sy*=font/16.0;
  
  if(position==0){tx-=12;ty-=8;}
  if(position==1){tx+=6.0*strlen(w)-12;ty-=18;}
  if(position==2)ty+=2;
  
  for(q1=0;w[q1];q1++)
    {
    int c1;
    
    if(w[q1]=='.')c1=10;else if(w[q1]=='-')c1=11;else c1=w[q1]-48;
    
    for(int q2=0;numsl[c1][q2+2]!=-1;q2+=2)
      {
      std::vector<double> e1;
      
      for(int q3=0;q3<2;q3++)
        {
        
        double xx=numsl[c1][q2+q3*2+0];
        double yy=numsl[c1][q2+q3*2+1];
        
        xx=(xx+tx)*sx+x;
        yy=(yy+ty)*sy+y;
        
        e1.push_back(xx);
        e1.push_back(yy);
        
        }
      e1.push_back(linewidth);
      lines.push_back(e1);
      }
    
    if(w[q1+1]=='.' || w[q1]=='.')tx-=9;else tx-=12;
    }
    
  
  }
  




void shaderlines(const std::vector<std::vector<double>>& lines, std::vector<OneVertex>& pts)
  {
  for(const std::vector<double>& line:lines)
    {
    double x1=line[0];
    double y1=line[1];
    double x2=line[2];
    double y2=line[3];
    
    float fx1=x1;
    float fy1=y1;
    float fx2=x2;
    float fy2=y2;
    
    double dx=x2-x1;
    double dy=y2-y1;
    
    double len=std::sqrt(dx*dx+dy*dy);
    float flen=len;
    
    double nx=-dy/len;
    double ny=+dx/len;
    
    float fnx=nx;
    float fny=ny;
    
    float ndx=dx/len;
    float ndy=dy/len; 
    
    double lw=line[4];
    float flw=lw;
    
    //float mx=nx*lw;
    //float my=ny*lw;
    
    
    
    OneVertex v;
    
    v.texcoord={+flw,-flen};v.normal={fnx,fny,0};v.vertex={fx1,fy1,-ndx,-ndy};pts.push_back(v);
    v.texcoord={-flw,-flen};v.normal={fnx,fny,0};v.vertex={fx1,fy1,-ndx,-ndy};pts.push_back(v);
    v.texcoord={-flw,+flen};v.normal={fnx,fny,0};v.vertex={fx2,fy2,+ndx,+ndy};pts.push_back(v);
    v.texcoord={+flw,+flen};v.normal={fnx,fny,0};v.vertex={fx2,fy2,+ndx,+ndy};pts.push_back(v);
    v.texcoord={+flw,-flen};v.normal={fnx,fny,0};v.vertex={fx1,fy1,-ndx,-ndy};pts.push_back(v);
    v.texcoord={-flw,+flen};v.normal={fnx,fny,0};v.vertex={fx2,fy2,+ndx,+ndy};pts.push_back(v);
    
   
    }
  }



void test_lines_shader()
  {
  auto&f=*cf;
  
  
  glMatrixMode(GL_MODELVIEW); 
  glPushMatrix(); 
  double lw=1.5;
  glLineWidth(lw);
  double scale=1;
  glScaled(scale/f.da_sx,scale/f.da_sy,1);
  
  
  //glLineWidth(1);glColor4d(1,1,1,1);
  //glBegin(GL_LINES);
  //for(int q1=-5;q1<=20;q1++){glVertex2d(-5,q1);glVertex2d(20,q1);}
  //for(int q1=-5;q1<=20;q1++){glVertex2d(q1,-5);glVertex2d(q1,20);}
  //glEnd();
  //
  //int num=9;
  //auto glfb=GL_FRONT_AND_BACK;
  ////glUseProgram(lshader);
  //
  //glPolygonMode(glfb,GL_FILL);glColor4d(1,0,0,1);draw_number_single(num);
  //glPolygonMode(glfb,GL_LINE);glColor4d(1,1,0,1);draw_number_single(num);
  //glPolygonMode(glfb,GL_FILL);glColor4d(0,0,1,1);draw_triangulate(num,1);
  //glPolygonMode(glfb,GL_LINE);glColor4d(0,1,1,1);draw_triangulate(num,1);
  //glLineWidth(4);glColor4d(0,1,0,1);draw_number_singleline(num);
  //
  //
  //glTranslated(22,0,0);
  ////glScaled(0.02,0.02,1);
  //glPolygonMode(glfb,GL_FILL);glColor4d(1,0,1,1);draw_triangulate2(num,1/scale); 
  
  glColor4d(0,1,0,1);
  glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
  int n=24;
  std::vector < std::vector<double> > lines;
  for(int q1=0;q1<n;q1++)
  //for(auto q1:VI{0,14,16,30,45})
    {
    std::vector<double> e1;
    double rad=350;
    e1.push_back(80+rad*cos((double)q1/n*M_PI));
    e1.push_back(80+rad*sin((double)q1/n*M_PI));
    e1.push_back(80-rad*cos((double)q1/n*M_PI));
    e1.push_back(80-rad*sin((double)q1/n*M_PI));
    e1.push_back(1);
    
    lines.push_back(e1);
    }
  
  n=50;
  for(int q1=0;q1<n;q1++)
    {
    std::vector<double> e1;
    e1.push_back(320+q1*10+0.1*q1);
    e1.push_back(20+0);
    e1.push_back(320+q1*10+0.1*q1);
    e1.push_back(20-100);
    e1.push_back(1);
    
    lines.push_back(e1);
    }
  
  //double ss=8;
  //for(int q1=0;q1<12;q1++)
  //  {
  //  glTranslated(ss*q1,0,0);
  //  draw_triangulate2(q1,ss);
  //  glTranslated(-ss*q1,0,0);
  //  }
  
  //shaderlines(lines);
  
  
  //glEnable(GL_LINE_SMOOTH);
  //glLineWidth(0.5);
  //glBegin(GL_LINES);
  //for(int q1=0;q1<lines.size();q1++)
  //  {
  //  glVertex2d(lines[q1][0],lines[q1][1]+300);
  //  glVertex2d(lines[q1][2],lines[q1][3]+300);
  //  }
  //glEnd();
  
  glPopMatrix();
  
  }


void scales_win_nodl(int win)
  {
  //FrameInfo*curframe=cf;
  FrameInfo&f=*cf;
  WindowInfo&w=windows[win];
  auto&pts=w.pts;
  
  double h=w.pos_top-w.pos_bottom;
  double a=w.bottom();
  double b=w.top();
  
  //printf("%d  %lf %lf\n",w.curtab,a,b);
  
  double R,G,B;
  R=w.r;
  G=w.g;
  B=w.b;
  R=R+(1.0-R)*0.3;
  G=G+(1.0-G)*0.3;
  B=B+(1.0-B)*0.3;
  
  
  if(w.reconfigured)
    {
    w.reconfigured=0;
    
    //printf("SCALES WIN NODL %d(%s): %lf %lf\n",win,w.name.c_str(),a,b);
    
    int om=0;
    
    if(use_dynamic_range && (!w.logsc))
      {
      while((b-a)>100.0){b/=1000;a/=1000;om+=3;}
      while((b-a)<0.1){b*=1000;a*=1000;om-=3;}
      }
    
    ScaleInfo scale;
    
    
    if(w.logsc)scale=log_scale_construct(a,b,f.textsize,(double)f.lsizey*h,f.textratio);  
    else       scale=    scale_construct(a,b,f.textsize,(double)f.lsizey*h,f.textratio);  
    
    
    //for(auto&p:scale.points)p.label*=3.3;
    
    pts.clear();
    std::vector < std::vector<double> > lines;
    
    //maint.start(21);// drawnum
    for(auto&line:scale.lines)lines.push_back({1/f.da_sx,(line.pos-a)/(b-a)*h,8.*(line.size+1)/f.da_sx,(line.pos-a)/(b-a)*h,0.8});
    lines.push_back({0,(scale.lines[0].pos-a)/(b-a)*h,0,(scale.lines.back().pos-a)/(b-a)*h,1});
    for(auto&line:scale.lines)lines.push_back({1/f.da_sx,(line.pos-a)/(b-a)*h,1,(line.pos-a)/(b-a)*h,(line.size+1)/60.0});
    
    if(om)draw_number2v(lines,(double)om,0,f.textsize*2,1,h*(1-1.5*f.textsize/f.da_sy),0,0,0,f.textsize/8);
    if(draw_curtab)draw_number2v(lines,(double)w.curtab,0,f.textsize*2,1,h*(1-8*f.textsize/f.da_sy),0,0,0,f.textsize/8);
    for(auto&p:scale.points)draw_number2v(lines,p.label,scale.dec,f.textsize+p.size*8.0,-1/f.da_sx,(p.pos-a)/(b-a)*h,0,0,0,f.textsize/12);
    //maint.stop(21);// drawnum 
    
    
    
    
    for(size_t q1=0;q1<lines.size();q1++)
      {
      lines[q1][0]*=f.da_sx;
      lines[q1][2]*=f.da_sx;
      lines[q1][1]*=f.da_sy;
      lines[q1][3]*=f.da_sy;
      }
    
    pts.clear();
    //maint.start(21);// drawnum
    shaderlines(lines,pts);
    //maint.stop(21);// drawnum
    
    //printf("BEFORE: %s\n",gluErrorString(glGetError()));
    
    
    maint.start(24);
    w.vbo.Reinitialise(pangolin::GlArrayBuffer,pts.size(),GL_FLOAT,sizeof(pts[0])/sizeof(float),GL_DYNAMIC_DRAW);
    w.vbo.Upload(pts.data(),w.vbo.SizeBytes());
    maint.stop(24);
    
    //printf("VBO: %lf\n",maint(24));
    }
  
  
  
  
  
  maint.start(20);// drawnum2
  glMatrixMode(GL_MODELVIEW); 
  glPushMatrix(); 
  glScaled(1/f.da_sx,1/f.da_sy,1);
  
  line_shader.Bind();
  
  w.vbo.Bind();
  
  for(auto&i:attribpos)glEnableVertexAttribArray(i.second);
  glVertexAttribPointer(attribpos["vertex"]  ,4,GL_FLOAT,0,sizeof(OneVertex),(void*)0 );
  glVertexAttribPointer(attribpos["normal"]  ,3,GL_FLOAT,0,sizeof(OneVertex),(void*)16);
  glVertexAttribPointer(attribpos["texcoord"],2,GL_FLOAT,0,sizeof(OneVertex),(void*)28);
  
  totallinepts+=pts.size();
  
  glColor4d(R,G,B,1);
  maint.start(21);// drawnum
  glDrawArrays(GL_TRIANGLES,0,pts.size());
  maint.stop(21);// drawnum
  
  for(auto&i:attribpos)glDisableVertexAttribArray(i.second);
  
  line_shader.Unbind();
  
  w.vbo.Unbind();
  
  //if(flag)printf("VA: %lf\n",maint(21));
  
  
  glPopMatrix(); 
  
  maint.stop(20);// drawnum2
  
  
  //test_lines_shader();
  
  }



void construct_timescale_nodl(double a,double b)
  {
  auto&f=*cf;
  auto&pts=f.pts;
  
  if(f.scalea!=a || f.scaleb!=b || f.reconfigured)
    {
    //printf("%s: %lf(%lf) %lf(%lf)  %d\n",f.name.c_str(),f.scalea,a,f.scaleb,b,f.reconfigured);
    
    f.reconfigured=0;
    f.scalea=a;
    f.scaleb=b;
    
    ScaleInfo scale=hscale_construct(a,b,f.textsize*10.0/16.0,f.da_sx,f.textratio);
    
    std::vector < std::vector<double> > lines;
    
    for(auto&p:scale.points)draw_number2v(lines,p.label,scale.dec,f.textsize+p.size*4.0,(p.pos-a)/(b-a),-0.5/f.da_sy,1,0,0,f.textsize/12);
    
   for(auto&line:scale.lines)
      lines.push_back({(line.pos-a)/(b-a),1/f.da_sy,(line.pos-a)/(b-a),5*(line.size+1)/f.da_sy,0.8});
    
    lines.push_back({(scale.lines[0].pos-a)/(b-a),0.0,(scale.lines.back().pos-a)/(b-a),0.0,1});
    
    for(auto&line:scale.lines)
      lines.push_back({(line.pos-a)/(b-a),0.0,(line.pos-a)/(b-a),1.0,(line.size+1)/60.0});
    
    for(size_t q1=0;q1<lines.size();q1++)
      {
      lines[q1][0]*=f.da_sx;
      lines[q1][2]*=f.da_sx;
      lines[q1][1]*=f.da_sy;
      lines[q1][3]*=f.da_sy;
      }
    
    pts.clear();
    shaderlines(lines,pts);
    
    //printf("BEFORE: %s\n",gluErrorString(glGetError()));
    
    maint.start(24);
    f.vbo.Reinitialise(pangolin::GlArrayBuffer,pts.size(),GL_FLOAT,sizeof(pts[0])/sizeof(float),GL_DYNAMIC_DRAW);
    f.vbo.Upload(pts.data(),f.vbo.SizeBytes());
    maint.stop(24);
    
    //printf("VBO: %lf\n",maint(24));
    }
    
  
  maint.start(20);// drawnum2
  glMatrixMode(GL_MODELVIEW); 
  glPushMatrix(); 
  glScaled(1/f.da_sx,1/f.da_sy,1);
  
  line_shader.Bind();
  
  //glBindBuffer(GL_ARRAY_BUFFER,f.vbo);
  f.vbo.Bind();
  
  for(auto&i:attribpos)glEnableVertexAttribArray(i.second);
  glVertexAttribPointer(attribpos["vertex"]  ,4,GL_FLOAT,0,sizeof(OneVertex),(void*)0);
  glVertexAttribPointer(attribpos["normal"]  ,3,GL_FLOAT,0,sizeof(OneVertex),(void*)16);
  glVertexAttribPointer(attribpos["texcoord"],2,GL_FLOAT,0,sizeof(OneVertex),(void*)28);
  
  totallinepts+=pts.size();
  
  glColor4dv(fg_col);
  maint.start(21);// drawnum
  glDrawArrays(GL_TRIANGLES,0,pts.size());
  maint.stop(21);// drawnum
  
  for(auto&i:attribpos)glDisableVertexAttribArray(i.second);
  
  line_shader.Unbind();
  f.vbo.Unbind();
  
  //if(flag)printf("VA: %lf\n",maint(21));
  
  glPopMatrix(); 
  
  maint.stop(20);// drawnum2
  }


void render1(FrameInfo*curf)
  {
  //TIME(2,curf->name);
  
  cf=curf;
  auto&f=*cf;
  
  f.lsizex=sizex*(f.x2-f.x1);
  f.lsizey=sizey*(f.y2-f.y1);
  
  f.da_sx=f.lsizex/(1.0+f.ml+f.mr);
  f.da_sy=f.lsizey/(1.0+f.mt+f.mb);
  f.da_xc=f.lsizex/((1.0+f.ml+f.mr)/f.ml)+f.x1*sizex;
  f.da_yc=f.lsizey/((1.0+f.mt+f.mb)/f.mb)+f.y1*sizey;
  f.da_uyc=f.lsizey/((1.0+f.mt+f.mb)/f.mt)+f.y1*sizey;
  
  
  
  glMatrixMode (GL_MODELVIEW);
    
  glPushMatrix();
  glTranslated(f.x1,f.y1,0); 
  glScaled(f.x2-f.x1,f.y2-f.y1,1);  
  glScaled(1/(1+f.mr+f.ml),1/(1+f.mb+f.mt),1);
  glTranslated(f.ml,f.mb,0); 
  
  
  
  if(displayfonts)
  for(auto&i:f.Images)
    {
    auto& im=i.second;
    auto& image=im.image;
    
    glEnable(GL_TEXTURE_2D);
    im.ensure_texture(im.desired_size);
    
    double dx=im.rendered_size*image.w/image.h;
    double dy=im.rendered_size;
    
    double a=im.angle*M_PI/180;
    
    im.tex.Bind();
    
    std::vector<double> e1={0,0,1,0,1,1,0,1};
    //VD e2={-dx/2,-dy/2,+dx/2,-dy/2,+dx/2,+dy/2,-dx/2,+dy/2};
    std::vector<double> e2={0,0,dx,0,dx,dy,0,dy};
    for(int q1=0;q1<8;q1+=2)e2[q1]-=dx/2;
    for(int q1=1;q1<8;q1+=2)e2[q1]-=dy/2;
    for(auto&e3:e2)e1.push_back(e3);
    
    
    for(int q1=0;q1<4;q1++)
      {
      double x1,y1,x2,y2;
      x1=e1[8+q1*2+0];
      y1=e1[8+q1*2+1];
      x2=cos(a)*x1-sin(a)*y1;
      y2=sin(a)*x1+cos(a)*y1;
      
      x2=im.framex+x2;
      y2=im.framey+y2;
      
      e1[8+q1*2+0]=im.framex+x2/f.da_sx;
      e1[8+q1*2+1]=im.framey+y2/f.da_sy;
      }
    
    glColor3d(im.r,im.g,im.b);
    glBegin(GL_QUADS);
    for(int q1=0;q1<4;q1++){glTexCoord2d(e1[q1*2+0],e1[q1*2+1]);glVertex3d(e1[8+q1*2+0],e1[8+q1*2+1],0);}
    glEnd();
    
    glDisable(GL_TEXTURE_2D);
    } 
  
  
  if(f.mouse.inside)
    {
    double delta=0;
    if(fw_motion.t(0)<fw_motion.motion_time) { delta-=0.1*f.timespan*fw_motion.t(4)/fw_motion.motion_time; fw_motion.t.start(4); }
    if(fw_motion.t(1)<fw_motion.motion_time) { delta+=0.1*f.timespan*fw_motion.t(5)/fw_motion.motion_time; fw_motion.t.start(5); }
    f.endtime+=delta;
    //printf("%lf %lf\n",f.endtime,delta);
    }
  
  double rendertime;
  if(f.mode==0||f.mode==3)rendertime=f.endtime;
  else if(f.mode==1||f.mode==2)rendertime=f.lasttime;
  else {printf("Invalid frame mode\n");rendertime=f.lasttime;}
  
  if(f.mode==1||f.mode==2)f.endtime=rendertime;
  
  double timespan=f.timespan;
  if(timespan<1e-6)timespan=1e-6;
  double starttime=rendertime-timespan;
  
  //printf("%lf %lf %lf\n",rendertime,timespan,starttime);
  
  
  double endt;
  if(f.mode==0||f.mode==1||f.mode==3)endt=rendertime;
  else if(f.mode==2)endt=0;
  else {printf("Invalid frame mode\n");endt=0;}
  
  maint.start(34);// mscale
  if(displaylists)construct_timescale_nodl(endt-timespan,endt);
  maint.stop(34);// mscale
  
  for(auto&w:f.windows)
    {
    glMatrixMode(GL_MODELVIEW);
    
    WindowInfo& win=windows[w];
    
    if(win.mouse.inside)
      {
      double delta=0;
      double size=win.top()-win.bottom();
      if(fw_motion.t(2)<fw_motion.motion_time) { delta-=0.1*size*fw_motion.t(6)/fw_motion.motion_time; fw_motion.t.start(6); }
      if(fw_motion.t(3)<fw_motion.motion_time) { delta+=0.1*size*fw_motion.t(7)/fw_motion.motion_time; fw_motion.t.start(7); }
      win.bottom()+=delta;
      win.top   ()+=delta;
      win.reconfigured=true;
      //printf("%lf %lf\n",f.endtime,delta);
      }
    
    
    maint.start(39);// mnmxft
    for(auto&c:win.channels)if(channels[c].active)
      for(auto&s:channels[c].data)
        s.findtime(starttime,rendertime,channels[c].samplesperpixel,f.da_sx);

    if(win.autorange)findminmax(win);
    maint.stop(39);// mnmxft
    
    if(win.reconfigured)
      {
      win.r=win.g=win.b=0;
      for(auto&c:win.channels)
        {
        win.r+=channels[c].style.r/win.channels.size();
        win.g+=channels[c].style.g/win.channels.size();
        win.b+=channels[c].style.b/win.channels.size();
        }
      if(f.win_basic_color){win.r=1-bg_col[0];win.g=1-bg_col[1];win.b=1-bg_col[2];}
      }
    
    
    glPushMatrix();
    glTranslated(0.0,win.pos_bottom, 0.0); 
    
    maint.start(40);// callchan
    if(displaylists)scales_win_nodl(w);
    maint.stop(40);// callchan
    
    glPopMatrix();
      
    
    
    //do_average(q1,level,c1,c2);
    
    double height=win.top()-win.bottom();
    double windowheight=win.pos_top-win.pos_bottom;
    
    int x1=(int)f.da_xc;
    int x2=(int)(f.da_sx+f.da_xc);
    int y1=(int)(f.da_yc+f.da_sy*win.pos_bottom);
    int y2=(int)(f.da_yc+f.da_sy*win.pos_top);
    
    glEnable(GL_SCISSOR_TEST);
    glScissor(x1+drawing_area->vp.l,y1+drawing_area->vp.b,x2-x1,y2-y1);
    
    //printf("%d %d %d %d\n",x1,x2,y1,y2);
    
    
    glPushMatrix();
    glTranslated(0.0,win.pos_bottom, 0.0); 
    
    auto xx=[&]()
      {
      maint.start(35);// font
      glEnable(GL_TEXTURE_2D);
      
      //double ts=f.textsize*0.66;
      double ts=f.textsize*f.labelratio;
      
      for(auto&c:win.channels)if(channels[c].active&&channels[c].wintab==win.curtab)if(channels[c].label!="")
        for(auto&s:channels[c].data)if(chanselect==&s)
        {
        //printf("%lf %lf\n",f.mouse.x,f.mouse.y);
        double x1=f.mouse.x*sizex*(f.x2-f.x1)+ts;
        double y1=f.mouse.y*sizey*(f.y2-f.y1)-f.da_sy*win.pos_bottom+ts*2;
        //double y1=f.mouse.y*sizey*(f.y2-f.y1)+ts*2;
        double x2=x1+(ts*2)*channels[c].im_label.image.w/channels[c].im_label.image.h;
        double y2=y1+(ts*2);
        
        channels[c].im_label.ensure_texture(ts*2);
        channels[c].im_label.tex.Bind();
        
        double xx2=x2/sizex/(f.x2-f.x1),yy1=y1/sizey/(f.y2-f.y1);
        double xx1=x1/sizex/(f.x2-f.x1),yy2=y2/sizey/(f.y2-f.y1);
        
        
        //if(chanselect)printf("%s\n",chanselect->parent->name.c_str());
        //printf("%lf %lf %lf\n",win.pos_bottom,win.pos_top,windowheight);
        //printf("%lf %lf %lf %lf\n",x1,x2,y1,y2);
        //printf("%lf %lf %lf %lf\n",xx1,xx2,yy1,yy2);
        //printf("%zu %zu\n",channels[c].im_label.image.w,channels[c].im_label.image.h);
        
        maint.start(36);// font2
        
        glColor3d(1-0.5*(1-channels[c].style.r),1-0.5*(1-channels[c].style.g),1-0.5*(1-channels[c].style.b));
        
        glBegin(GL_QUADS);
        
        //glTexCoord2d(0,0); glVertex3d(0.98-xx2+xx1,yy1*(1-0)+yy2*0,0);
        //glTexCoord2d(1,0); glVertex3d(0.98-xx2+xx2,yy1*(1-0)+yy2*0,0);
        //glTexCoord2d(1,1); glVertex3d(0.98-xx2+xx2,yy1*(1-1)+yy2*1,0);
        //glTexCoord2d(0,1); glVertex3d(0.98-xx2+xx1,yy1*(1-1)+yy2*1,0);
         
        glTexCoord2d(0,0); glVertex2d(xx1,yy1*(1-0)+yy2*0);
        glTexCoord2d(1,0); glVertex2d(xx2,yy1*(1-0)+yy2*0);
        glTexCoord2d(1,1); glVertex2d(xx2,yy1*(1-1)+yy2*1);
        glTexCoord2d(0,1); glVertex2d(xx1,yy1*(1-1)+yy2*1);
        
        glEnd();
        break;
        }
      
      
      double x1=ts/2;
      double y1=windowheight*sizey*(f.y2-f.y1)-ts*2.5;
      
      y1-=windowheight*sizey*(f.y2-f.y1)*f.offsetlabel;
      
      if(win.names)for(auto&c:win.channels)if(channels[c].active && channels[c].wintab==win.curtab)if(channels[c].displayname)
        {
        double x2=x1+(ts*2.0)*channels[c].im_name.image.w/channels[c].im_name.image.h;
        double y2=y1+(ts*2.0);
        
        channels[c].im_name.ensure_texture(ts*2);
        channels[c].im_name.tex.Bind();
        
        double xx2=x2/sizex/(f.x2-f.x1);
        double xx1=x1/sizex/(f.x2-f.x1);
        double yy1=y1/sizey/(f.y2-f.y1);
        double yy2=y2/sizey/(f.y2-f.y1);
        
        maint.start(36);// font2
        
        glColor3d(channels[c].style.r,channels[c].style.g,channels[c].style.b);
        
        glBegin(GL_QUADS);
        
        if(f.right_label)
          {
          glTexCoord2d(0,0); glVertex2d(0.98-xx2+xx1,yy1*(1-0)+yy2*0);
          glTexCoord2d(1,0); glVertex2d(0.98-xx2+xx2,yy1*(1-0)+yy2*0);
          glTexCoord2d(1,1); glVertex2d(0.98-xx2+xx2,yy1*(1-1)+yy2*1);
          glTexCoord2d(0,1); glVertex2d(0.98-xx2+xx1,yy1*(1-1)+yy2*1);
          }
        else
          {
          glTexCoord2d(0,0); glVertex2d(xx1,yy1*(1-0)+yy2*0);
          glTexCoord2d(1,0); glVertex2d(xx2,yy1*(1-0)+yy2*0);
          glTexCoord2d(1,1); glVertex2d(xx2,yy1*(1-1)+yy2*1);
          glTexCoord2d(0,1); glVertex2d(xx1,yy1*(1-1)+yy2*1);
          }
        
        glEnd();
        if(auto c1=glGetError();c1)printf("ERROR: LINE %d %u\n",__LINE__,c1);
        
        maint.stop(36);// font2
        y1-=ts*2.0;
        }
      glDisable(GL_TEXTURE_2D);
      maint.stop(35);// font
      };
    
    glPushMatrix();
    glScaled(1.0/timespan,1.0,1.0);
    
    glScaled(1.0,windowheight/height,1.0);
    glTranslated(0.0,-win.bottom(), 0.0); 
    
      
    
    //TIME(1);
    //if(0)
    for(auto&c:win.channels)for(auto&s:channels[c].data)
      {
      //TIME(2);
      if(!channels[c].active)continue;
      if(channels[c].wintab!=win.curtab)continue;
      if(s.data.size()==0)continue;
      if(s.data[0].t>rendertime)continue;
      if(s.data.back().t<starttime)continue;
      
      const ChanInfo& chan=channels[c];
      ChanInfo& chan_m=channels[c];
      
      if(vbos.count(&s)==0)vbos[&s]=std::make_unique<pangolin::GlBuffer>(pangolin::GlArrayBuffer,0,GL_FLOAT,2,GL_DYNAMIC_DRAW);
      pangolin::GlBuffer& vbo=*vbos[&s];
      
      int c1=s.c1;
      int c2=s.c2;
      
      int stride =s.stride;
      int toprint=s.toprint;
      
      
      std::vector<float> va;
      
      
      if(chan.style.style==2)
        {
        
        
        if(starttime!=s.vastart || vbo.num_elements==0)
          {
          s.vastart=starttime;
          
          for(int q1=0;q1<(int)s.data.size()-1;q1++)
            {
            va.push_back(-starttime + s.data[q1].t);   va.push_back(0.0);
            va.push_back(-starttime + s.data[q1+1].t); va.push_back(0.0);
            va.push_back(-starttime + s.data[q1+1].t); va.push_back(s.data[q1].x);
            va.push_back(-starttime + s.data[q1].t);   va.push_back(s.data[q1].x);
            }
          
          vbo.Reinitialise(pangolin::GlArrayBuffer,va.size()/2,GL_FLOAT,2,GL_DYNAMIC_DRAW,(unsigned char*)va.data());
          
          }
        
        
        glColor4d(chan.style.r,chan.style.g,chan.style.b,chan.style.a);
        pangolin::RenderVbo(vbo,GL_QUADS);
        
        
        continue;
        }
      
      //glColor3d(chan.style.r,chan.style.g,chan.style.b);
      
      
      
      maint.start(38);// prepdata
      if(starttime!=s.vastart || toprint!=(int)vbo.num_elements)
        {
        //TIME(1);
        //printf("%s %d\n",chan.name.c_str(),toprint);
        s.vastart=starttime;
        va.reserve(toprint*2);
        
        double*src=(double*)(&(s.data[c1]));
        
        for(int q2=0,q3=0;q2<=c2-c1;q2+=stride,q3++)
          {
          va.push_back(src[q2*2]-starttime);
          va.push_back(src[q2*2+1]+0);
          }
        
        
        vbo.Reinitialise(pangolin::GlArrayBuffer,va.size()/2,GL_FLOAT,2,GL_DYNAMIC_DRAW,(unsigned char*)va.data());
        //vbo.Resize(va.size()/2);
        //vbo.Upload(va.data(),vbo.SizeBytes());
        }
      maint.stop(38);// prepdata
      
      //printf("%25s: %d\n",chan.name.c_str(),toprint);
      
      
      
      double alpha=1;
      
      
      
      if(chan.style.style==0)
        {
        alpha=chan.style.a;
        glLineWidth(std::min(10.0,chan.style.width));
        glPointSize(std::min(10.0,chan.style.width));
        }
      if(chan.style.style==1)
        {
        glLineWidth(chan.style.width/1.5);
        glPointSize(chan.style.width);
        alpha=s.alpha*chan.style.a;
        if(!chan.showshadow)alpha=0;
        }
      if(chan.style.style==1 && chanselect==&s && chan.showshadow)
        {
        glLineWidth(chan.style.width*1.5+2);
        glPointSize(chan.style.width*1.5+3);
        alpha=0.9*chan.style.a;
        //printf("%s\n",chan.label.c_str());
        }
        
      totalprint+=toprint;
      
      maint.start(37);// data2gpu
      if(chan.style.style==0||alpha!=0)
        {
        glColor4d(chan.style.r,chan.style.g,chan.style.b,alpha);
        pangolin::RenderVbo(vbo,GL_LINE_STRIP);
        //printf("%d alpha: %lf\n",c,alpha);
        }
      if(chan.style.style==1)
        {
        glColor4d(chan.style.r,chan.style.g,chan.style.b,chan.style.a);
        
        if(shaderuse)glUseProgram(point_shader.ProgramId());else glUseProgram(0);
        pangolin::RenderVbo(vbo,GL_POINTS);
        glUseProgram(0);
        
        }
      maint.stop(37);// data2gpu
      
      
      if(chan.data2.data.size())
        {
        auto& d=chan_m.data2;
        auto& d1=chan_m.data[0];
        
        if(d.fixtex)//if(0)
          {
          //TIME(1);
          while(d.totalfill*d.h*3<(int)d.data.size())
            {
            int texfill=d.totalfill%d.maxtexture;
            if(texfill==0)d.tex.emplace_back(d.maxtexture,d.h,GL_RGB32F,false,0,GL_RGB,GL_FLOAT);
            
            size_t len=d.data.size()-d.totalfill*d.h*3;
            len=std::min(len,(size_t)d.maxtexture*d.h*3-texfill*3*d.h);
            assert(len>0);
            size_t width=len/3/d.h;
            size_t addr=d.totalfill*d.h*3;
            //printf("%zu   %zu %d   %zu %d %zu  %zu  %d %d\n",len,  w,d.h ,d.tex.size(),d.maxtexture,d.data.size(),addr,texfill,d.totalfill);
            for(size_t q2=0;q2<width;q2++)d.tex.back().Upload(d.data.data()+addr+d.h*3*q2,texfill+q2,0,1,d.h,GL_RGB,GL_FLOAT);
            //d.tex.emplace_back(1,1,GL_RGB32F,false,0,GL_RGB,GL_FLOAT,d.data.data()+addr);
            //pangolin::PixelFormatFromString
            d.totalfill+=width;
            }
          //chan_m.data2.gltex.Load(chan.data2.image);
          d.fixtex=false;
          }
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        
        for(size_t q1=0;q1<d.tex.size();q1++)
          {
          //TIME(1);
          double ww=std::min(d.maxtexture,d.totalfill-(int)q1*d.maxtexture)/(double)d.maxtexture;
          
          glEnable(GL_TEXTURE_2D);
          glBindTexture(GL_TEXTURE_2D,d.tex[q1].tid);
          glColor3d(1,1,1);
          glBegin(GL_QUADS);
          //float t1=q1*d.maxtexture*d.dt;
          //float t2=t1+d.dt*w*d.maxtexture;
          float t1=d1.data[0].t + q1*d.dt*d.maxtexture;  
          float t2=          t1 + ww*d.dt*d.maxtexture;
          //printf("==tex== %zu %lf %lf   %lf    %d\n",q1,t1,t2,w,d.totalfill-(int)q1*d.maxtexture);
          glTexCoord2d(0,0); glVertex2f(t1-starttime,d.x1);
          glTexCoord2d(ww,0); glVertex2f(t2-starttime,d.x1);
          glTexCoord2d(ww,1); glVertex2f(t2-starttime,d.x2);
          glTexCoord2d(0,1); glVertex2f(t1-starttime,d.x2);
          glEnd();
          glDisable(GL_TEXTURE_2D);
          }
        }
      
      }
    
    glPopMatrix();
    
    if(displayfonts)xx();
    
    glPopMatrix();
    glDisable(GL_SCISSOR_TEST);
    } 
  
  maint.start(42);// mousedraw
  if(displaylists)if(f.mouse.inside&&f.mousedraw)
    {
    glLineWidth(1.5);
    glBegin(GL_LINES);
    glColor4dv(fg_col);
    glVertex2d(f.mouse.x,-f.mb);
    glVertex2d(f.mouse.x,1+f.mt);
    glVertex2d(-f.ml,f.mouse.y);
    glVertex2d(1+f.mr,f.mouse.y);
    glEnd();
    }
  maint.stop(42);// mousedraw
  
  glPopMatrix();
  
  }

double fps=0.0,num_frames=0.0;
void render()
  {  
  maint.start(11);
  maint.start(10);
  
  //int iconified=glfwGetWindowAttrib(window, GLFW_ICONIFIED);
  //if(iconified!=iconify)if(iconify==1)glfwIconifyWindow(window);
  //if(iconified!=iconify)if(iconify==0)glfwRestoreWindow(window);
  
  
  configdata.lock();
  
  if(!point_shader.Valid())doshaders();
  
  
  glLoadIdentity();
  
  
  glOrtho(0.0,1.0,0.0,1.0,-1.0,1.0);
  //glOrtho(0.sin(maint())*0.5,1.0,0.0,1.0,-1.0,1.0);
  
  
  
  if(size_request){size_request=0;pango_window->Resize(size_request_x,size_request_y);}
  
  auto& vp=drawing_area->vp;
  
  if(vp.w!=sizex || vp.h!=sizey)
    {
    sizex=vp.w;
    sizey=vp.h;
    if(size_request_x==0)size_request_x=sizex;
    if(size_request_y==0)size_request_y=sizey;
    for(auto&w:windows)w.reconfigured=1;
    for(auto&f:frames)f.reconfigured=1;
    }
  
  //glViewport(100,200,sizex*4/5,sizey*2/3);
  //printf("%d %d %d %d\n",vp.l,vp.b,vp.w,vp.h);
  glViewport(vp.l,vp.b,vp.w,vp.h);
  
  glClearColor(bg_col[0],bg_col[1],bg_col[2],bg_col[3]);
  glClear(GL_COLOR_BUFFER_BIT);
  
  maint.start(12);
  
  maint.stop(10);
   
  
  
  glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
  //glHint(GL_LINE_SMOOTH,GL_NICEST);
  //glHint(GL_POINT_SMOOTH,GL_NICEST);
  //glHint(GL_POLYGON_SMOOTH,GL_NICEST);
  glEnable(GL_BLEND);
  //glEnable(GL_LINE_SMOOTH);
  //glEnable(GL_POINT_SMOOTH);
  //glEnable(GL_POLYGON_SMOOTH);
  glEnable(GL_POINT_SPRITE);
  
  if(shaderuse)glEnable(GL_PROGRAM_POINT_SIZE);
  else glDisable(GL_PROGRAM_POINT_SIZE);
  
  
  
  
  
  findlasttimes();
  
  
  //glGetQueryObjectuiv(query[0], GL_QUERY_RESULT, &queryres[0]);
  //glBeginQuery(GL_TIME_ELAPSED, query[0]);
  for(auto&i:uframes)if(frames[i.second].active)render1(&frames[i.second]);
  //glEndQuery(GL_TIME_ELAPSED);
  
  
  //printf("%u\n",queryres[0]);
  
  //int c1;
  //glGetIntegerv(GL_samples,&c1); 
  //printf("%d\n",c1);
   
  //if(displaylists)
  
  glColor4d(1,1,1,1);
  //if(maint(9)>1.0)printf("%lf\n",frames);
  if(displaylists)draw_number2(num_frames,0,8,32.0/sizex,(8+2)/2.0/sizey,0,1,1,1);
  fps+=1.0;if(maint(9)>1.0){num_frames=fps;fps=0.0;maint.start(9);}
  
  maint.stop(12);
  
  
  
  
  while(screenshot.take)
    {
    if(screenshot.precise)if(size_request_x!=sizex || size_request_y!=sizey)break;
    
    glFinish();
    screenshot.take=0;
    screenshot.precise=false;
    
    auto savescreen_shm=[](std::string name, std::vector<unsigned char> im,int x,int y)
      {
      thread_local SharedMemoryOne shm(name,1<<24);
      CommStruct cs(im.size()+2*sizeof(int));
      cs.i[0]=x;
      cs.i[1]=y;
      //printf("%d %d\n",sizex,sizey);
      memcpy(cs.uc+8,im.data(),im.size());
      shm.send2(cs.d,cs.length(),2);
      while(shm.peek())usleep(1000);
      //shm.unlink();
      };
    
    auto savescreen_png_pango=[](std::string file, std::vector<unsigned char> im,int x,int y)
      {
      TIME(1,"TOTAL PANGO PNG");
      pangolin::TypedImage pimg(x,y,pangolin::PixelFormatFromString("RGBA32"));
      TIMEIT(1,"COPY PANGO PNG")
      memcpy(pimg.ptr,im.data(),pimg.SizeBytes());
      TIMEIT(1,"SAVE PANGO PNG")
      pangolin::SaveImage(pimg,file);
      };
    
    auto savescreen_jpg_pango=[](std::string file, std::vector<unsigned char> im,int x,int y)
      {
      TIME(1,"TOTAL PANGO JPEG");
      pangolin::TypedImage pimg(x,y,pangolin::PixelFormatFromString("RGB24"));
      TIMEIT(1,"COPY PANGO JPEG")
      memcpy(pimg.ptr,im.data(),pimg.SizeBytes());
      TIMEIT(1,"SAVE PANGO JPEG")
      pangolin::SaveImage(pimg,file);
      };
    
    
    std::string file,filej,shm;
    if(screenshot.dest.find("shm://")==0)
      {
      shm=screenshot.dest.substr(6);
      }
    else if(screenshot.dest!="")
      {
      fs::path f=screenshot.dest;
           if(f.extension().string()==".jpg")filej=f;
      else if(f.extension().string()==".png")file =f;
      else {filej=f.string()+".jpg";file=f.string()+".png";}
      screenshot.dest=""; 
      }
    
    fs::create_directories("./shots");
    
    fs::create_directories("./shots/png");
    fs::create_directories("./shots/jpg");
    
    if(file=="" && filej=="" && shm=="")
    for(int q1=0;q1<100000;q1++)
      {
      file =sprint("./shots/png/%05d.png",q1);
      filej=sprint("./shots/jpg/%05d.jpg",q1);
      if(file_exists(file))continue;
      else {FILE*fn=fopen(file.c_str(),"wb");fclose(fn);break;}
      }
    
    //int sizex=screenshot.sizex;
    //int sizey=screenshot.sizey;
    
    printf("%d %d\n",sizex,sizey);
    
    std::vector<unsigned char> im2(sizex*sizey*4);
    std::vector<unsigned char> im (sizex*sizey*4);
    std::vector<unsigned char> imj(sizex*sizey*3);
    
    TIMEIT(1,"OPENGL READ")
      {
      glPixelStorei(GL_PACK_ALIGNMENT,1);
      glReadPixels(screenshot.x,screenshot.y,sizex,sizey,GL_RGBA,GL_UNSIGNED_BYTE,im2.data());
      for(int q1=0;q1<sizey;q1++)memcpy(im.data()+sizex*4*q1,im2.data()+sizex*4*(sizey-1-q1),sizex*4);
      //for(int q1=0;q1<sizex*sizey;q1++)im[q1*4+3]=255;
      for(int q1=0;q1<sizey;q1++)for(int q2=0;q2<sizex;q2++)memcpy(imj.data()+(q1*sizex+q2)*3,im.data()+(q1*sizex+q2)*4,3);
      }
    
    if(filej!="")
      {
      std::thread th2(savescreen_jpg_pango,filej,imj,sizex,sizey);
      if(screenshot.blocking)th2.join();else th2.detach();
      }
    if(file!="")
      {
      std::thread th2(savescreen_png_pango,file ,im ,sizex,sizey);
      if(screenshot.blocking)th2.join();else th2.detach();
      }
    if(shm!="")
      {
      std::thread th1(savescreen_shm,shm  ,imj,sizex,sizey);
      if(screenshot.blocking)th1.join();else th1.detach();
      }
    }
  
  
  
  
  
  
   
   
  
  maint.stop(11);
  
  
  
  if(print_stats)if(maint(8)>1)
    {
    maint.start(8);
    printf("prepare: %8.3lf    ",maint(10)*1000);  
    printf("render: %8.3lf    ",maint.acc(12)*1000);   
    //printf("swap: %8.3lf    ",maint(13)*1000); 
    printf("font: %8.3lf   ",maint.acc(35)*1000.0);
    //printf("scales: %8.3lf   ",maint.acc(31)*1000.0);
    printf("mscale: %8.3lf   ",maint.acc(34)*1000.0);
    printf("linedraw: %8.3lf   ",maint.acc(20)*1000.0);
    printf("line2gpu: %8.3lf(%d*%zubytes)   ",maint.acc(21)*1000.0,totallinepts,sizeof(OneVertex)); 
    //printf("endlist: %8.3lf   ",maint.acc(24)*1000.0);
    printf("prepdata: %8.3lf   ",maint.acc(38)*1000.0);
    printf("data2gpu: %8.3lf(%d*8bytes)   ",maint.acc(37)*1000.0,totalprint);
    printf("findtime: %8.3lf   ",maint.acc(39)*1000.0);
    //printf("calltime: %8.3lf   ",maint.acc(41)*1000.0);
    printf("computenum: %8.3lf   ",maint.acc(99)*1000.0);
    printf("\n");
    }
  totalprint=0;
  totallinepts=0;
  
  
  configdata.unlock();
  
  
  maint.start(13);  
  // TO CHANGE TOPANGO
  //glfwSwapBuffers(window);
  maint.stop(13); 
  
  
  
  {
  std::lock_guard lg2(configdata);
  //TIME(1);
  std::unordered_set<ChanInfo::Segment*> ptrs;
  for(auto&i:uframes)for(auto&w:frames[i.second].windows)for(auto&c:windows[w].channels)for(auto&s:channels[c].data)ptrs.insert(&s);
  std::vector<ChanInfo::Segment*> todelete;
  for(auto&e1:vbos)if(ptrs.count(e1.first)==0)todelete.push_back(e1.first);
  for(auto&e1:todelete)vbos.erase(e1);
  }
  
  
  
  }


enum MouseEventType
  {
  SW_GDK_SCROLL,
  SW_GDK_BUTTON_PRESS,
  SW_GDK_BUTTON_RELEASE,
  SW_GDK_2BUTTON_PRESS,
  SW_GDK_ENTER_NOTIFY,
  SW_GDK_LEAVE_NOTIFY,
  SW_GDK_MOTION_NOTIFY
  
  };

struct MouseEventSW
  {
  MouseEventType type;
  unsigned int mods;
  int button;
  int scroll;
  double x,y;
  };


void mouse_event(const MouseEventSW& event)
  {
  int scroll=event.scroll;
  int button=event.button;
  int filtered_mods=event.mods & (~(7u));
  
  //printf("MODS: %d %u\n",filtered_mods,event.mods);
  //if(event.type==SW_GDK_ENTER_NOTIFY)   {printf("Mouse entered    %.0lf %.0lf\n",event.x,event.y);}
  //if(event.type==SW_GDK_LEAVE_NOTIFY)   {printf("Mouse left       %.0lf %.0lf\n",event.x,event.y);}
  //if(event.type==SW_GDK_MOTION_NOTIFY)  {printf("Mouse moved to   %.0lf %.0lf\n",event.x,event.y);}
  //if(event.type==SW_GDK_BUTTON_PRESS)   {printf("button pressed   %.0lf %.0lf %d\n",event.x,event.y,button);}
  //if(event.type==SW_GDK_BUTTON_RELEASE) {printf("button released  %.0lf %.0lf %d\n",event.x,event.y,button);}
  //if(event.type==SW_GDK_2BUTTON_PRESS)  {printf("button doubled   %.0lf %.0lf %d\n",event.x,event.y,button);}
  //if(event.type==SW_GDK_SCROLL)         {printf("mouse scrolled   %d\n",scroll);}
  
  if(event.type==SW_GDK_ENTER_NOTIFY)mi.inside=1;
  if(event.type==SW_GDK_LEAVE_NOTIFY)mi.inside=0;
  if(event.type==SW_GDK_MOTION_NOTIFY){mi.x=event.x/sizex; mi.y=event.y/sizey;}
  if(event.type==SW_GDK_BUTTON_PRESS)if(button<4)
    {
    mi.button[button].x=event.x/sizex;
    mi.button[button].y=event.y/sizey;
    mi.button[button].pressed=1;
    }
  if(event.type==SW_GDK_BUTTON_RELEASE)mi.button[button].pressed=0;
  if(event.type==SW_GDK_2BUTTON_PRESS){}
  if(event.type==SW_GDK_SCROLL){}
   
  //printf("Mouse at: %lf %lf (%s),  buttons: (%d %lf %lf) (%d %lf %lf)\n",mi.x,mi.y,mi.inside?"inside":"outside",mi.button[1].pressed,mi.button[1].x,mi.button[1].y,mi.button[3].pressed,mi.button[3].x,mi.button[3].y);
  
  
  
  if(event.type==SW_GDK_BUTTON_PRESS && button==3 && filtered_mods==0)
    {
    screenshot.take=1;
    screenshot.blocking=false;
    screenshot.sizex=sizex;
    screenshot.sizey=sizey;
    screenshot.x=screenshot.y=0;
    printf("%d %d %d %d\n",screenshot.x,screenshot.y,screenshot.sizex,screenshot.sizey);
    }
  
  configdata.lock();
  
  findlasttimes();
  
  double ratio=7.0/8.0;
  
  for(auto&i:uframes)
    {
    FrameInfo&f=frames[i.second];
    
    for(auto&j:f.windows)windows[j].mouse.inside=false;
    
    if(!f.active)continue;
    
    f.mouse.x=f.mouse.y=-1;
    f.mouse.inside=0;
    
    if(event.type==SW_GDK_LEAVE_NOTIFY)continue;
    
    //printf("\nf.name: %s  %lf %lf\n",f.name.c_str(),mi.x,mi.y);
    
    double x=(mi.x-f.x1)/(f.x2-f.x1);
    double y=(mi.y-f.y1)/(f.y2-f.y1);
    
    //printf("f.name: %s  %lf %lf\n",f.name.c_str(),x,y);
    
    if(!(x>=0&&x<=1&&y>=0&&y<=1))continue;
    
    x=x*(1+f.mr+f.ml)-f.ml;
    y=y*(1+f.mt+f.mb)-f.mb;
    
    //printf("f.name: %s  %lf %lf\n",f.name.c_str(),x,y);
    
    f.mouse.x=x;
    f.mouse.y=y;
    f.mouse.inside=1;
    
    for(auto&j:f.windows)if(y>windows[j].pos_bottom && y<windows[j].pos_top)windows[j].mouse.inside=1;
    
    }
   
  for(auto&i:uframes)
    {
    FrameInfo&f=frames[i.second]; 
    
    if(!f.active)continue;
    
    //printf("mousexy: %.3lf %.3lf \t",mi.x,mi.y);
    
    //printf("framex1y1x2y2: %.2lf %.2lf %.2lf %.2lf \t",f.x1,f.y1,f.x2,f.y2);
    
    double x=(mi.x-f.x1)/(f.x2-f.x1);
    double y=(mi.y-f.y1)/(f.y2-f.y1);
    
    //printf("framexy1: %.3lf %.3lf \t",x,y);
    
    x=x*(1+f.mr+f.ml)-f.ml;
    y=y*(1+f.mt+f.mb)-f.mb;
    
    //printf("framexy2: %.3lf %.3lf \n",x,y);
    
    double a=f.endtime-f.timespan;
    double b=f.endtime;
    
    double c=a+(b-a)*x;
    
    if(f.active && f.mouse.inside)
      {
      CommStruct& cs=comm->ss;
      cs.d[0]=11;
      strncpy(cs.c+32,f.name.c_str(),96);
      cs.data[1]=c;
      //printf("sending %lf\n",c);
      comm->sms.send2(cs.d,128,0);
      }
    
    f.mx=f.my=-1;
    
    if(!(event.type==SW_GDK_SCROLL))if(mi.button[1].x<f.x1||mi.button[1].x>f.x2)continue;
    if(!(event.type==SW_GDK_SCROLL))if(mi.button[1].y<f.y1||mi.button[1].y>f.y2)continue;

    
    if(mi.inside){f.mx=x;f.my=y;}
     
    
    //if(f.mode!=3)continue;
      
      
    if(event.type==SW_GDK_BUTTON_RELEASE)if(button==1)f.drag_x=-1;
    if(event.type==SW_GDK_BUTTON_PRESS)if(x>0 && button==1)
      {
      f.drag_end=f.endtime;
      f.drag_span=f.timespan;
      f.drag_x=x;
      }
    if(event.type==SW_GDK_MOTION_NOTIFY)if(mi.button[1].pressed && f.drag_x!=-1)
      {
      //double b1=f.drag_end-(x-f.drag_x)*f.timespan;
      double b1=f.drag_end-(x-f.drag_x)*f.drag_span;
      //printf("%lf ",b1);
      if(b1>f.lasttime)b1=f.lasttime;
      if(b1-f.drag_span<f.firsttime)b1=f.firsttime+f.drag_span;
      
      //printf("%lf   %lf %lf %lf\n",b1,f.endtime,f.firsttime,f.timespan);
      f.endtime=b1;
      f.reconfigured=1;
      }
    
    if(event.type==SW_GDK_SCROLL)if(x>0) 
      {
      //printf("%s %d %lf                 %lf %lf    %lf %lf\n",f.name.c_str(),f.mouse.inside,(double)x,f.timespan,f.endtime,f.firsttime,f.lasttime);
      if(!f.mouse.inside)continue;
      double r=(scroll==0)?1/ratio:ratio;
      
      double a1=c-(c-a)*r;
      double b1=c+(b-c)*r;
      
      if(x>0.9&&scroll==1){b1=b;a1=b-(b-a)*r;}
      if(x<0.1&&scroll==1){a1=a;b1=a+(b-a)*r;}
      
      //printf("%lf %lf %lf %lf   %lf %lf\n",a,b,c,r,a1,b1);
      
      if(a1>f.lasttime){a1-=b1-f.lasttime;b1=f.lasttime;}
      if(b1<f.firsttime){b1+=f.firsttime-a1;a1=f.firsttime;}
      
      if(a1<f.firsttime)a1=f.firsttime;
      if(b1>f.lasttime)b1=f.lasttime;
      
      f.endtime=b1;
      f.timespan=b1-a1;
      if(f.timespan<1e-4){f.timespan=1e-4;f.endtime+=f.timespan/2;}
      //printf("%lf %lf   %lf %lf       %lf %lf    %lf %lf %lf\n",f.timespan,f.endtime,f.firsttime,f.lasttime,a1,b1,a,b,c);
      f.reconfigured=1;
      }
    //printf("Mouse: %lf %lf\n",x,y);
    if(f.mouse.inside)for(auto&j:f.windows)
      {
      auto&w=windows[j]; 
      
      if(event.type==SW_GDK_SCROLL)
        {
        //printf("x,y: %lf %lf        pos/bot:   %lf %lf\n",x,y,w.pos_bottom,w.pos_top);
        }
        
      if(event.type==SW_GDK_BUTTON_RELEASE)if(button==1)w.drag_y=-1;
      if(event.type==SW_GDK_BUTTON_PRESS)  if(button==1)if(y>w.pos_bottom && y<w.pos_top)
        {
        //printf("DRAG: %lf %lf %d\n", x, y, button);
        w.drag_top=w.top();
        w.drag_bottom=w.bottom();
        w.drag_y =y;
        }
      
      if(event.type==SW_GDK_MOTION_NOTIFY)if(x>0 && y>w.pos_bottom && y<w.pos_top)
        {
        double time=c;
        double value=(y-w.pos_bottom)/(w.pos_top-w.pos_bottom)*(w.top()-w.bottom())+w.bottom();
        double best=1e99;
        ChanInfo::Segment* bestc=nullptr;
        
        for(auto&chan:w.channels)if(channels[chan].active && channels[chan].wintab==w.curtab)for(auto&s:channels[chan].data)
          {
          double c1=s.get_data_at_time(time);
          if(std::fabs(c1-value)<best){best=std::fabs(c1-value);bestc=&s;}
          }
        
        //if(bestc!=-1)printf("%s %lf  %lf\n",channels[bestc].name.c_str(),best/(w.top-w.bottom),best/(w.top-w.bottom)*(w.pos_top-w.pos_bottom)*f.da_sy);
        
        chanselect=nullptr;
        if(bestc)if(std::fabs(best/(w.top()-w.bottom())*(w.pos_top-w.pos_bottom)*f.da_sy)<40)chanselect=bestc;
        
        //printf("%lf %lf   %d \n",time,value,chanselect);
        }
      
      if(event.type==SW_GDK_MOTION_NOTIFY)if(mi.button[1].pressed && w.drag_y!=-1)
        {
        double span=(w.top()-w.bottom())/(w.pos_top-w.pos_bottom);
        
        //printf("%d (%s): %lf %lf     %lf %lf   %lf  %lf",j,w.name.c_str(), y, w.drag_y,   w.bottom, w.top    ,span , (w.pos_top - w.pos_bottom));
        double bnew=w.drag_bottom - span*(y-w.drag_y);
        double tnew=w.drag_top    - span*(y-w.drag_y);
        //printf("\t%lf %lf   %lf \n", bnew,tnew,tnew-bnew);
        
        w.top()=tnew;
        w.bottom()=bnew;
        
        w.reconfigured=1;
        } 
      
      //printf("%d %d %d %d\n",event.type,SW_GDK_BUTTON_PRESS, button, mevent.mods);
      if(event.type==SW_GDK_BUTTON_PRESS && button==3 && (event.mods & pangolin::KeyModifierCtrl))if(y>w.pos_bottom && y<w.pos_top)
        {
        screenshot.take=1;
        screenshot.x=f.x1*sizex;
        screenshot.y=f.da_yc+w.pos_bottom*f.da_sy;
        screenshot.sizex=f.lsizex;
        screenshot.sizey=f.da_sy*(w.pos_top-w.pos_bottom);
        
        //printf("%d %d %d %d\n",screenshot.x,screenshot.y,screenshot.sizex,screenshot.sizey);
        
        }
      
      if(event.type==SW_GDK_2BUTTON_PRESS)if(y>w.pos_bottom && y<w.pos_top)
        {
        findminmax_total(w);
        //printf("%d %lf %lf\n",j,x,y);
        f.endtime=f.lasttime;
        f.timespan=f.lasttime-f.firsttime;
        if(f.timespan<1e-4){f.timespan=1e-4;f.endtime+=f.timespan/2;}
        }
      
      if(event.type==SW_GDK_SCROLL)if(x<0 && y>w.pos_bottom && y<w.pos_top)
        {
        double s=w.top()-w.bottom();
        double r=(scroll==0)?1/ratio:ratio;
        double cursor=(y-w.pos_bottom)/(w.pos_top-w.pos_bottom)*s+w.bottom();
        //findminmax(j);
        
        double u=w.top();
        double l=w.bottom();
        
        //w.top=(u+l)/2+s/2*r;
        //w.bottom=(u+l)/2-s/2*r;
        
        w.top()=cursor+(u-cursor)*r;
        w.bottom()=cursor-(cursor-l)*r;
        
        w.reconfigured=1;
        
        //printf("%s2:   %lf %lf       %lf %lf   \n",w.name.c_str(),w.mx,w.my,w.pos_bottom,w.pos_top);
        
        }
      
      if(event.type==SW_GDK_BUTTON_PRESS)if(button==1){w.mx=x;w.my=y;}
      
      constexpr double eps=1e-7;
      if(0)
      if(event.type==SW_GDK_BUTTON_RELEASE)if(button==1 && w.mx<0 && w.my>w.pos_bottom && w.my<w.pos_top && fabs(w.my-y)>eps)
        {
        double u=w.my;
        double l=y;
        double bottom=w.pos_bottom;
        double top=w.pos_top;
        double tp=w.top();
        double bt=w.bottom();
        
        if(u<l)std::swap(u,l);
        
        double b1=bt+(tp-bt)/(top-bottom)*(l-bottom);
        double t1=bt+(tp-bt)/(top-bottom)*(u-bottom);
        
        //printf("%s1:   %lf %lf       %lf %lf   \n",w.name.c_str(),w.mx,w.my,w.pos_bottom,w.pos_top);
        
        w.top()=t1;
        w.bottom()=b1;
        
        w.reconfigured=1;
        }
      }
    
    
    //for(auto&fi2:f.linked_frames_time)printf("LINKED: %s %S\n",f.name.c_str(),fi2.c_str());
    
    for(auto&fi2:f.linked_frames_time)
      if(int if2=uframes[fi2]; if2)
        {
        frames[if2].endtime=f.endtime;
        frames[if2].timespan=f.timespan;
        frames[if2].mode=f.mode;
        }
    
    
    }
  
  configdata.unlock();
  
  } 


void key_callback(int key, int action, int mods)
  {
  using namespace pangolin;
  configdata.lock();
  
  if(action==1)if(key=='s'){shaderuse=1-shaderuse;printf("New shaderuse: %d\n",shaderuse);}
  if(action==1)if(key=='f'){displayfonts=1-displayfonts;printf("New displayfonts: %d\n",displayfonts);}
  if(action==1)if(key=='l'){displaylists=1-displaylists;printf("New displaylists: %d\n",displaylists);}
  if(action==1)if(key=='v'){usevsync=1-usevsync;printf("New usevsync: %d\n",usevsync);}
  if(action==1)if(key=='p')print_status();
  
  std::vector<int> dirs{PANGO_KEY_LEFT,PANGO_KEY_RIGHT,PANGO_KEY_DOWN,PANGO_KEY_UP};
  for(auto&e1:dirs)e1+=PANGO_SPECIAL;
  
  if(action==1)for(int q1=0;q1<4;q1++)if(key==dirs[q1])
    {
    if(fw_motion.t(q1)>fw_motion.motion_time)fw_motion.t.start(q1+4);
    fw_motion.t.start(q1);
    }
  
  if(action==1)if(mods&2)if(key>='0'&&key<='9')
    {
    for(auto&i:uframes)
      if(FrameInfo&f=frames[i.second]; f.mouse.inside)
        f.mode=key-48;
     
    }
  
  
  if(action==1)if(mods==0)if(key>='0'&&key<='9')for(auto&i:uframes)
    {
    FrameInfo&f=frames[i.second];
    
    if(!f.active)continue;
    
    
    double x=(mi.x-f.x1)/(f.x2-f.x1);
    double y=(mi.y-f.y1)/(f.y2-f.y1);
    
    x=x*(1+f.mr+f.ml)-f.ml;
    y=y*(1+f.mt+f.mb)-f.mb;
    
    if(f.mouse.inside)for(auto&j:f.windows)
      {
      auto&w=windows[j];
      
      if(y>w.pos_bottom && y<w.pos_top) 
        {
        w.curtab=key-48;
        w.reconfigured=1;
        }
      }
    }
  
  
  // BACKSPACE
  if(key==8)clear_all_data();// BACKSPACE
  
  for(auto& i:uframes)
    if(FrameInfo& f=frames[i.second]; f.mouse.inside)
      for(auto&fi2:f.linked_frames_time)
        if(int if2=uframes[fi2]; if2)
          {
          frames[if2].endtime=f.endtime;
          frames[if2].timespan=f.timespan;
          frames[if2].mode=f.mode;
          }
  
  configdata.unlock();
  }



struct InputHandler : public pangolin::Handler
  {
  using View=pangolin::View;
  
  Instance* inst=nullptr;
  Timer mouse_timer;
  MouseEventSW mevent;
  
  static constexpr int CTRL_MOD=1;
  static constexpr int ALT_MOD=2;
  static constexpr int SHIFT_MOD=4;
  
  int key_mods=0;
  
  
  void Keyboard(View&, unsigned char key, int x, int y, bool pressed) override
    {
    //double seconds=mouse_timer();
    //printf("Key: %.9lf %d %d %d %d\n",seconds,key,x,y,pressed);
    
    if(key==pangolin::PANGO_SPECIAL + pangolin::PANGO_KEY_ALT_L  ){if(pressed)key_mods|=ALT_MOD;  else key_mods&=~ALT_MOD  ;}
    if(key==pangolin::PANGO_SPECIAL + pangolin::PANGO_KEY_CTRL_L ){if(pressed)key_mods|=CTRL_MOD; else key_mods&=~CTRL_MOD ;}
    if(key==pangolin::PANGO_SPECIAL + pangolin::PANGO_KEY_SHIFT_L){if(pressed)key_mods|=SHIFT_MOD;else key_mods&=~SHIFT_MOD;}
    
    inst->key_callback(key,pressed,key_mods);
    }
  void Mouse(View&, pangolin::MouseButton button, int x, int y, bool pressed, int button_state) override
    {
    //printf("MouseButton: %d %d %d %d %d\n",(int)button,x,y,pressed,button_state);
    
    mevent.x=x;
    mevent.y=y;
    mevent.mods=button_state;
    
    mevent.type = pressed ? SW_GDK_BUTTON_PRESS : SW_GDK_BUTTON_RELEASE;
    
    if(button==pangolin::MouseButtonLeft)mevent.button=1;
    if(button==pangolin::MouseButtonRight)mevent.button=3;
    
    if(button==pangolin::MouseWheelDown){mevent.type=SW_GDK_SCROLL; mevent.scroll=1; }
    if(button==pangolin::MouseWheelUp  ){mevent.type=SW_GDK_SCROLL; mevent.scroll=0; }
    
    if(pressed)
      {
      if(button==1)if(mouse_timer(button)<0.3)
        {
        mevent.type=SW_GDK_2BUTTON_PRESS;
        inst->mouse_event(mevent);
        }
      mouse_timer.start(button);
      }
    
    if(mevent.type==SW_GDK_SCROLL && !pressed)return;
    
    inst->mouse_event(mevent);
    
    }
  void MouseMotion(View&, int x, int y, int button_state) override
    {
    //printf("MouseMotion: %d %d %d\n",x,y,button_state);
    mevent.x=x;
    mevent.y=y;
    mevent.type=SW_GDK_MOTION_NOTIFY;
    inst->mouse_event(mevent);
    }
  void PassiveMouseMotion(View&, int x, int y, int button_state) override
    {
    //printf("PassiveMouseMotion: %d %d %d\n",x,y,button_state);
    mevent.x=x;
    mevent.y=y;
    mevent.type=SW_GDK_MOTION_NOTIFY;
    inst->mouse_event(mevent);
    }
  void MouseBoundary(View&, int x, int y, int button_state, bool enter) override
    {
    //printf("HANDLER: %sNotify %d %d\n",enter?"Enter":"Leave",x,y);
    if(!enter)inst->mi.inside=0;
    mevent.x=x;
    mevent.y=y;
    mevent.type = enter ? SW_GDK_ENTER_NOTIFY : SW_GDK_LEAVE_NOTIFY;
    inst->mouse_event(mevent);
    }
  void Special(View&, pangolin::InputSpecial inType, float x, float y, float p1, float p2, float p3, float p4, int button_state) override
    {
    //printf("Special: %d %f %f   %f %f %f %f  %d\n",(int)inType,x,y,p1,p2,p3,p4,button_state);
    }
  
  };

InputHandler handler;

void start(const std::string& name)
  {
  wname="Render";
  shmname="new_render";
  if(iname!="")wname  +=" - "+iname;
  if(iname!="")shmname+="_"+iname;
  
  comm=std::make_unique<CommHandler>(shmname);
  
  pangolin::CreateWindowAndBind(name,1200,1200,{{"default_font_size","15"}});
  pango_window=pangolin::GetBoundWindow();
  
  
  pangolin::View& cont1=pangolin::CreateDisplay().SetLayout(pangolin::LayoutEqual);
  cont1.SetBounds(0, 1, 0, 1);
  
  handler.inst=this;
  
  drawing_area=&cont1;
  cont1.SetHandler(&handler);
  
  // TO CHANGE TOPANGO
  //glfwWindowHint(GLFW_FOCUSED,GL_FALSE);
  
  
  
  //runit();
  //stop();
  }


Instance(const std::string& name="") : iname(name)
  {
  //GLFWLib::add_window([this,name](){ this->start(name); return true;});
  start(name);
  }

~Instance() { }


static void test_render(const std::string& name="")
  {
  RenderController torender(name);
  //usleep(2000000);
  RenderController::FrameInfo ft;
  

  torender.clear_all_data();
  
  ft.name="test";
  ft.x1=0.1;ft.x2=0.8;ft.y1=0.1;ft.y2=0.9;
  //ft.x1=0;ft.x2=1;ft.y1=0;ft.y2=1;
  ft.mt=0.1;ft.mb=0.1;ft.mr=0.1;ft.ml=0.1;
  ft.textsize=16;
  ft.mode=3;
  ft.active=1;
  ft.endtime=1;
  ft.timespan=1;
  torender.frame_config(ft);
   
  RenderController::ChanInfo cc;
  
  cc.clear=0;
  cc.active=1;
  
  cc.prefix=cc.frame="test";
  cc.window="test";cc.wintab=1;
  cc.pos_bottom=0.1;
  cc.pos_top=0.9;
  //cc.top=-0.7;
  //cc.bottom=-1;
  
  
  cc.label="test";
  cc.name="test0";cc.set(2,6,"bw");torender.SEND(cc);
  cc.name="test1";cc.set(1,6,"r");torender.SEND(cc);
  cc.name="test2";cc.set(0,6,"g");torender.SEND(cc);
  
  int n=200;
  double time=1.0;
  
  RenderController::DisplayData dd(torender,8192); 
  
  //dd.add_sample(0.5,0.5);
  for(int q1=0;q1<=n;q1++)dd.add_sample(q1*time/n,sin(q1*time/n*10+0.7)/2);
  dd.send_many_samples("test.test0");dd.clear_samples();
  
  for(int q1=0;q1<=n;q1++)dd.add_sample(q1*time/n,sin(q1*time/n*10));
  dd.send_many_samples("test.test1",false);dd.clear_samples();
  
  for(int q1=0;q1<=n;q1++)dd.add_sample(q1*time/n,sin(q1*time/n*10+1.1));
  dd.send_many_samples("test.test1",false);dd.clear_samples();
  
  for(int q1=0;q1<=n;q1++)dd.add_sample(q1*time/n,cos(q1*time/n*10));
  dd.send_many_samples("test.test2",false,true);dd.clear_samples();
  
  for(int q1=0;q1<=n;q1++)dd.add_sample(q1*time/n,cos(q1*time/n*10+0.4));
  dd.send_many_samples("test.test2",false);dd.clear_samples();
  
  torender.size_request(1600,1200);
  
  RenderController::FrameTextInfo ftt;
  ftt.framex=0.6;
  ftt.framey=0.6;
  ftt.angle=30;
  ftt.color="ry";
  ftt.size=45;
  ftt.text="Test Text rendering";
  ftt.frame="test";
  ftt.tname="test_text";
  
  torender.frametext(ftt);
  
  printf("Done sending test frame  %ld\n",torender.server.peek());
  }

};


int main(int argc, const char** argv)
  {
  Args args(argc,argv);
  assert(argc>=2);
  
  Instance instance(args.args[1]);
  
  
  //glEnable(GL_DEPTH_TEST);
  //glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  //glEnable(GL_BLEND);
  //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  
  if(args.args[1]=="test")instance.test_render("test");
  
  
  while(!pangolin::ShouldQuit())
    {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    instance.listen_main();
    
    // TO CHANGE FIX PANGO
    //if(usevsync)glfwSwapInterval(1);
    //else glfwSwapInterval(0);
    
    instance.render();
    
    //flush_input_event_queue();
    
    pangolin::FinishFrame();
    }
  
  return 0;
  }
