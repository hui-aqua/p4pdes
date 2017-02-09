#!/usr/bin/env python

import numpy as np
from numpy.random import rand
import matplotlib.pyplot as plt
from matplotlib import cm
from mpl_toolkits.mplot3d import axes3d

L = 1800.0e3
Nx = 80
Ny = 80
pi = np.pi

dx = L / float(Nx)
dy = L / float(Ny)
x = np.linspace(0,L - dx, Nx)
y = np.linspace(0,L - dy, Ny)
xx, yy = np.meshgrid(x,y)

# frequencies and coefficients generated by fiddling
nc = 4
jc = [1, 3, 6, 8]
kc = [1, 3, 4, 7]
cr = np.array([[ 2.00000000,  0.33000000, -0.55020034,  0.54495520],
               [ 0.50000000,  0.45014486,  0.60551833, -0.52250644],
               [ 0.93812068,  0.32638429, -0.24654812,  0.33887052],
               [ 0.17592361, -0.35496741,  0.22694547, -0.05280704]])
c = 750.0 * cr

# use ELA = 2000.0 m

b = np.zeros(np.shape(xx))
for j in range(nc):
    for k in range(nc):
        b += c[j,k] * np.sin(jc[j] * pi * xx / L) * np.sin(kc[k] * pi * yy / L)

surface = True
fig = plt.figure()
if surface:
    ax = fig.gca(projection='3d')
    h = ax.plot_surface(xx/1000.0,yy/1000.0,b,
                        rstride=1, cstride=1, cmap=cm.coolwarm, linewidth=0)
    ax.set_zlim3d(0.0, max(b.flatten()))
else:
    ax = fig.gca(projection='rectilinear')
    h = ax.pcolor(xx/1000.0,yy/1000.0,b,
                  cmap=cm.coolwarm, linewidth=0)

fig.colorbar(h,shrink=0.8,aspect=8)
ax.set_xlabel('x  (km)')
ax.set_ylabel('y  (km)')
ax.set_title('bed elevations  (m)')
plt.show()

