#!/usr/bin/env python3
"""Build reproducible active-profile CLI twins without rebuilding paused x64 code."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
X64_ARCHIVE_SHA = '1720431862157caa3a44c2cbd47fb4c6f64b75d5b3b22ccf70d0dd9b1b322ce6'
OBJECTS = tuple('CMakeFiles/boothush.dir/src/'+name+'.obj' for name in
                ('main.c','pc286.cpp','video_capture.c','media_convert.c'))
LIBRARIES = ('libboothush_mac.a','libi286.a','libboothush_nes_core.a',
             'libmoira.a','libboothush_devices.a')

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('cmake','ninja','compiler-bin','x64-media-archive','output'):
        p.add_argument('--'+name,required=True,type=Path)
    a=p.parse_args()
    cmake=a.cmake.resolve(strict=True);ninja=a.ninja.resolve(strict=True)
    compiler=a.compiler_bin.resolve(strict=True);archive=a.x64_media_archive.resolve(strict=True)
    out=a.output.resolve()
    if out==ROOT or ROOT in out.parents:raise ValueError('output must be outside repository')
    if sha(archive)!=X64_ARCHIVE_SHA:raise ValueError('unexpected retained x64 archive identity')
    cc=compiler/'clang.exe';cxx=compiler/'clang++.exe'
    paths=[ROOT/'CMakeLists.txt',Path(__file__),cmake,ninja,cc,cxx,archive]
    # Hash source and headers used by the active targets, including vendored
    # CPU dependencies. The paused x64 implementation is never compiled here.
    for folder in (ROOT/'src',ROOT/'include',ROOT/'cmake',ROOT/'third_party/moira',ROOT/'third_party/mame-i286'):
        paths.extend(x for x in folder.rglob('*') if x.is_file() and
                     (x.suffix.lower() in ('.c','.cpp','.h','.hpp','.inc','.in','.cmake')) and
                     (not x.name.startswith('x64_') or x.suffix.lower() in ('.h','.hpp')))
    paths=sorted(set(paths),key=str);before={str(x):sha(x) for x in paths}
    out.mkdir(parents=True,exist_ok=False)
    env=os.environ.copy();env['PATH']=str(compiler)+os.pathsep+env.get('PATH','')
    env['SOURCE_DATE_EPOCH']='0';commands=[];executables=[]
    for name in ('a','b'):
        build=out/name
        configure=[cmake,'-S',ROOT,'-B',build,'-G','Ninja',
                   '-DCMAKE_MAKE_PROGRAM='+str(ninja),'-DCMAKE_C_COMPILER='+str(cc),
                   '-DCMAKE_CXX_COMPILER='+str(cxx),'-DCMAKE_BUILD_TYPE=Release',
                   '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DBUILD_TESTING=OFF']
        targets=[cmake,'--build',build,'--target','boothush_mac','i286','boothush_nes_core']
        objects=[ninja,'-C',build,*OBJECTS]
        executable=build/'boothush-active.exe'
        link=[cxx,'-O3','-DNDEBUG','-municode','-static','-Wl,--no-insert-timestamp',
              *(build/x for x in OBJECTS),build/LIBRARIES[0],build/LIBRARIES[1],archive,
              *(build/x for x in LIBRARIES[2:]),'-luser32','-lgdi32','-lbcrypt',
              '-lkernel32','-lwinspool','-lshell32','-lole32','-loleaut32','-luuid',
              '-lcomdlg32','-ladvapi32','-o',executable]
        for label,cmd in (('configure',configure),('active-libraries',targets),('cli-objects',objects),('link',link)):
            args=[str(x) for x in cmd];commands.append(args)
            result=subprocess.run(args,cwd=ROOT,env=env,capture_output=True,text=True,timeout=180,
                                  creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            # Configure creates the build directory before the first log.
            (build/(label+'.log')).write_text(result.stdout+'\n--- stderr ---\n'+result.stderr,encoding='utf-8')
            if result.returncode:raise RuntimeError(label+' failed: '+str(build/(label+'.log')))
        executables.append(executable)
    if sha(executables[0])!=sha(executables[1]):raise ValueError('active-profile twins differ')
    if before!={str(x):sha(x) for x in paths}:raise ValueError('build input changed')
    report={'status':'pass','commands':commands,'input_sha256':before,
            'executable_sha256':sha(executables[0]),'source_date_epoch':'0',
            'outputs':{name:{p:sha(out/name/p) for p in (*OBJECTS,*LIBRARIES,'compile_commands.json')}
                       for name in ('a','b')},
            'limits':'Active Mac/286/NES sources rebuilt; an existing x64 media archive is linked unchanged. Not a source-complete distribution or third-party license review.'}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'status':'pass','executable_sha256':report['executable_sha256'],'report':str(out/'report.json')}))

if __name__=='__main__':main()
