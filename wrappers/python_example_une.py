# Written by Une Butaite


# =============================================================================
# =============================================================================
# =============================================================================
#     COMMENT OUT THE FOLLOWING IN BITPACK HOLOGRAMS AND INSERT FRAME
#     if not phase.flags['C_CONTIGUOUS']:
#         phase = np.ascontiguousarray(phase)
# =============================================================================
# =============================================================================
# =============================================================================



import numpy as np
import ctypes
from PLMController import PLMController 
import matplotlib.pyplot as plt
import time


MAX_FRAMES = 4
N = 1358
M = 800

fullpath = r'C:\Users\ub217\OneDrive - University of Exeter\Documents\PYTHON\plmctrl-bitpack-gpu\bin\plmctrl.dll'

# Create PLMController instance
plm = PLMController(MAX_FRAMES, N, M, fullpath)

# set phase levels
phase_levels = np.array((0.004, 0.017, 0.036, 0.058, 0.085, 0.117, 0.157, 0.217, 0.296, 0.4, 0.5, 0.605, 0.713, 0.82, 0.922, 0.981, 1), dtype=np.float32)
plm.set_lookup_table(phase_levels)

# set phase map
phase_map = np.array([
    [0, 0, 0, 0],
    [1, 0, 0, 0],
    [0, 1, 0, 0],
    [1, 1, 0, 0],
    [0, 0, 1, 0],
    [1, 0, 1, 0],
    [0, 1, 1, 0],
    [1, 1, 1, 0],
    [0, 0, 0, 1],
    [1, 0, 0, 1],
    [0, 1, 0, 1],
    [1, 1, 0, 1],
    [0, 0, 1, 1],
    [1, 0, 1, 1],
    [0, 1, 1, 1],
    [1, 1, 1, 1],
])
phase_map_order = (13, 0, 9, 5, 1, 14, 10, 6, 2, 15, 11, 7, 3, 12, 8, 4)
phase_map = phase_map[phase_map_order,:]
plm.set_phase_map(phase_map)

# Start the UI
monitor_id = 1
plm.start_ui(monitor_id)





#%% INSERTING ONE FRAME AT A TIME
# if inserting one frame at a time, the frame does not need to be transposed and in F-order,
# but the phase does

numHolograms = 24

for i in range(MAX_FRAMES):
    phase = np.zeros((M,N, numHolograms), dtype=np.float32)
    a = np.linspace(0, i*2+1, np.shape(phase)[1])[None, :]
    b = np.linspace(0, 0, np.shape(phase)[0])[:, None]
    ph = np.mod(a + b, 1)
    phase[:,:,:] = np.tile(ph[:, :, np.newaxis], (1, 1, 24))
    
    phase = np.asfortranarray(np.transpose(phase, (1, 0, 2)))

    plm.bitpack_and_insert_gpu(phase, i)
    
    # frame = plm.bitpack_holograms_gpu(phase)
    # frame = np.expand_dims(frame, axis = 2)
    # plm.insert_frames(frame, i, format=1)
    




#%% INSERTING MULTIPLE FRAMES AT ONCE
# if inserting multiple frames at a time, frames need to be transposed and in F-order,
# as well as the phase

numHolograms = 24

frames = np.zeros((4*2*N, 2*M, MAX_FRAMES), dtype=np.uint8)
for i in range(MAX_FRAMES):
    phase = np.zeros((M,N, numHolograms),dtype=np.float32)
    a = np.linspace(0, 0, np.shape(phase)[1])[None, :]
    b = np.linspace(0, i*2+1, np.shape(phase)[0])[:, None]
    ph = np.mod(a + b, 1)
    phase[:,:,:] = np.tile(ph[:, :, np.newaxis], (1, 1, 24))
    
    # plt.imshow(phase[:,:,0]); plt.draw(); plt.pause(0.01)
    phase = np.asfortranarray(np.transpose(phase, (1, 0, 2)))
    
    frames[:,:,i] = plm.bitpack_holograms_gpu(phase)
    
frames = np.asfortranarray(np.transpose(frames, (1, 0, 2)))
plm.insert_frames(frames, 0, format=1)





#%% INSERT ONE FRAME
# if inserting one frame at a time, the frame does not need to be transposed and in F-order,
# but the phase does

phase = np.zeros((M,N,24), dtype=np.float32)
phase[:M//4,:N//4,:]     = 0
phase[:M//4,N//4:-1,:]   = 0.2
phase[M//4:-1,:N//4,:]   = 0.3
phase[M//4:-1,N//4:-1,:] = 0.9

plt.imshow(phase[:,:,0])
phase = np.asfortranarray(np.transpose(phase, (1, 0, 2)))

frame = plm.bitpack_holograms_gpu(phase)
frame = np.expand_dims(frame, axis = 2)
plm.insert_frames(frame, 0, format=1)







#%%

p1 = np.arange(0, 35)
p1 = np.reshape(p1, (5, 7))
p2 = np.arange(35, 70)
p2 = np.reshape(p2, (5, 7))

phase_C = np.stack((p1,p2), axis=2)
phase_C_flat = phase_C.flatten()


# phase_F = np.asfortranarray(np.transpose(phase_C, (1, 0, 2)))
phase_F = np.transpose(phase_C, (1, 0, 2))
phase_F_flat = phase_F.flatten(order='F')




#%%
plm.pause_ui()




#%%
plm.resume_ui()




#%%
plm.cleanup()






