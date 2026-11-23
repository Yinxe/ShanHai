// Two opt-in landform families, enabled by config.voidRevision>=1.
//
// 1) Ancient rift (voidRegions.ancientRift): a closed inland chasm. The floor
//    sits below sea level, the walls drop through one narrow radial band and
//    the rim keeps its original shaped height. The floor is warm and humid
//    regardless of regional climate, so it carries its own ancient ecology.
// 2) Sky archipelago (voidRegions.skyIslands): a separate region whose entire
//    column is void. There is no stone, no cave, no aquifer and no bedrock
//    below the platforms - Y=-64 is air. Platforms are lens-shaped and only
//    the interval [bottom, top] is filled.
//
// Both are pure functions of (cell, seed) plus the pre-void shaped height, so
// workers, chunk order and saved worlds always agree. Nothing here samples a
// generated chunk, and no site builder calls surface()/base().
struct VoidRegion {bool valid=false;double x=0,z=0,rx=0,rz=0,phase=0;int floorY=0,rimY=0;};
struct SkyRegion {bool valid=false;double x=0,z=0,rx=0,rz=0,phase=0;int rimY=0;};
bool voidEnabled=false;
bool ancientRiftEnabled=false,skyIslandsEnabled=false;
double riftGrid=6144,riftChance=.55,riftRadiusMin=420,riftRadiusMax=760;
double riftFloorDepth=12,riftRimMargin=24;
double skyGrid=5120,skyChance=.50,skyRadiusMin=340,skyRadiusMax=620;
double islandGrid=192,islandChance=.55,islandRadiusMin=18,islandRadiusMax=56;
int islandTopMin=300,islandTopMax=450;
Perlin voidNoise{seedHash(terrainSeed,71001)};
Cache<uint64_t,VoidRegion> riftRegions{256};
Cache<uint64_t,SkyRegion> skyRegions{256};
void loadVoidRegions(){
 if(C.value("voidRevision",0)<1||!C.contains("voidRegions"))return;
 auto const&q=C.at("voidRegions");
 // The parent switch wins: a recipe may keep its tuned sub-blocks and disable
 // the whole family without deleting them.
 if(!q.value("enabled",true))return;
 if(q.contains("ancientRift")&&q.at("ancientRift").value("enabled",false)){
  auto const&r=q.at("ancientRift");
  riftGrid=num(r,"grid");riftChance=num(r,"chance");
  riftRadiusMin=num(r,"radiusMin");riftRadiusMax=num(r,"radiusMax");
  riftFloorDepth=num(r,"floorDepth");riftRimMargin=num(r,"rimMargin");
  // A quarter-cell inset keeps two full-radius sites from merging their walls.
  if(riftGrid<3072||riftGrid>12288||riftChance<0||riftChance>1
     ||riftRadiusMin<160||riftRadiusMax<riftRadiusMin||riftRadiusMax*2.4+520>=riftGrid
     ||riftFloorDepth<4||riftFloorDepth>40||riftRimMargin<8)
   throw std::runtime_error("Invalid ancient rift distribution");
  ancientRiftEnabled=true;
 }
 if(q.contains("skyIslands")&&q.at("skyIslands").value("enabled",false)){
  auto const&r=q.at("skyIslands");
  skyGrid=num(r,"grid");skyChance=num(r,"chance");
  skyRadiusMin=num(r,"radiusMin");skyRadiusMax=num(r,"radiusMax");
  islandGrid=num(r,"islandGrid");islandChance=num(r,"islandChance");
  islandRadiusMin=num(r,"islandMinRadius");islandRadiusMax=num(r,"islandMaxRadius");
  islandTopMin=int(num(r,"islandMinTop"));islandTopMax=int(num(r,"islandMaxTop"));
  if(skyGrid<2560||skyGrid>10240||skyChance<0||skyChance>1
     ||skyRadiusMin<200||skyRadiusMax<skyRadiusMin||skyRadiusMax*2.4+520>=skyGrid
     ||islandGrid<96||islandGrid>512||islandChance<0||islandChance>1
     ||islandRadiusMin<8||islandRadiusMax<islandRadiusMin||islandRadiusMax*2>islandGrid
     ||islandTopMin<seaLevelValue+120||islandTopMax<islandTopMin||islandTopMax>generatedMaxYExclusive-32)
   throw std::runtime_error("Invalid sky island distribution");
  skyIslandsEnabled=true;
 }
 voidEnabled=ancientRiftEnabled||skyIslandsEnabled;
}
// Signed ellipse distance with angular and coherent warp. 1 is the wall top.
double voidEdge(double x,double z,double cx,double cz,double rx,double rz,double phase){
 double dx=(x-cx)/rx,dz=(z-cz)/rz,a=std::atan2(dz,dx);
 double warp=1+.10*std::sin(3*a+phase)+.06*std::sin(5*a-phase*1.7);
 return std::hypot(dx,dz)/warp+.035*voidNoise.at(x/260.,z/260.);
}
// Both site builders share the same acceptance survey: dry inland rim, no
// accepted special forest, no ocean or lake cut by the wall, and a real drop.
VoidRegion riftRegion(int gx,int gz){return riftRegions.get(key2(gx,gz),[&]{
 VoidRegion r;if(!ancientRiftEnabled)return r;
 auto rnd=[&](int salt){return hash01(gx,gz,terrainSeed^salt);};
 if(rnd(71101)>=riftChance)return r;
 r.rx=riftRadiusMin+(riftRadiusMax-riftRadiusMin)*rnd(71103);
 r.rz=r.rx*(.80+.40*rnd(71105));
 r.phase=rnd(71107)*6.283185307179586;
 for(int attempt=0;attempt<5;++attempt){
  constexpr double inset=.24;
  r.x=(gx+inset+(1-2*inset)*rnd(71111+attempt*20))*riftGrid;
  r.z=(gz+inset+(1-2*inset)*rnd(71113+attempt*20))*riftGrid;
  bool valid=true;
  for(auto const&p:regionalProtected)if(p.radius>=0&&std::hypot(r.x-p.x,r.z-p.z)<p.radius+r.rx*1.35+220)valid=false;
  // Two rings: the wall top and the outer shelf. The whole rim must be dry
  // inland ground, otherwise the chasm would halve an ocean or a lake.
  double low=1e9,high=-1e9;
  for(int i=0;i<24&&valid;++i){
   double a=i*.2617993877991494;
   for(double k:{.74,1.08}){
    double X=r.x+std::cos(a)*r.rx*k+32768,Z=r.z+std::sin(a)*r.rz*k+32768;
    double h=shaped(X,Z,vcont(X,Z));
    low=std::min(low,h);high=std::max(high,h);
    if(h<seaLevelValue+riftRimMargin)valid=false;
   }
  }
  if(!valid)continue;
  r.floorY=int(std::floor(seaLevelValue-riftFloorDepth));
  r.rimY=int(std::floor(std::min(high,double(generatedMaxYExclusive-40))));
  // A shallow bowl is a basin, not a rift. Require a real vertical drop.
  if(r.rimY-r.floorY<120)continue;
  r.valid=true;break;
 }
 return r;
});}
SkyRegion skyRegion(int gx,int gz){return skyRegions.get(key2(gx,gz),[&]{
 SkyRegion r;if(!skyIslandsEnabled)return r;
 auto rnd=[&](int salt){return hash01(gx,gz,terrainSeed^salt);};
 if(rnd(71301)>=skyChance)return r;
 r.rx=skyRadiusMin+(skyRadiusMax-skyRadiusMin)*rnd(71303);
 r.rz=r.rx*(.80+.40*rnd(71305));
 r.phase=rnd(71307)*6.283185307179586;
 for(int attempt=0;attempt<5;++attempt){
  constexpr double inset=.24;
  r.x=(gx+inset+(1-2*inset)*rnd(71311+attempt*20))*skyGrid;
  r.z=(gz+inset+(1-2*inset)*rnd(71313+attempt*20))*skyGrid;
  bool valid=true;
  for(auto const&p:regionalProtected)if(p.radius>=0&&std::hypot(r.x-p.x,r.z-p.z)<p.radius+r.rx*1.35+220)valid=false;
  double high=-1e9;
  for(int i=0;i<24&&valid;++i){
   double a=i*.2617993877991494;
   for(double k:{.74,1.08}){
    double X=r.x+std::cos(a)*r.rx*k+32768,Z=r.z+std::sin(a)*r.rz*k+32768;
    double h=shaped(X,Z,vcont(X,Z));
    high=std::max(high,h);
    if(h<seaLevelValue+riftRimMargin)valid=false;
   }
  }
  if(!valid)continue;
  r.rimY=int(std::floor(std::min(high,double(generatedMaxYExclusive-40))));
  r.valid=true;break;
 }
 return r;
});}
// Resolve the owning site for one column. Neighbours are visited in a stable
// order and the closest edge wins, so cell borders never truncate a wall.
bool voidSite(int wx,int wz,VoidRegion&rift,double&riftQ,SkyRegion&sky,double&skyQ){
 rift=VoidRegion{};sky=SkyRegion{};riftQ=1e9;skyQ=1e9;
 if(!voidEnabled)return false;
 double x=wx-32768,z=wz-32768;
 // Cheap reject before the owner scan. A site centre sits in [0.24,0.76] of its
 // cell and reaches at most radiusMax past it, so only that band of a cell can
 // ever be covered. The shipped lattices leave a wide uncovered gap.
 auto covered=[](double v,double grid,double radius){
  double f=v-grid*std::floor(v/grid),lo=.24*grid-radius,hi=.76*grid+radius;
  return (f>=lo&&f<=hi)||(f+grid>=lo&&f+grid<=hi)||(f-grid>=lo&&f-grid<=hi);
 };
 bool riftPossible=ancientRiftEnabled&&covered(x,riftGrid,riftRadiusMax)&&covered(z,riftGrid,riftRadiusMax);
 bool skyPossible=skyIslandsEnabled&&covered(x,skyGrid,skyRadiusMax)&&covered(z,skyGrid,skyRadiusMax);
 if(!riftPossible&&!skyPossible)return false;
 if(!riftPossible){rift=VoidRegion{};riftQ=1e9;}
 if(riftPossible){
  int gx=int(std::floor(x/riftGrid)),gz=int(std::floor(z/riftGrid));
  for(int b=gz-1;b<=gz+1;++b)for(int a=gx-1;a<=gx+1;++a){
   auto r=riftRegion(a,b);if(!r.valid)continue;
   if(std::abs(x-r.x)>r.rx*1.5||std::abs(z-r.z)>r.rz*1.5)continue;
   double q=voidEdge(x,z,r.x,r.z,r.rx,r.rz,r.phase);
   if(q<riftQ){riftQ=q;rift=r;}
  }
 }
 if(skyPossible){
  int gx=int(std::floor(x/skyGrid)),gz=int(std::floor(z/skyGrid));
  for(int b=gz-1;b<=gz+1;++b)for(int a=gx-1;a<=gx+1;++a){
   auto r=skyRegion(a,b);if(!r.valid)continue;
   if(std::abs(x-r.x)>r.rx*1.5||std::abs(z-r.z)>r.rz*1.5)continue;
   double q=voidEdge(x,z,r.x,r.z,r.rx,r.rz,r.phase);
   if(q<skyQ){skyQ=q;sky=r;}
  }
 }
 return (rift.valid&&riftQ<1.06)||(sky.valid&&skyQ<1.06);
}
// One platform plan for a column. Neighbouring lattice cells are visited in a
// stable order; the highest top wins so a column never holds two islands. The
// plan is identical in the height pass and in the material pass.
struct IslandPlan {bool valid=false;int top=0,bottom=0;};
IslandPlan islandPlan(int x,int z){
 IslandPlan out;
 int igx=int(std::floor(x/islandGrid)),igz=int(std::floor(z/islandGrid));
 for(int b=igz-1;b<=igz+1;++b)for(int a=igx-1;a<=igx+1;++a){
  auto rnd=[&](int salt){return hash01(a,b,terrainSeed^salt);};
  if(rnd(71201)>=islandChance)continue;
  double cx=(a+.18+.64*rnd(71203))*islandGrid,cz=(b+.18+.64*rnd(71205))*islandGrid;
  double radius=islandRadiusMin+(islandRadiusMax-islandRadiusMin)*rnd(71207);
  double phase=rnd(71209)*6.283185307179586;
  double dx=x-cx,dz=z-cz,d=std::hypot(dx,dz);
  if(d>=radius)continue;
  double angle=std::atan2(dz,dx);
  double lobe=1+.16*std::sin(3*angle+phase)+.10*std::sin(5*angle-phase);
  double q=d/(radius*lobe);if(q>=1)continue;
  int centre=islandTopMin+int((islandTopMax-islandTopMin)*rnd(71211));
  int thickness=12+int(22*rnd(71213));
  double lens=std::sqrt(std::max(0.,1-q*q));
  int islandTop=centre+int(7*lens*voidNoise.at(x/17.,z/17.));
  int islandBottom=islandTop-int(std::max(4.,thickness*lens));
  if(islandTop>out.top||!out.valid){out.valid=true;out.top=islandTop;out.bottom=islandBottom;}
 }
 return out;
}
// Fill the rift, or void the sky region and raise its platforms. Runs at the
// end of compute(), after every other height shaper, so no later pass can
// reintroduce ground, water or a coast into a void column.
void voidShape(Surface&s,double& height,int wx,int wz){
 if(!voidEnabled)return;
 VoidRegion rift;SkyRegion sky;double riftQ,skyQ;
 if(!voidSite(wx,wz,rift,riftQ,sky,skyQ))return;
 double x=wx-32768,z=wz-32768;
 // The sky archipelago owns its whole footprint: the column is void, and only
 // a platform may fill an interval. It never coexists with a rift floor.
 if(sky.valid&&skyQ<1.06){
  s.voidVoid=true;s.voidRegion=1-smooth(.86,1.06,skyQ);
  s.water=-32768;s.lava=-32768;s.kind=0;s.lake=0;s.closedBasin=false;s.basinWater=-32768;s.basinWeight=0;
  s.river=0;s.mainRiver=0;s.fluvialType=0;s.aridType=0;s.wetland=0;
  s.treeD=0;s.rockCap=0;s.rock=0;s.surf=0;s.giant=0;
  int top=-32768,bottom=0;auto plan=islandPlan(x,z);
  if(plan.valid){top=plan.top;bottom=plan.bottom;}
  if(top>-32768){
   s.voidPlatform=true;s.voidTop=top;s.voidBottom=bottom;
   height=top;
  }else{
   // Bare void: keep a plausible reference height so downstream arithmetic
   // stays in range, but the column is emitted as air from Y=-64 up.
   height=std::min(sky.rimY,generatedMaxYExclusive-2);
  }
  return;
 }
 // Ancient rift: only the floor is lowered; the rim keeps its shaped height.
 if(!rift.valid||riftQ>=1.06)return;
 double rim=height;
 double tier=std::floor(4*(1-smooth(.10,.66,riftQ)))/4.;
 double floorY=rift.floorY+2.5*voidNoise.at(x/95.,z/95.)+5*tier+3*voidNoise.at(x/31.,z/31.);
 // Nearly the entire drop happens inside one narrow band, so no ring-shaped
 // plateau appears outside the wall and no island of old terrain survives.
 // mix(wall, floor, rim): wall=0 keeps the floor, wall=1 restores the rim.
 double wall=smooth(.62,.72,riftQ);
 height=clamp(mix(wall,floorY,rim),double(seaLevelValue-64),double(generatedMaxYExclusive-2));
 s.voidRegion=std::max(s.voidRegion,1-smooth(.80,1.02,riftQ));
 s.treeD=0;s.rockCap=0;
 if(riftQ<=.72){
  s.voidFloor=true;
  s.closedBasin=true;s.basinWeight=std::max(s.basinWeight,1-smooth(.56,.72,riftQ));
  // One shallow lake below sea level. closedBasin keeps the hydrology planner
  // from treating that water as a marine sink.
  s.basinWater=int(std::floor(rift.floorY+3));
  if(s.basinWater>height){s.water=s.basinWater;s.kind=2;s.lake=1;}
 }
}
// Climate for the rift floor, evaluated before the shared weight machinery so
// the ancient ecology is warm and humid even inside a cold region.
void voidFloorClimate(Surface&s){
 s.T=.78;s.H=.84;s.e=s.continuous;s.closedBasin=true;
 s.treeD=.010;s.treeK=4;s.soilSand=0;s.surf=0;
 s.rockCap=0;s.rock=0;
 s.desert=0;s.desertSand=0;s.badlands=0;s.aridType=0;
 s.glacier=0;s.superGlacier=0;s.glacierSnow=0;s.snowCover=0;s.iceHabitat=0;s.iceGround=-1;
 s.volcanic=0;s.barren=0;s.wetland=0;s.wetlandMoisture=0;
}
// Final identity of both landforms. Runs after the cached skin so neither the
// forest pass nor the snow skin can repaint an ancient floor or an island.
void voidSurfaceEcology(Surface&s,int x,int z){
 if(!voidEnabled)return;
 if(s.voidVoid&&!s.voidPlatform){
  s.biome=20;s.treeD=0;s.snowCover=0;s.glacierSnow=0;s.glacier=0;s.superGlacier=0;
  return;
 }
 if(s.voidPlatform){
  s.biome=14;s.treeD=.006;s.treeK=1;
  s.soilSand=0;s.snowCover=0;s.glacierSnow=0;s.glacier=0;s.superGlacier=0;
  s.iceHabitat=0;s.iceGround=-1;s.wetland=0;s.wetlandMoisture=0;
  s.desert=0;s.desertSand=0;s.badlands=0;s.aridType=0;s.rockCap=0;s.rock=0;s.surf=0;
  return;
 }
 if(s.voidFloor){
  // Warm humid carrier: the palette index stays a vanilla climate carrier and
  // the landform identity lives in the flag, exactly like glacier and volcano.
  s.biome=13;s.treeD=.010;s.treeK=4;s.soilSand=0;s.surf=0;
  s.snowCover=0;s.glacierSnow=0;s.glacier=0;s.superGlacier=0;
  s.iceHabitat=0;s.iceGround=-1;s.wetland=0;s.wetlandMoisture=0;
  s.desert=0;s.desertSand=0;s.badlands=0;s.aridType=0;s.rockCap=0;s.rock=0;
 }
}
// Platform body: deepslate keel, stone shell, soil cap. Only [bottom, top] is
// ever written, so a platform has no roots into the void.
uint8_t voidPlatformMaterial(Surface const&s,int y){
 if(y<=s.voidBottom+1)return 95;
 int depth=s.voidTop-y;
 if(depth<=0)return 4;
 if(depth<=3)return 3;
 return 2;
}
// Rift skin: moss, mud and clay terraces on the floor, banded rock on the wall.
uint8_t voidRiftMaterial(Surface const&s,int x,int y,int z,bool floor){
 if(floor){
  int depth=s.height-y;
  if(depth<=0){double grain=matNoise.at(x/37.,z/37.);return uint8_t(grain>.42?80:grain>.16?3:85);}
  if(depth<=2)return 3;
  if(depth<=5)return 48;
  return uint8_t(y<-40?95:2);
 }
 double band=voidNoise.at(x/23.,y/19.);
 return uint8_t(band>.55?96:band<-.55?94:(y<s.height-60?95:2));
}
