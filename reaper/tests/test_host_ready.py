# SPDX-License-Identifier: MPL-2.0
"""State tests only; native startup proof comes from initialization.json."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from host_ready import wait, decision, InitializationError
class Clock:
    now=0
    def time(self):return self.now
    def sleep(self,n):self.now+=n
class Fake:
    clicks=0
    def __init__(self,clock,blocked=False):self.clock=clock;self.blocked=blocked
    def snapshot(self):
        if self.blocked or self.clock.now<1:
            return [{'evaluation':True,'buttons':[{'id':4,'continue':True,'enabled':False}]}]
        if not self.clicks:return [{'evaluation':True,'buttons':[{'id':4,'continue':True,'enabled':True}]}]
        return [{'main':True,'enabled':True}]
    def click(self,control):
        assert self.clock.now>=1 and control==4
        self.clicks+=1
c=Clock();f=Fake(c);r=wait(f,5,clock=c.time,sleep=c.sleep)
assert r['countdown_observed'] and f.clicks==1 and r['status']=='ready'
c=Clock();f=Fake(c,True)
try:wait(f,2,clock=c.time,sleep=c.sleep);raise AssertionError('blocked evaluation accepted')
except InitializationError:pass
assert f.clicks==0
assert decision([{'main':True,'enabled':False}])[0]=='initializing'
assert decision([{'main':True,'enabled':True}])[0]=='ready'
print('Startup state OK: disabled waits, enabled ordinary click, timeout is initialization failure')
