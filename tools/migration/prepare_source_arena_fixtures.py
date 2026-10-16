"""Generate deterministic standalone arena pressure without SDK/game inputs."""
import hashlib,json,pathlib

HERE=pathlib.Path(__file__).resolve().parent

def bucket(name):
    data=name.encode('ascii');length=len(data)&255;checksum=0
    for value in data[:length]:
        checksum=(((checksum>>3)|(checksum<<5))+value)&255
    return ((length<<8)|checksum)&2047

def colliding(prefix,count,target=None):
    names=[];wanted=target
    for n in range(100000):
        name=prefix+('%06d'%n)
        if wanted is None:wanted=bucket(name)
        if bucket(name)==wanted:names.append(name)
        if len(names)==count:return wanted,names
    raise RuntimeError('collision search bound exhausted')

def prepare(output):
    output.mkdir(parents=True,exist_ok=False)
    macro_bucket,macros=colliding('arena_macro_',40)
    export_bucket,exports=colliding('arena_export_',40)
    # CFunc_ParseFuncDef interns __FUNCTION__ with GetHashNameNodeExport.
    # Populate its bucket with a different macro first to stimulate the tail
    # allocation path, without declaring or redefining the reserved identifier.
    function_bucket,function_collision=colliding('arena_',1,bucket('__FUNCTION__'))
    lines=['// Generated fixed-length colliding names; see fixture-manifest.json.']
    lines += ['// Distinct name colliding with the automatically interned __FUNCTION__.',
              '#define %s 1u'%function_collision[0]]
    lines += ['#define %s %du'%(name,i+1) for i,name in enumerate(macros)]
    lines += ['unsigned %s = %s;'%(name,macros[i]) for i,name in enumerate(exports)]
    lines += ['extern "C" unsigned arena_collisions(unsigned value) {',
              '  unsigned result=value;']
    lines += ['  result += %s;'%name for name in exports]
    lines += ['  return result;','}']
    # The second mention of each macro/export is intentionally a same-name lookup.
    lines += ['unsigned arena_duplicate_lookup = %s;'%macros[0]]
    collision_source='\n'.join(lines)+'\n'
    body=['// Standalone reset/reuse pressure: many independent functions, no includes.',
          'extern unsigned arena_observe(unsigned);',
          'struct ArenaPass { unsigned words[4]; };',
          'extern const ArenaPass arena_symbol;',
          'extern void arena_call(unsigned,const ArenaPass&);',
          'template<int N> unsigned arena_template(unsigned value) {',
          '  return arena_observe(value+N);',
          '}',
          'template<int N> struct ArenaBox {',
          '  unsigned words[N];',
          '  ArenaBox(unsigned value) { for(int i=0;i<N;++i) words[i]=value+i; }',
          '  ~ArenaBox() { arena_observe(words[N-1]); }',
          '  unsigned get(unsigned i) const { return words[i%N]; }',
          '};',
          'static inline unsigned arena_inline(unsigned value) {',
          '  unsigned nested[2]={value,value+1};',
          '  return arena_observe(nested[0])+nested[1];',
          '}',
          '#pragma opt_unroll_loops off']
    for i in range(4):
        body += ['extern "C" unsigned arena_function_%02d(const void* condition,unsigned input) {'%i,
                 '  ArenaBox<3> box(input+%du);'%i,
                 '  unsigned flag[2]; int k;',
                 '  for(k=1;k<2;++k) flag[k]=condition?0u:1u;',
                 '  if(k>0) arena_call(arena_inline(input),arena_symbol);',
                 '  if(arena_observe(input)) flag[1]=box.get(input);',
                 '  for(unsigned j=0;j<3;++j) input+=arena_observe(j+flag[1]);',
                 '  return arena_template<5>(input)+flag[1];','}']
    body+=['#pragma opt_unroll_loops reset']
    cases=[]
    for name,source,aim in [('arena_collisions.cpp',collision_source,'40 same-bucket macro names and 40 same-bucket externally visible variable names; stimulates first/tail lookup attempts, not a priori runtime site coverage'),('arena_lifetimes.cpp','\n'.join(body)+'\n','4 independent exported functions, templates, nontrivial local destruction, inline argument calls, branches, loops and promoted-array patterns; repeated ordinary-pool resets/reuse are observations to verify')]:
        data=source.encode('ascii');(output/name).write_bytes(data)
        cases.append({'source':name,'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),'aim':aim})
    manifest={'format_version':1,'generator_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),'flags':['-nodefaults','-proc','gekko','-fp','hardware','-Cpp_exceptions','off','-O4,p'],'cases':cases,'collisions':[{'kind':'macro','bucket':macro_bucket,'names':macros},{'kind':'export','bucket':export_bucket,'names':exports},{'kind':'automatic_function_identifier_export_tail','bucket':function_bucket,'names':function_collision,'target':'__FUNCTION__'}],'scope':'These are source stimuli. Actual guarded runtime sites/pools/reset paths must be measured; no fixture promises locked reset/OOM/failure callback coverage.'}
    (output/'fixture-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps({'fixtures':len(cases),'output':str(output),'collision_buckets':[macro_bucket,export_bucket]}))

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=pathlib.Path,required=True)
    prepare(parser.parse_args().output.resolve())
