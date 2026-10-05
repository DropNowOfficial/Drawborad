"""Real child process; independent Python wire encoder/decoder."""
import hashlib,struct,subprocess,sys
exe=sys.argv[1]
def packet(kind=1):
 text=b'DBIPC1';payload=b'process probe';body=struct.pack('<Q',len(text))+text+struct.pack('<QQ',1,kind)+bytes(range(16))+bytes(range(16,32))+struct.pack('<Q',len(payload))+payload
 return struct.pack('<Q',len(body))+hashlib.sha256(body).digest()+body
for k in [1,4]:
 r=subprocess.run([exe,'--stdio'],input=packet(k),stdout=subprocess.PIPE,timeout=10)
 assert r.returncode==0
 n=struct.unpack_from('<Q',r.stdout)[0];body=r.stdout[40:];assert n==len(body) and hashlib.sha256(body).digest()==r.stdout[8:40]
 assert struct.unpack_from('<Q',body,22)[0]==(5 if k==4 else 1)
for b in [b'',struct.pack('<Q',2**64-1),packet()[:-1]]:
 assert subprocess.run([exe,'--stdio'],input=b,stdout=subprocess.PIPE,timeout=10).returncode!=0
print('PASS real worker process: hello/cancel/oversize/truncated/empty')
