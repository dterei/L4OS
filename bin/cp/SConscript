from os import listdir as ls

Import("*")

addressing = env.WeaverAddressing(direct=True)
weaver = env.WeaverIguanaProgram(addressing = addressing)

libs = Split("c sos l4")

targetsrc = ''
targetname = ''

for file in ls('.'):
    if file.endswith('.c'):
        targetsrc = file
        targetname = file.rstrip('.c')
        break

target = env.KengeProgram(targetname, source=[targetsrc], weaver=weaver, LIBS=libs)
Return("target")

# vim: set filetype=python:
