from pathlib import Path
import ctypes,numpy as np
p=Path(__file__).resolve().parent
cap=Path('C:/Users/brett/Documents/Codex/work/arceus-games/ArceusTest/captures')
def load(stamp):
 return np.fromfile(next(cap.glob('*'+stamp+'*cb4.bin')),'<f4')[48:64].reshape(4,4)
fn=ctypes.CDLL(str(p/'test_camera.dll')).camera_history
arr=np.ctypeslib.ndpointer(dtype=np.float32,ndim=2,flags='C_CONTIGUOUS')
fn.argtypes=[arr,arr,arr];fn.restype=ctypes.c_int
prev=load('192104')
def check(name,cur,want):
 out=np.zeros((4,4),'f4')
 assert fn(cur,prev,out)==want,name
 if want:np.testing.assert_allclose(out,cur.astype(float)@np.linalg.inv(prev.astype(float)),atol=1e-4)
 print(name,'pass')
check('captured stationary pose',prev,1)
# A 0.2-world-unit lateral movement along the camera axis; max translation
# coefficient exceeds 1, but the 2m plane moves only ~10% of the screen.
axis=prev[0,:3].astype(float);axis/=np.linalg.norm(axis)
shift=np.eye(4);shift[3,:3]=.2*axis
m=(prev@shift).astype('f4')
assert np.max(np.abs(m@np.linalg.inv(prev)-np.eye(4)))>1
check('small physical camera translation',m,1)
check('captured change from close-up to wide',load('192112'),0)
check('invalid camera matrix',np.full((4,4),np.nan,'f4'),0)
