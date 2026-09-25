/* X25519 (TweetNaCl-style, public domain algorithm) checked against RFC 7748 */
#include <stdio.h>
#include <string.h>
typedef unsigned char u8; typedef long long i64; typedef i64 gf[16];
static const gf _121665 = {0xDB41, 1};
static void car25519(gf o){int i;i64 c;for(i=0;i<16;i++){o[i]+=(1LL<<16);c=o[i]>>16;o[(i+1)*(i<15)]+=c-1+37*(c-1)*(i==15);o[i]-=c<<16;}}
static void sel25519(gf p,gf q,int b){i64 t,i,c=~(b-1);for(i=0;i<16;i++){t=c&(p[i]^q[i]);p[i]^=t;q[i]^=t;}}
static void pack25519(u8*o,const gf n){int i,j,b;gf m,t;for(i=0;i<16;i++)t[i]=n[i];car25519(t);car25519(t);car25519(t);
 for(j=0;j<2;j++){m[0]=t[0]-0xffed;for(i=1;i<15;i++){m[i]=t[i]-0xffff-((m[i-1]>>16)&1);m[i-1]&=0xffff;}
 m[15]=t[15]-0x7fff-((m[14]>>16)&1);b=(m[15]>>16)&1;m[14]&=0xffff;sel25519(t,m,1-b);}
 for(i=0;i<16;i++){o[2*i]=t[i]&0xff;o[2*i+1]=t[i]>>8;}}
static void unpack25519(gf o,const u8*n){int i;for(i=0;i<16;i++)o[i]=n[2*i]+((i64)n[2*i+1]<<8);o[15]&=0x7fff;}
static void A(gf o,const gf a,const gf b){int i;for(i=0;i<16;i++)o[i]=a[i]+b[i];}
static void Z(gf o,const gf a,const gf b){int i;for(i=0;i<16;i++)o[i]=a[i]-b[i];}
static void M(gf o,const gf a,const gf b){i64 i,j,t[31];for(i=0;i<31;i++)t[i]=0;for(i=0;i<16;i++)for(j=0;j<16;j++)t[i+j]+=a[i]*b[j];
 for(i=0;i<15;i++)t[i]+=38*t[i+16];for(i=0;i<16;i++)o[i]=t[i];car25519(o);car25519(o);}
static void S(gf o,const gf a){M(o,a,a);}
static void inv25519(gf o,const gf i){gf c;int a;for(a=0;a<16;a++)c[a]=i[a];for(a=253;a>=0;a--){S(c,c);if(a!=2&&a!=4)M(c,c,i);}for(a=0;a<16;a++)o[a]=c[a];}
static void x25519(u8*q,const u8*n,const u8*p){u8 z[32];i64 x[80],r,i;gf a,b,c,d,e,f;
 for(i=0;i<31;i++)z[i]=n[i];z[31]=(n[31]&127)|64;z[0]&=248;unpack25519(x,p);
 for(i=0;i<16;i++){b[i]=x[i];d[i]=a[i]=c[i]=0;}a[0]=d[0]=1;
 for(i=254;i>=0;--i){r=(z[i>>3]>>(i&7))&1;sel25519(a,b,r);sel25519(c,d,r);A(e,a,c);Z(a,a,c);A(c,b,d);Z(b,b,d);S(d,e);S(f,a);M(a,c,a);M(c,b,e);
  A(e,a,c);Z(a,a,c);S(b,a);Z(c,d,f);M(a,c,_121665);A(a,a,d);M(c,c,a);M(a,d,f);M(d,b,x);S(b,e);sel25519(a,b,r);sel25519(c,d,r);}
 for(i=0;i<16;i++){x[i+16]=a[i];x[i+32]=c[i];x[i+48]=b[i];x[i+64]=d[i];}
 inv25519(x+32,x+32);M(x+16,x+16,x+32);pack25519(q,x+16);}
static void hex(const u8*b,char*o){for(int i=0;i<32;i++)sprintf(o+2*i,"%02x",b[i]);}
static void unhex(const char*s,u8*o){for(int i=0;i<32;i++){unsigned v;sscanf(s+2*i,"%2x",&v);o[i]=v;}}
static int check(const char*name,const u8*got,const char*want){char h[65];hex(got,h);int ok=!strcmp(h,want);printf("x25519 %s: %s %s\n",name,ok?"PASS":"FAIL",ok?"":h);return ok;}
int main(void){
 u8 k[32],u[32],o[32];int bad=0,i;
 unhex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4",k);
 unhex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c",u);
 x25519(o,k,u);bad|=!check("rfc7748-v1",o,"c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552");
 memset(k,0,32);k[0]=9;memset(u,0,32);u[0]=9;
 for(i=1;i<=1000;i++){x25519(o,k,u);memcpy(u,k,32);memcpy(k,o,32);
  if(i==1)bad|=!check("iter-1",k,"422c8e7a6227d7bca1350b3e2bb7279f7897b87bb6854b783c60e80311ae3079");}
 bad|=!check("iter-1000",k,"684cf59ba83309552800ef566f2f4d3c1c3887c49360e3875f2eb94d99532c51");
 printf("x25519 overall: %s\n",bad?"FAIL":"PASS");return bad;}
