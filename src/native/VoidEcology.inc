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
struct RiftTree {bool valid=false;int x=0,z=0,ground=0,height=0;double radius=0,phase=0;};
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
  t.valid=true;t.x=x;t.z=z;t.ground=s.height;
  t.height=26+int(rnd(71407)*16);t.radius=3.4+rnd(71409)*1.4;
  t.phase=rnd(71411)*6.283185307179586;
  if(t.ground+t.height>Height-66)return RiftTree{};
  return t;
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
  double lush=voidNoiseAt(X/21.+13.,Z/21.-7.);
  // Moss carpet on damp terraces, ferns and tall grass in the hollows.
  if(lush>.10&&pick<.34){col[h+64]=80;if(pick<.16)col[h+65]=120;continue;}
  if(pick<.20){col[h+65]=37;continue;}
  if(pick<.27){col[h+65]=36;}
 }
 // Old-growth trunks. Only the columns owned by this chunk are written.
 for(int gz=int(std::floor((wz0-8)/11.));gz<=int(std::floor((wz0+23)/11.));++gz)
 for(int gx=int(std::floor((wx0-8)/11.));gx<=int(std::floor((wx0+23)/11.));++gx){
  auto t=riftTree(gx,gz);if(!t.valid)continue;
  int n=int(std::ceil(t.radius));
  // Trunk column, then a flattened crown centred above the floor.
  for(int dz=-n;dz<=n;++dz)for(int dx=-n;dx<=n;++dx){
   int X=t.x+dx-wx0,Z=t.z+dz-wz0;if(X<0||X>=16||Z<0||Z>=16)continue;
   auto cell=raw.data()+((Z)*16+X)*Height;
   double d=std::hypot(double(dx),double(dz));
   double edge=t.radius*(1+.13*std::sin(std::atan2(double(dz),double(dx))*3+t.phase));
   // Roots and vines hang a few blocks down the wall side of the trunk.
   if(d>edge)continue;
   int crownLo=t.ground+t.height-int(3+.20*t.height),crownHi=t.ground+t.height;
   for(int y=crownLo;y<=crownHi;++y){
    double q=(d/edge);double vy=double(y-(crownLo+crownHi)*.5)/((crownHi-crownLo)*.5+.001);
    if(q*q+vy*vy*.85>1)continue;
    if(y+64<0||y+64>=Height)continue;
    if(cell[y+64]==0)cell[y+64]=15;
   }
   if(d<=1.01){
    for(int y=t.ground+1;y<t.ground+t.height;++y){
     if(y+64<0||y+64>=Height)continue;
     cell[y+64]=14;
     // Occasional side branch so the trunk reads as a tree, not a pole.
     if((y-t.ground)%7==3&&dx==0&&dz==0)for(auto [bx,bz]:{std::pair{1,0},std::pair{-1,0},std::pair{0,1},std::pair{0,-1}}){
      int bx2=X+bx,bz2=Z+bz;if(bx2<0||bx2>=16||bz2<0||bz2>=16)continue;
      auto other=raw.data()+((bz2)*16+bx2)*Height;if(other[y+64]==0)other[y+64]=14;
     }
    }
    if(t.ground+64<Height)cell[t.ground+64]=3;
   }
  }
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
 for(int i=0;i<256;++i){
  auto const&s=surfaces[i];if(!s.voidPlatform)continue;
  int X=x0+i%16,Z=z0+i/16;auto col=raw.data()+i*Height;int h=s.voidTop;
  if(col[h+64]!=4&&col[h+64]!=3)continue;
  double pick=hash01(X,Z,terrainSeed^71501);
  if(pick<.30){col[h+64]=4;if(col[h+65]==0&&pick<.16)col[h+65]=pick<.06?63:36;continue;}
  // Hanging vines under the rim: only where the platform edge is nearby.
  bool rim=false;
  for(auto [dx,dz]:{std::pair{3,0},std::pair{-3,0},std::pair{0,3},std::pair{0,-3}}){
   auto b=field.surface(X+dx+32768,Z+dz+32768,false);
   if(!b.voidPlatform)rim=true;
  }
  if(!rim||pick>.62)continue;
  int len=2+int(hash01(X,Z,terrainSeed^71503)*7);
  for(int y=1;y<=len&&h-y+64>0;++y)if(col[h-y+64]==0)col[h-y+64]=76;
 }
}
// A small tree on a wide platform: one trunk, a flat crown, nothing below.
void skyIslandTrees(int x0,int z0,std::vector<uint16_t>&raw,std::array<Surface,256>const&surfaces){
 if(!skyIslandsEnabled())return;
 int wx0=x0-32768,wz0=z0-32768;
 bool any=false;for(auto const&s:surfaces)any|=s.voidPlatform;if(!any)return;
 for(int gz=int(std::floor((wz0-4)/9.));gz<=int(std::floor((wz0+19)/9.));++gz)
 for(int gx=int(std::floor((wx0-4)/9.));gx<=int(std::floor((wx0+19)/9.));++gx){
  auto rnd=[&](int salt){return hash01(gx,gz,terrainSeed^salt);};
  if(rnd(71511)>.34)continue;
  int x=gx*9+int(rnd(71513)*9),z=gz*9+int(rnd(71515)*9);
  auto s=field.surface(x+32768,z+32768,false);
  if(!s.voidPlatform)continue;
  // Keep the crown inside the platform: four samples must all be platform.
  int lo=s.voidTop,hi=s.voidTop;bool fits=true;
  for(auto [dx,dz]:{std::pair{-2,0},std::pair{2,0},std::pair{0,-2},std::pair{0,2}}){
   auto b=field.surface(x+dx+32768,z+dz+32768,false);
   if(!b.voidPlatform){fits=false;break;}
   lo=std::min(lo,b.voidTop);hi=std::max(hi,b.voidTop);
  }
  if(!fits||hi-lo>1)continue;
  int trunk=3+int(rnd(71517)*3);
  for(int y=1;y<=trunk;++y){
   int X=x-wx0,Z=z-wz0;if(X<0||X>=16||Z<0||Z>=16)continue;
   auto col=raw.data()+((Z)*16+X)*Height;if(s.voidTop+y+64<Height)col[s.voidTop+y+64]=10;
  }
  for(int dz=-2;dz<=2;++dz)for(int dx=-2;dx<=2;++dx){
   if(dx*dx+dz*dz>5)continue;
   int X=x+dx-wx0,Z=z+dz-wz0;if(X<0||X>=16||Z<0||Z>=16)continue;
   auto col=raw.data()+((Z)*16+X)*Height;int y=s.voidTop+trunk;
   if(y+64<Height&&col[y+64]==0)col[y+64]=11;
  }
 }
}
