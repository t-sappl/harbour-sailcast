import re, os, sys, glob, json
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape
root = sys.argv[1]
ts = os.path.join(root, 'translations/harbour-sailcast-de.ts')
extra = json.load(open(sys.argv[2])) if len(sys.argv) > 2 else {}
# existing translations by source (any context), plus by (context, source)
known = {}
t = ET.parse(ts)
for ctx in t.getroot().findall('context'):
    for m in ctx.findall('message'):
        src = m.find('source').text
        tr = m.find('translation')
        nf = tr.findall('numerusform')
        known[src] = [n.text for n in nf] if nf else tr.text
known.update(extra)
strpat = r'"((?:[^"\\]|\\.)*)"'
call = re.compile(r'\b(qsTr|tr)\(\s*' + strpat + r'(\s*,\s*' + strpat + r'\s*,)?')
def unq(s): return s.encode().decode('unicode_escape').encode('latin1').decode('utf-8')
contexts = {}
files = sorted(glob.glob(root + '/qml/**/*.qml', recursive=True)) + [root + '/src/appcore.cpp']
for f in files:
    ctx = 'AppCore' if f.endswith('.cpp') else os.path.splitext(os.path.basename(f))[0]
    text = open(f, encoding='utf-8').read()
    for m in call.finditer(text):
        if f.endswith('.cpp') and m.group(1) != 'tr': continue
        if f.endswith('.qml') and m.group(1) != 'qsTr': continue
        src = unq(m.group(2))
        numerus = m.group(3) is not None
        lst = contexts.setdefault(ctx, [])
        if not any(s == src for s, _ in lst):
            lst.append((src, numerus))
missing = []
out = ['<?xml version="1.0" encoding="utf-8"?>', '<!DOCTYPE TS>', '<TS version="2.1" language="de">']
for ctx in sorted(contexts):
    out.append('<context>'); out.append('    <name>%s</name>' % ctx)
    for src, numerus in contexts[ctx]:
        tr = known.get(src)
        if tr is None: missing.append((ctx, src)); tr = [] if numerus else ''
        if numerus:
            if isinstance(tr, str): tr = [tr, tr]
            out.append('    <message numerus="yes">'); out.append('        <source>%s</source>' % escape(src))
            out.append('        <translation>')
            for n in tr: out.append('            <numerusform>%s</numerusform>' % escape(n))
            out.append('        </translation>'); out.append('    </message>')
        else:
            if isinstance(tr, list): tr = tr[-1]
            out.append('    <message>'); out.append('        <source>%s</source>' % escape(src))
            out.append('        <translation>%s</translation>' % escape(tr)); out.append('    </message>')
    out.append('</context>')
out.append('</TS>')
if missing:
    print('MISSING:'); [print(' ', c, '|', s) for c, s in missing]
else:
    open(ts, 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    print('written', sum(len(v) for v in contexts.values()), 'messages')
