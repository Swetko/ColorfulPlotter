#pragma once

namespace RawShaders
{
const auto line_vert=R"Shader(
#version 130 

in vec4 vertex;
in vec3 normal;
in vec2 texcoord;

#define pi 3.1415926535897932384626433832795

out vec2 fnormal;
out vec4 fcolor;

flat out vec2 lw;
flat out float expand;
flat out float angle;

void main()
  {
  float norms=texcoord.s;
  float normt=texcoord.t;
  
  float new=abs(norms);
  //new=40;
  
  
  //norms=norms+sign(norms)*add;
  norms=sign(norms)*new;
  
  //float expand2=1;
  float expand2=sqrt(2);
  expand=expand2;
   
  float scale=norms+sign(norms)*expand2;
  float scale2=normt+sign(normt)*abs(scale);
  
  
  //vec3 norm=vec3(gl_Normal.xy*scale,scale);
  vec3 norm=vec3(normal.xy*scale,scale);
  
  
  gl_Position = 
    gl_ModelViewProjectionMatrix * 
      (vec4(
            vertex.xy
            +0.5*norm.xy
            +0.5*vertex.zw*abs(scale)
             
            ,0,1));
  
  
  fcolor=gl_Color;
  
  lw=vec2(abs(norms),abs(normt));
  
  fnormal=vec2(scale,scale2);
  
  angle=atan(norm.y/norm.x);
  
  }
)Shader";

const auto line_frag=R"Shader(
#version 130

in vec4 fcolor;
in vec2 fnormal;

flat in vec2 lw;
flat in float angle;

#define pi 3.1415926535897932384626433832795

//imlpement as texture 

float compute(float k,float angle)
  {
  float e=cos(pi/4-angle)*sqrt(2.0)/2;
  float g=sin(angle);
  float h=e-g;
  float l=1/cos(angle);
  float p=e-k;
  
  float alpha=0;
  if     (p<0  )alpha=0;
  else if(p<g  )alpha=l*(p/g)*p/2;
  else if(p<e+h)alpha=l*g/2+(p-g)*l;
  else if(p<e*2)alpha=l*g/2+2*h*l +l*g/2-l/g/2*(2*e-p)*(2*e-p);
  else alpha=1;
  
  return alpha;
  }

void main()
  {
  float ang=angle;
  if(ang<0)ang+=pi/2;
  if(ang>pi/4)ang=pi/2-ang;
  
  float dist=fnormal.x;
  float disty=abs(fnormal.y)/2-lw.y/2;
  
  vec4 color=fcolor;
  
  float d=abs(dist)/2;

  
  
  float lw2=lw.x/2;
  float alpha;
  
  
  alpha=0.5;
  
  if(disty<0)
    {
    alpha=compute(d-lw2,ang);
    alpha-=compute(d+lw2,ang);
    }
  else
    {
    // compute new angle
    
    //float ang2=atan(d/disty);
    //float ang2=atan(disty/d);
    //ang=angle+ang2/2;
    
    //while(ang<0)ang+=pi/2;
    //while(ang>pi/2)ang-=pi/2;
    //if(ang>pi/4)ang=pi/2-ang;
    
    d=sqrt(d*d+disty*disty);
    alpha=compute(d-lw2,ang);
    alpha-=compute(d+lw2,ang);
    //color.rgb=vec3(1,0,0);
    //alpha=1;
    }
  
  
  
  color.a=exp(log(alpha)*0.55);
  //color.a=exp(log(alpha)*0.45);
  //color.a=exp(log(alpha)*0.7);
  //color.a=alpha;
  
  //if(disty>=0)color=vec4(1,0,0,1);
  
  gl_FragColor=color;
  }


 

)Shader";

const auto point_vert=R"Shader(
#version 130 

//attribute vec4 position;
//attribute vec4 color;
//varying vec3 normal, lightDir;


out float ps,ss;

void main()
  {  
  gl_Position = gl_ModelViewProjectionMatrix * (vec4(gl_Vertex.xyz,gl_Vertex.w));
  gl_FrontColor = gl_Color;
  
  ps=gl_Point.size;
  //ps=18; 
  //ps+=2;
  //ps=8.3;
  ss=ps+sqrt(2.0);
  
  gl_PointSize = ss;
  
  
  }
)Shader";

const auto point_frag=R"Shader(
#version 130

#define pi 3.1415926535897932384626433832795

in float ps,ss;

float compute(float k,float angle)
  {
  float e=cos(pi/4-angle)*sqrt(2.0)/2;
  float g=sin(angle);
  float h=e-g;
  float l=1/cos(angle);
  float p=e-k;
  
  float alpha=0;
  if     (p<0  )alpha=0;
  else if(p<g  )alpha=l*(p/g)*p/2;
  else if(p<e+h)alpha=l*g/2+(p-g)*l;
  else if(p<e*2)alpha=l*g/2+2*h*l +l*g/2-l/g/2*(2*e-p)*(2*e-p);
  else alpha=1;
  
  return alpha;
  }

void main()
  {
  
  vec2 pos=gl_PointCoord*ss-vec2(ss,ss)/2;
  
  float ang=atan(pos.y/pos.x);
  if(ang<0)ang+=pi/2;
  if(ang>pi/4)ang=pi/2-ang;
  
  float dist=length(pos);
  
  
  float alpha;
  
  //alpha=(ps-dist)/ss;
  
  //alpha=compute(dist-ps/2,ang)*compute(dist-ps/2,ang);;
  alpha=compute(dist-ps/2,ang);
  
  
  vec4 color=gl_Color;
  
  //color.a=dist/ps;
  
  //color.a=alpha;
  color.a=exp(log(alpha*color.a)*0.45);
  //float beta=smoothstep(-1,1,pos.y);
  //color.rgb=vec3(1,0,0)*beta+(1-beta)*color.rgb;
  
  
  
  //color=vec4(1,1,1,1);
  //color.r=ang;
  //color.b=ang;
  //color.g=1-ang;
  
  
  gl_FragColor=color;
  }
)Shader";

}
