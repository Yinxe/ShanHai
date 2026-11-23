#undef NDEBUG
#include "native/Engine.h"
#include "BiomeCatalog.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <set>
namespace nativegen {
struct VoidLandformsTest {
 static void verify(J data){
  // Opt-in only: the same recipe without voidRevision must be byte identical
  // to the pre-feature kernel, so saved worlds never change.
  Engine legacy(data);auto& plain=legacy.field;
  auto lifted=data;
  lifted["config"]["voidRevision"]=1;
  lifted["config"]["voidRegions"]=J{{"enabled",true},
   {"ancientRift",{{"enabled",true},{"grid",6144},{"chance",.55},{"radiusMin",420},{"radiusMax",760},{"floorDepth",12},{"rimMargin",24}}},
   {"skyIslands",{{"enabled",true},{"grid",5120},{"chance",.50},{"radiusMin",340},{"radiusMax",620},
                  {"islandGrid",176},{"islandChance",.80},{"islandMinRadius",30},{"islandMaxRadius",86},
                  {"islandMinTop",300},{"islandMaxTop",450}}}};
  Engine engine(lifted);auto& f=engine.field;
  auto regions=f.inspectVoidRegions();
  assert(!regions.at("rifts").empty()||!regions.at("sky").empty());
  // The opt-in recipe must not disturb ordinary terrain outside a site.
  int same=0,diff=0;
  for(int z=-2048;z<=2048;z+=256)for(int x=-2048;x<=2048;x+=256){
   auto a=plain.surface(x+32768,z+32768,false),b=f.surface(x+32768,z+32768,false);
   if(b.voidVoid||b.voidFloor){continue;}
   if(a.height==b.height&&a.biome==b.biome)++same;else ++diff;
  }
  assert(same>0&&diff==0);
  // Every rift floor is warm, humid, below sea level and carries the ancient
  // landform identity instead of a climate biome.
  size_t floors=0,platforms=0,bareVoid=0;
  auto scan=[&](double wx,double wz){
   auto s=f.surface(int(wx)+32768,int(wz)+32768,false);
   if(s.voidFloor){
    ++floors;
    assert(s.height<=f.seaLevel()+8);
    assert(s.T>=.70&&s.H>=.78);
    assert(s.biome==13);
    assert(s.water<=s.height||s.water==s.basinWater);
    assert(s.closedBasin);
   }
   if(s.voidPlatform){++platforms;assert(s.voidTop>=300&&s.voidTop<=460);assert(s.voidBottom<s.voidTop);}
   if(s.voidVoid&&!s.voidPlatform){++bareVoid;assert(s.water==-32768&&s.lava==-32768&&s.lake==0);}
  };
  for(auto const&r:regions.at("rifts"))for(int i=0;i<12;++i){
   double a=i*.5235987756;scan(double(r.at("x"))+std::cos(a)*double(r.at("radiusX"))*.4,double(r.at("z"))+std::sin(a)*double(r.at("radiusZ"))*.4);
  }
  for(auto const&r:regions.at("sky"))for(int i=0;i<12;++i){
   double a=i*.5235987756;scan(double(r.at("x"))+std::cos(a)*double(r.at("radiusX"))*.5,double(r.at("z"))+std::sin(a)*double(r.at("radiusZ"))*.5);
  }
  assert(floors>0&&platforms>0&&bareVoid>0);
  // Void columns are air below the platform: no bedrock, no stone, no water.
  std::set<std::pair<int,int>> visited;
  for(auto const&r:regions.at("sky")){
   int cx=int(std::floor(double(r.at("x"))/16)),cz=int(std::floor(double(r.at("z"))/16));
   for(int dz=-6;dz<=6;++dz)for(int dx=-6;dx<=6;++dx){
    if(!visited.insert({cx+dx,cz+dz}).second)continue;
    auto bytes=engine.chunk(cx+dx,cz+dz);
    size_t q=260;std::vector<std::vector<int>> cols(256);
    for(int c=0;c<256;++c){int n=bytes[q]|(bytes[q+1]<<8);q+=2;int at=0;cols[c].assign(576,0);
     for(int i=0;i<n;++i){int len=bytes[q]|(bytes[q+1]<<8),b=bytes[q+2]|(bytes[q+3]<<8);q+=4;
      for(int k=0;k<len;++k)cols[c][at+k]=b;at+=len;}}
    for(int i=0;i<256;++i){
     auto s=f.surface(32768+(cx+dx)*16+i%16,32768+(cz+dz)*16+i/16,false);
     if(!s.voidVoid)continue;
     auto const&col=cols[i];
     if(s.voidPlatform){
      // Nothing may hang below the keel, and nothing may float above the
      // bounded decoration band (trunk, flat crown, rim vines).
      for(int y=-64;y<s.voidBottom;++y)assert(col[y+64]==0);
      for(int y=s.voidTop+33;y<512;++y)assert(col[y+64]==0);
      assert(col[s.voidTop+64]!=0);
     }else for(int y=-64;y<512;++y)assert(col[y+64]==0);
    }
   }
  }
  // Catalogue entries resolve to the same landform flags used by /locate.
  using namespace terrain_runtime::biome_catalog;
  int riftId=find("peakarma:ancient_rift"),skyId=find("peakarma:sky_islands");
  assert(riftId>=0&&skyId>=0);
  Surface rift;rift.voidFloor=true;
  Surface sky;sky.voidPlatform=true;
  assert(matches(entries().at(riftId),rift,true)&&!matches(entries().at(riftId),sky,true));
  assert(matches(entries().at(skyId),sky,true)&&!matches(entries().at(skyId),rift,true));
  std::cout<<"PASS ancient rift floors="<<floors<<" sky platforms="<<platforms<<" bare void columns="<<bareVoid
           <<", void has no bedrock, opt-in recipes leave legacy terrain identical\n";
 }
};
}
int main(int argc,char**argv){if(argc!=2)return 2;nativegen::J data;std::ifstream(argv[1])>>data;nativegen::VoidLandformsTest::verify(data);}
