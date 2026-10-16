// Standalone reset/reuse pressure: many independent functions, no includes.
extern unsigned arena_observe(unsigned);
struct ArenaPass { unsigned words[4]; };
extern const ArenaPass arena_symbol;
extern void arena_call(unsigned,const ArenaPass&);
template<int N> unsigned arena_template(unsigned value) {
  return arena_observe(value+N);
}
template<int N> struct ArenaBox {
  unsigned words[N];
  ArenaBox(unsigned value) { for(int i=0;i<N;++i) words[i]=value+i; }
  ~ArenaBox() { arena_observe(words[N-1]); }
  unsigned get(unsigned i) const { return words[i%N]; }
};
static inline unsigned arena_inline(unsigned value) {
  unsigned nested[2]={value,value+1};
  return arena_observe(nested[0])+nested[1];
}
#pragma opt_unroll_loops off
extern "C" unsigned arena_function_00(const void* condition,unsigned input) {
  ArenaBox<3> box(input+0u);
  unsigned flag[2]; int k;
  for(k=1;k<2;++k) flag[k]=condition?0u:1u;
  if(k>0) arena_call(arena_inline(input),arena_symbol);
  if(arena_observe(input)) flag[1]=box.get(input);
  for(unsigned j=0;j<3;++j) input+=arena_observe(j+flag[1]);
  return arena_template<5>(input)+flag[1];
}
extern "C" unsigned arena_function_01(const void* condition,unsigned input) {
  ArenaBox<3> box(input+1u);
  unsigned flag[2]; int k;
  for(k=1;k<2;++k) flag[k]=condition?0u:1u;
  if(k>0) arena_call(arena_inline(input),arena_symbol);
  if(arena_observe(input)) flag[1]=box.get(input);
  for(unsigned j=0;j<3;++j) input+=arena_observe(j+flag[1]);
  return arena_template<5>(input)+flag[1];
}
extern "C" unsigned arena_function_02(const void* condition,unsigned input) {
  ArenaBox<3> box(input+2u);
  unsigned flag[2]; int k;
  for(k=1;k<2;++k) flag[k]=condition?0u:1u;
  if(k>0) arena_call(arena_inline(input),arena_symbol);
  if(arena_observe(input)) flag[1]=box.get(input);
  for(unsigned j=0;j<3;++j) input+=arena_observe(j+flag[1]);
  return arena_template<5>(input)+flag[1];
}
extern "C" unsigned arena_function_03(const void* condition,unsigned input) {
  ArenaBox<3> box(input+3u);
  unsigned flag[2]; int k;
  for(k=1;k<2;++k) flag[k]=condition?0u:1u;
  if(k>0) arena_call(arena_inline(input),arena_symbol);
  if(arena_observe(input)) flag[1]=box.get(input);
  for(unsigned j=0;j<3;++j) input+=arena_observe(j+flag[1]);
  return arena_template<5>(input)+flag[1];
}
#pragma opt_unroll_loops reset
