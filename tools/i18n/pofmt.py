import re,sys

def unescape(s):
    return s.encode('utf-8').decode('unicode_escape').encode('latin-1','backslashreplace').decode('utf-8') if False else _un(s)

def _un(s):
    out=[];i=0
    while i<len(s):
        c=s[i]
        if c=='\\' and i+1<len(s):
            n=s[i+1]
            mp={'n':'\n','t':'\t','r':'\r','"':'"','\\':'\\','a':'\a','b':'\b','f':'\f','v':'\v'}
            if n in mp: out.append(mp[n]); i+=2; continue
            out.append(n); i+=2; continue
        out.append(c); i+=1
    return ''.join(out)

def esc(s):
    s=s.replace('\\','\\\\').replace('"','\\"').replace('\n','\\n').replace('\t','\\t').replace('\r','\\r')
    return s

class Entry:
    __slots__=('comments','msgctxt','msgid','msgid_plural','msgstr')
    def __init__(self):
        self.comments=[]; self.msgctxt=None; self.msgid=None; self.msgid_plural=None; self.msgstr={}

def parse(path):
    entries=[]; cur=Entry(); key=None
    def flush():
        nonlocal cur
        if cur.msgid is not None or cur.comments:
            entries.append(cur)
        cur=Entry()
    for raw in open(path,encoding='utf-8'):
        l=raw.rstrip('\n')
        if l.startswith('#'):
            if cur.msgid is not None: flush()
            cur.comments.append(l); key=None; continue
        if not l.strip():
            if cur.msgid is not None or cur.comments: flush()
            key=None; continue
        m=re.match(r'^(msgctxt|msgid_plural|msgid|msgstr(?:\[(\d+)\])?)\s+"(.*)"$', l)
        if m:
            kind=m.group(1).split('[')[0]; val=m.group(3)
            if kind=='msgid' and cur.msgid is not None: flush()
            if kind=='msgstr':
                idx=int(m.group(2)) if m.group(2) else 0
                cur.msgstr[idx]=val; key=('msgstr',idx)
            elif kind=='msgctxt': cur.msgctxt=val; key=('msgctxt',None)
            elif kind=='msgid': cur.msgid=val; key=('msgid',None)
            else: cur.msgid_plural=val; key=('msgid_plural',None)
            continue
        m=re.match(r'^"(.*)"$', l)
        if m and key:
            if key[0]=='msgstr': cur.msgstr[key[1]]+=m.group(1)
            elif key[0]=='msgctxt': cur.msgctxt+=m.group(1)
            elif key[0]=='msgid': cur.msgid+=m.group(1)
            else: cur.msgid_plural+=m.group(1)
            continue
        raise SystemExit("UNPARSED: %r" % l)
    flush()
    return [e for e in entries if e.msgid is not None or e.comments]

def render_field(name, value):
    # value is the raw escaped string (single logical line)
    return '%s "%s"\n' % (name, value)

def dump(entries, path):
    with open(path,'w',encoding='utf-8') as f:
        first=True
        for e in entries:
            if not first: f.write('\n')
            first=False
            for c in e.comments: f.write(c+'\n')
            if e.msgctxt is not None: f.write(render_field('msgctxt', e.msgctxt))
            if e.msgid is not None: f.write(render_field('msgid', e.msgid))
            if e.msgid_plural is not None: f.write(render_field('msgid_plural', e.msgid_plural))
            for idx in sorted(e.msgstr):
                nm='msgstr' if e.msgid_plural is None else 'msgstr[%d]'%idx
                f.write(render_field(nm, e.msgstr[idx]))
