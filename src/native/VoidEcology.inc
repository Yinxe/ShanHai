// Ancient rift ecology and sky-island decoration.
//
// The rift floor is a warm humid pocket below sea level: moss terraces, mud
// banks, a shallow lake and an old-growth stand whose trunks are the only
// thing tall enough to leave the chasm. The walls keep banded rock with moss
// shelves. Sky islands get a thin grass cap, hanging vines and the occasional
// small tree; nothing is placed below a platform.
//
// Every anchor is a world-coordinate lattice lookup, clipped to the emitting
// chunk. No pass loads a neighbour chunk and no pass carves rock.

// --- ancient rift ---------------------------------------------------------
struct RiftTree {
 bool valid=false;int x=0,z=0,ground=0,height=0;
 double radius=0,phase=0,crown=0,lean=0;int trunkRadius=1,style=0;bool hollow=false;
 std::array<std::array<int,3>,6> rootFeet{};
};
Cache<uint64_t,RiftTree> riftTrees{1024};
RiftTree riftTree(int gx,int gz){
 return riftTrees.get(key2(gx,gz),[&]{
  RiftTree t;auto rnd=[&](int salt){return hash01(gx,gz,terrainSeed^salt);};
  if(rnd(71401)>.62)return t;
  int x=gx*11+int(rnd(71403)*11),z=gz*11+int(rnd(71405)*11);
  auto s=field.surface(x+32768,z+32768,false);
  if(!s.voidFloor||s.water>s.height||s.height>=Height-66)return t;
  // A trunk must stand on the floor itself, not on a terrace edge or a bank.
  int lo=s.height,hi=s.height;
  for(auto [dx,dz]:{std::pair{-2,0},std::pair{2,0},std::pair{0,-2},std::pair{0,2}}){
   auto b=field.surface(x+dx+32768,z+dz+32768,false);
   if(!b.voidFloor||b.water>b.height)return t;
   lo=std::min(lo,b.height);hi=std::max(hi,b.height);
  }
  if(hi-lo>2)return t;
  // Buttress roots need level floor on all six samples; a tree on a terrace
  // lip would float one side of its root plate.
  for(auto [dx,dz]:{std::pair{-3,0},std::pair{3,0},std::pair{0,-3},std::pair{0,3},std::pair{-3,-3},std::pair{3,3}}){
   auto b=field.surface(x+dx+32768,z+dz+32768,false);
   if(!b.voidFloor||b.water>b.height||std::abs(b.height-s.height)>1)return t;
  }
  t.valid=true;t.x=x;t.z=z;t.ground=s.height;
  // Three silhouettes: emergent canopy tree, leaning buttress tree, and a
  // hollow elder with an open chamber. Each reads differently from below.
  double style=rnd(71413);
  t.style=style<.44?0:style<.78?1:2;
  t.height=(t.style==2?34:t.style==1?30:26)+int(rnd(71407)*(t.style==2?18:14));
  t.crown=(t.style==2?5.4:t.style==1?4.8:4.2)+rnd(71409)*1.6;
  t.trunkRadius=t.style==2?2:t.style==1?1:1;   // radius: 3x3, 3x3, 3x3 minimum via +1 below
  t.lean=(t.style==1?.22:.08)*rnd(71415);
  t.hollow=t.style==2&&rnd(71417)<.62;
  t.phase=rnd(71411)*6.283185307179586;
  if(t.ground+t.height>Height-66)return RiftTree{};
  // Root feet: six deterministic buttresses around the base.
  for(int k=0;k<6;++k){
   double a=t.phase+k*1.0471975512;
   double reach=t.trunkRadius+2.4+rnd(71419+k)*1.8;
   t.rootFeet[k]={x+roundJS(std::cos(a)*reach),s.height,z+roundJS(std::sin(a)*reach)};
  }
  return t;
 });
}
// --- ancient-rift old growth -------------------------------------------------
// Palette indices used by the rift stand. All are vanilla blocks already in the
// frozen contract, so no new palette entry is introduced.
static constexpr uint16_t RiftBark=12;      // spruce_log
static constexpr uint16_t RiftWood=61;      // spruce_wood (bark on all faces)
static constexpr uint16_t RiftRoot=34;      // spruce_log_x, laid horizontally
static constexpr uint16_t RiftLeaf=13;      // spruce_leaves
static constexpr uint16_t RiftMoss=80;      // moss_block
static constexpr uint16_t RiftCarpet=120;   // moss_carpet
static constexpr uint16_t RiftVine=76;      // vine
// Extra rift dressing. All indices already exist in the frozen contract.
static constexpr uint16_t RiftCaveVine=124;      // cave_vines
static constexpr uint16_t RiftCaveVineBerries=125;// cave_vines_head_with_berries
static constexpr uint16_t RiftSporeBlossom=123;  // spore_blossom
static constexpr uint16_t RiftAzalea=121;        // azalea
static constexpr uint16_t RiftFloweringAzalea=122;
static constexpr uint16_t RiftFern=37;
static constexpr uint16_t RiftClay=48;
static constexpr uint16_t RiftTuff=96;
static constexpr uint16_t RiftCalcite=94;
static constexpr uint16_t RiftGlowLichen[5]={243,244,245,246,247};
struct RiftCell {int x,y,z;uint16_t block;};
struct RiftModel {RiftTree tree;std::vector<RiftCell> cells;};
Cache<uint64_t,std::shared_ptr<RiftModel>> riftShapes{192};

std::shared_ptr<RiftModel> riftTreeModel(int gx,int gz){
 return riftShapes.get(key2(gx,gz),[&]{
  auto out=std::make_shared<RiftModel>();auto t=riftTree(gx,gz);out->tree=t;
  if(!t.valid)return out;
  auto add=[&](int x,int y,int z,uint16_t b){
   if(y>=-59&&y<=Height-65)out->cells.push_back({x,y,z,b});
  };
  // Ellipsoidal foliage shell with an angular warp. `flatten` squashes the
  // crown, which is what makes a 40-block trunk read as an emergent canopy
  // rather than a lollipop.
  auto crown=[&](double cx,double cy,double cz,double r,double ry,double flatten){
   int span=int(std::ceil(r*1.18));
   for(int z=int(std::floor(cz-span));z<=int(std::ceil(cz+span));++z)
   for(int x=int(std::floor(cx-span));x<=int(std::ceil(cx+span));++x){
    double dx=x-cx,dz=z-cz,angle=std::atan2(dz,dx);
    double rr=r*(1+.11*std::sin(angle*3+t.phase)+.05*std::cos(angle*5-t.phase));
    double radial=(dx*dx+dz*dz)/(rr*rr);
    for(int y=int(std::floor(cy-ry));y<=int(std::ceil(cy+ry));++y){
     double vy=(y-cy)/(ry*std::max(.35,flatten));
     // 1.06 keeps the shell one block thicker at the rim, which closes the
     // see-through gaps a strict <=1 ellipsoid leaves in a large crown.
     if(radial+vy*vy>1.06)continue;
     add(x,y,z,RiftLeaf);
    }
   }
  };
  // A limb walks from the trunk to its tip, thickening to 2x2 near the base so
  // branches read as wood and not as a one-block line.
  auto limb=[&](int fx,int fy,int fz,int tx,int ty,int tz,bool thick){
   int x=fx,y=fy,z=fz;add(x,y,z,RiftWood);
   int guard=0;
   while((x!=tx||y!=ty||z!=tz)&&guard++<256){
    if(x!=tx)x+=x<tx?1:-1;
    if(z!=tz)z+=z<tz?1:-1;
    if(y!=ty&&guard%2==0)y+=y<ty?1:-1;
    add(x,y,z,RiftWood);
    if(thick){add(x+1,y,z,RiftWood);add(x,y,z+1,RiftWood);}
   }
  };
  // Buttress roots: a low wedge that widens outward, the signature of an
  // emergent rainforest tree. Laid sideways so the grain runs outward.
  for(auto const&f:t.rootFeet){
   double dx=f[0]-t.x,dz=f[2]-t.z,len=std::hypot(dx,dz);
   if(len<1)continue;
   for(int s=0;s<=int(len);++s){
    double u=double(s)/len;
    int x=t.x+roundJS(dx*u),z=t.z+roundJS(dz*u);
    int h=int(std::floor(1.0+2.6*(1-u)*(1-u)));
    for(int y=0;y<h;++y)add(x,t.ground+y,z,y==0?RiftRoot:RiftWood);
   }
  }
  // Trunk: leans by `lean` blocks over its height, and opens into a hollow
  // chamber when the tree is a hollow elder.
  int tr=t.trunkRadius;
  for(int y=1;y<=t.height;++y){
   double u=double(y)/t.height;
   int cx=t.x+roundJS(std::sin(t.phase)*t.lean*y);
   int cz=t.z+roundJS(std::cos(t.phase)*t.lean*y);
   // Radius never drops below 1, so every trunk is at least 3x3 at the base
   // and 2x2 above the taper: a one-block pole is what made these read as grass.
   int r=tr+1-(u>.72?1:0);
   for(int dz=-r;dz<=r;++dz)for(int dx=-r;dx<=r;++dx){
    if(dx*dx+dz*dz>r*r+(r>1?2:0))continue;
    // Hollow elder: a two-block-wide shaft of air from 2 to 55% height.
    if(t.hollow&&u>.06&&u<.55&&std::abs(dx)<=0&&std::abs(dz)<=0)continue;
    // The trunk column itself carries oriented bark, so a cut top shows rings;
    // the surrounding shell is all-bark wood. Without this every log was
    // RiftWood and a felled trunk read as a solid block of bark.
    bool core=(dx==0&&dz==0);
    add(cx+dx,t.ground+y,cz+dz,core?RiftBark:RiftWood);
   }
   // Moss streaks on the shaded side of the trunk.
   if(y%5==2&&r>1)add(cx-r,t.ground+y,cz,RiftMoss);
  }
  // Hollow floor and lintel so the chamber is enterable, not a sealed tube.
  if(t.hollow){
   add(t.x,t.ground+1,t.z,RiftBark);
   int doorY=t.ground+2;
   for(int d=0;d<=tr+1;++d)add(t.x+d,doorY,t.z,0);
   for(int d=0;d<=tr+1;++d)add(t.x+d,doorY+1,t.z,0);
  }
  int top=t.ground+t.height;
  // Primary crown plus two offset sub-crowns: the layered silhouette the other
  // forest biomes use. Each is flattened differently so they read as tiers.
  crown(t.x+.5,top-.4,t.z+.5,t.crown*1.08,t.crown*.70,.72);
  for(int k=0;k<3;++k){
   double a=t.phase+k*2.094395102;
   double reach=t.crown*.78;
   int bx=t.x+roundJS(std::cos(a)*reach),bz=t.z+roundJS(std::sin(a)*reach);
   int by=top-2-int(k*2)-int(t.height*.10);
   // A real limb from the trunk up into each sub-crown.
   limb(t.x+roundJS(std::sin(t.phase)*t.lean*t.height*.75),top-int(t.height*.22),t.z,bx,by,bz,true);
   crown(bx+.5,by+.5,bz+.5,t.crown*.74,t.crown*.48,.80);
  }
  // Top tuft: a small cap so the crown does not end in a flat cut.
  crown(t.x+.5,top+.6,t.z+.5,t.crown*.52,t.crown*.32,.95);
  // Hanging vines from the outer crown ring, the vertical accent that sells
  // the ancient-forest look from a distance.
  for(auto const&c:out->cells){
   if(c.block!=RiftLeaf)continue;
   if(hash01(c.x,c.z,terrainSeed^71441)>.24)continue;
   int len=2+int(hash01(c.x,c.y,terrainSeed^71443)*6);
   for(int d=1;d<=len;++d)add(c.x,c.y-d,c.z,RiftVine);
  }
  // Moss carpet on the root plate.
  for(auto const&f:t.rootFeet){
   if(hash01(f[0],f[2],terrainSeed^71445)<.55)add(f[0],t.ground+1,f[2],RiftCarpet);
  }
  return out;
 });
}
void riftEcology(int x0,int z0,std::vector<uint16_t>&raw,std::array<Surface,256>const&surfaces){
 if(!ancientRiftEnabled())return;
 // Lattice sites use offset-free coordinates; the chunk origin carries +32768.
 int wx0=x0-32768,wz0=z0-32768;
 bool any=false;for(auto const&s:surfaces)any|=s.voidFloor;if(!any)return;
 // Floor flora first: it must not be overwritten by a trunk base later.
 for(int i=0;i<256;++i){
  auto const&s=surfaces[i];if(!s.voidFloor)continue;
  int X=x0+i%16,Z=z0+i/16;if(s.water>s.height)continue;
  auto col=raw.data()+i*Height;int h=s.height;
  if(col[h+64]==0||col[h+65]!=0)continue;
  double pick=hash01(X,Z,terrainSeed^71421);
  // Two independent fields: a broad damp mask and a fine litter mask. The
  // rift floor is a rainforest floor, so moss dominates and grass is the
  // exception, not the default.
  double lush=voidNoiseAt(X/21.+13.,Z/21.-7.);
  double litter=voidNoiseAt(X/6.1-31.,Z/6.1+17.);
  // Moss replaces the soil cap on damp terraces, with carpet on top of it.
  if(lush>.02&&pick<.52){col[h+64]=80;if(pick<.30)col[h+65]=120;continue;}
  // Azalea patches: the flowering variant is the rarer one.
  if(litter>.30&&pick<.10){col[h+64]=80;col[h+65]=pick<.035?RiftFloweringAzalea:RiftAzalea;continue;}
  // Ferns are the dominant ground plant; tall grass only in dry gaps.
  if(pick<.46){col[h+65]=RiftFern;continue;}
  if(pick<.56){col[h+65]=36;}
  // A few clay lenses where the floor is wettest.
  if(lush<-.34&&pick<.70)col[h+64]=RiftClay;
 }
 // Hanging cave vines and spore blossoms under the chasm rim. These read as
 // "the chasm has been here a very long time", which a flat floor cannot.
 for(int i=0;i<256;++i){
  auto const&s=surfaces[i];if(!s.voidFloor||s.water>s.height)continue;
  int X=x0+i%16,Z=z0+i/16;auto col=raw.data()+i*Height;
  // Only where the wall rises beside this column: a canopy overhead.
  int ceiling=-32768;
  for(int d=1;d<=6;++d){
   auto b=field.surface(X+d+32768,Z+32768,false);
   if(b.voidRegion>0&&!b.voidFloor&&!b.voidPlatform){ceiling=std::max(ceiling,b.height);break;}
  }
  if(ceiling<0)continue;
  int top=std::min(Height-66,ceiling-1);
  if(top<=s.height+2)continue;
  double hang=hash01(X,Z,terrainSeed^71451);
  // A cave-vine curtain, ending in a berry cluster.
  if(hang<.22){
   int len=2+int(hash01(X,Z,terrainSeed^71453)*(top-s.height-2));
   len=std::min(len,top-s.height-2);
   for(int k=0;k<len;++k){
    int y=top-k;if(y<=s.height)break;
    if(col[y+64]==0)col[y+64]=uint16_t(k==len-1?RiftCaveVineBerries:RiftCaveVine);
   }
   continue;
  }
  if(hang<.27){if(col[top+64]==0)col[top+64]=RiftSporeBlossom;continue;}
  // Glow lichen on the shaded underside of the overhang.
  if(hang<.40&&col[top+64]==0)col[top+64]=RiftGlowLichen[int(hash01(X,Z,terrainSeed^71455)*5)];
 }
 // Old-growth trunks. Only the columns owned by this chunk are written.
 for(int gz=int(std::floor((wz0-8)/11.));gz<=int(std::floor((wz0+23)/11.));++gz)
 for(int gx=int(std::floor((wx0-8)/11.));gx<=int(std::floor((wx0+23)/11.));++gx){
  auto t=riftTree(gx,gz);if(!t.valid)continue;
  // A cached voxel model per tree keeps neighbouring chunks identical and
  // makes a limb/crown write cheap: build once, clip to this chunk.
  auto model=riftTreeModel(gx,gz);
  for(auto const&c:model->cells){
   int X=c.x-wx0,Z=c.z-wz0;
   if(X<0||X>=16||Z<0||Z>=16)continue;
   int y=c.y+64;if(y<0||y>=Height)continue;
   auto col=raw.data()+((Z)*16+X)*Height;
   // Trunks overwrite plants; leaves and moss never erase existing wood.
   if(c.block==RiftBark||c.block==RiftWood||c.block==RiftRoot)col[y]=c.block;
   else if(col[y]==0||col[y]==15)col[y]=c.block;
  }
 }
 // The rift lake is the floor's water source. Lilies, reeds and a mud ring
 // make it read as a pond rather than a flat water plane.
 for(int i=0;i<256;++i){
  auto const&s=surfaces[i];if(!s.voidFloor)continue;
  int X=x0+i%16,Z=z0+i/16;auto col=raw.data()+i*Height;
  if(s.water>s.height&&s.water<Height-65){
   double pick=hash01(X,Z,terrainSeed^71461);
   // Lilies float on the surface; reeds stand on the shallow bank.
   if(col[s.water+64]==8&&col[s.water+65]==0&&pick<.16)col[s.water+65]=86;
   continue;
  }
  // Dry column directly beside water: mud, reeds, then ferns inland.
  bool shore=false;
  for(auto [dx,dz]:{std::pair{1,0},std::pair{-1,0},std::pair{0,1},std::pair{0,-1}}){
   auto b=field.surface(X+dx+32768,Z+dz+32768,false);
   if(b.voidFloor&&b.water>b.height){shore=true;break;}
  }
  if(!shore)continue;
  double pick=hash01(X,Z,terrainSeed^71463);
  if(col[s.height+64]==0)continue;
  if(pick<.44)col[s.height+64]=85;                 // mud ring
  if(col[s.height+65]==0&&pick<.34)col[s.height+65]=87;  // reeds
  else if(col[s.height+65]==0&&pick<.50)col[s.height+65]=RiftFern;
 }
 // Hanging vines on the shaded wall band, and moss shelves on the terraces.
 for(int i=0;i<256;++i){
  auto const&s=surfaces[i];if(!s.voidFloor||s.water>s.height)continue;
  int X=x0+i%16,Z=z0+i/16;auto col=raw.data()+i*Height;
  // Vines only on the chasm wall: a solid horizontal neighbour must be at
  // least three blocks higher than this floor column.
  double pick=hash01(X,Z,terrainSeed^71431);
  if(pick>.42)continue;
  bool wall=false;
  for(auto [dx,dz]:{std::pair{1,0},std::pair{-1,0},std::pair{0,1},std::pair{0,-1}}){
   auto b=field.surface(X+dx+32768,Z+dz+32768,false);
   if(b.voidRegion>0&&!b.voidFloor&&b.height>=s.height+3){wall=true;break;}
  }
  if(!wall)continue;
  int top=std::min(Height-66,s.height+3);
  for(int y=top;y>=s.height;--y){
   if(col[y+64]==0)continue;
   if(y+65<Height&&col[y+65]==0&&y+1<=s.height+3){col[y+65]=76;}
   break;
  }
 }
}
// --- sky islands ----------------------------------------------------------
void skyIslandDeco(int x0,int z0,std::vector<uint16_t>&raw,std::array<Surface,256>const&surfaces){
 if(!skyIslandsEnabled())return;
 bool any=false;for(auto const&s:surfaces)any|=s.voidPlatform;if(!any)return;
 // A platform's top is flat, so a pond needs a lip: the water sits one block
 // below the grass and is ringed by the platform's own stone, never floating.
 for(int i=0;i<256;++i){
  auto const&s=surfaces[i];if(!s.voidPlatform)continue;
  int X=x0+i%16,Z=z0+i/16;auto col=raw.data()+i*Height;int h=s.voidTop;
  if(col[h+64]!=4&&col[h+64]!=3)continue;
  double pick=hash01(X,Z,terrainSeed^71501);
  double patch=voidNoiseAt(X/11.+41.,Z/11.-29.);
  // The cap is never empty: the ground plant and the cap material are chosen
  // independently, so a column cannot fall through every branch.
  // Coarse patches expose the platform's own dirt instead of grass.
  if(patch>.52&&pick<.22)col[h+64]=33;
  // Ground cover: fern is the default island plant, grass is the sparse one.
  double plant=hash01(X,Z,terrainSeed^71511);
  if(col[h+65]==0){
   if(plant<.34)col[h+65]=RiftFern;
   else if(plant<.46)col[h+65]=36;
   else if(plant<.52)col[h+65]=pick<.26?63:64;   // poppy / oxeye daisy
  }
  // Shallow pond in a broad depression of the noise field.
  if(patch<-.62){
   // Only carve when the whole 3x3 stays on the platform, so the lip holds.
   bool solid=true;
   for(int dz=-1;dz<=1&&solid;++dz)for(int dx=-1;dx<=1;++dx){
    auto b=field.surface(X+dx+32768,Z+dz+32768,false);
    if(!b.voidPlatform||b.voidTop!=h){solid=false;break;}
   }
   if(solid){col[h+64]=8;col[h+65]=0;continue;}
  }
  // Under the rim: roots and hanging vines. These are the island's signature
  // and must not depend on the cap lottery above.
  bool rim=false;
  for(auto [dx,dz]:{std::pair{3,0},std::pair{-3,0},std::pair{0,3},std::pair{0,-3}}){
   auto b=field.surface(X+dx+32768,Z+dz+32768,false);
   if(!b.voidPlatform)rim=true;
  }
  if(!rim)continue;
  // Roots and vines hang BELOW the keel, in the void. Writing them upward
  // inside the platform body silently did nothing: those cells are stone.
  double hang=hash01(X,Z,terrainSeed^71513);
  int base=s.voidBottom-1;
  if(base+64<1)continue;
  if(hang<.42){
   int len=3+int(hash01(X,Z,terrainSeed^71505)*10);
   for(int k=1;k<=len;++k){
    int y=base-(k-1);if(y+64<0)break;
    if(col[y+64]==0)col[y+64]=uint16_t(k==len?RiftRoot:61);
   }
   continue;
  }
  int len=2+int(hash01(X,Z,terrainSeed^71503)*7);
  for(int k=1;k<=len;++k){
   int y=base-(k-1);if(y+64<0)break;
   if(col[y+64]==0)col[y+64]=76;
  }
 }
 // Flowering pockets: a small azalea stand where the cap is damp.
 for(int i=0;i<256;++i){
  auto const&s=surfaces[i];if(!s.voidPlatform)continue;
  int X=x0+i%16,Z=z0+i/16;auto col=raw.data()+i*Height;int h=s.voidTop;
  if(col[h+64]!=4||col[h+65]!=0)continue;
  double bloom=voidNoiseAt(X/7.3+61.,Z/7.3-43.);
  if(bloom<.42)continue;
  double pick=hash01(X,Z,terrainSeed^71507);
  if(pick<.14)col[h+65]=pick<.05?RiftFloweringAzalea:RiftAzalea;
 }
}
// Sky-island trees. A platform is small, so a tree there must stay inside the
// lens: the anchor requires a platform footprint, and the crown is sized from
// the measured clearance rather than from a constant.
static constexpr uint16_t SkyBark=10;      // oak_log
static constexpr uint16_t SkyLeaf=11;      // oak_leaves
static constexpr uint16_t SkyWood=69;      // oak_wood
struct SkyCell {int x,y,z;uint16_t block;};
struct SkyModel {int top=0;std::vector<SkyCell> cells;};
Cache<uint64_t,std::shared_ptr<SkyModel>> skyTreeShapes{192};

std::shared_ptr<SkyModel> skyTreeModel(int gx,int gz){
 return skyTreeShapes.get(key2(gx,gz),[&]{
  auto out=std::make_shared<SkyModel>();
  auto rnd=[&](int salt){return hash01(gx,gz,terrainSeed^salt);};
  if(rnd(71511)>.34)return out;
  int x=gx*9+int(rnd(71513)*9),z=gz*9+int(rnd(71515)*9);
  auto s=field.surface(x+32768,z+32768,false);
  if(!s.voidPlatform)return out;
  // Measure the real clearance in eight directions; the crown radius is the
  // smallest platform half-width, capped so a tree never overhangs the keel.
  int clearance=6;bool fits=true;
  int lo=s.voidTop,hi=s.voidTop;
  for(int k=0;k<8;++k){
   double a=k*.7853981634;
   int reach=0;
   for(int d=2;d<=7;++d){
    auto b=field.surface(x+roundJS(std::cos(a)*d)+32768,z+roundJS(std::sin(a)*d)+32768,false);
    if(!b.voidPlatform)break;
    reach=d;lo=std::min(lo,b.voidTop);hi=std::max(hi,b.voidTop);
   }
   if(reach<2){fits=false;break;}
   clearance=std::min(clearance,reach);
  }
  if(!fits||hi-lo>1||clearance<3)return out;
  out->top=s.voidTop;
  auto add=[&](int X,int Y,int Z,uint16_t b){
   if(Y>=-59&&Y<=Height-65)out->cells.push_back({X,Y,Z,b});
  };
  double phase=rnd(71519)*6.283185307179586;
  int height=5+int(rnd(71517)*4)+clearance/3;
  double lean=.10+rnd(71521)*.14;
  // Leaning trunk: one-block offset every few blocks, so it is visibly curved.
  int cx=x,cz=z;
  for(int y=1;y<=height;++y){
   if(y%3==0){cx+=roundJS(std::sin(phase)*lean*3);cz+=roundJS(std::cos(phase)*lean*3);}
   add(cx,s.voidTop+y,cz,y==height?SkyWood:SkyBark);
  }
  // Two umbrella tiers with a branch reaching into each: the layered look the
  // other forest biomes have, scaled down to a platform.
  double r=std::min(double(clearance)-1.,2.6+rnd(71523)*1.4);
  auto tier=[&](int ty,double tr,int droop){
   int span=int(std::ceil(tr));
   for(int dz=-span;dz<=span;++dz)for(int dx=-span;dx<=span;++dx){
    double d=std::hypot(double(dx),double(dz));
    double edge=tr*(1+.14*std::sin(std::atan2(double(dz),double(dx))*3+phase));
    if(d>edge)continue;
    // Slight droop at the rim, flat at the centre: a mushroom-cap silhouette.
    int y=ty-(d>edge*.66?droop:0);
    add(cx+dx,y,cz+dz,SkyLeaf);
   }
  };
  tier(s.voidTop+height,2.2+rnd(71525)*1.2,1);
  tier(s.voidTop+height-3,r+0.4,0);
  for(int k=0;k<2;++k){
   double a=phase+k*3.14159265;
   int bx=cx+roundJS(std::cos(a)*(r*.75)),bz=cz+roundJS(std::sin(a)*(r*.75));
   int by=s.voidTop+height-3;
   add(bx,by,bz,SkyWood);
   tier(by+1,r*.62,0);
  }
  return out;
 });
}
void skyIslandTrees(int x0,int z0,std::vector<uint16_t>&raw,std::array<Surface,256>const&surfaces){
 if(!skyIslandsEnabled())return;
 int wx0=x0-32768,wz0=z0-32768;
 bool any=false;for(auto const&s:surfaces)any|=s.voidPlatform;if(!any)return;
 for(int gz=int(std::floor((wz0-4)/9.));gz<=int(std::floor((wz0+19)/9.));++gz)
 for(int gx=int(std::floor((wx0-4)/9.));gx<=int(std::floor((wx0+19)/9.));++gx){
  auto model=skyTreeModel(gx,gz);
  for(auto const&c:model->cells){
   int X=c.x-wx0,Z=c.z-wz0;
   if(X<0||X>=16||Z<0||Z>=16)continue;
   int y=c.y+64;if(y<0||y>=Height)continue;
   auto col=raw.data()+((Z)*16+X)*Height;
   if(c.block==SkyBark||c.block==SkyWood)col[y]=c.block;
   else if(col[y]==0)col[y]=c.block;
  }
 }
}
