"""Deterministic bright/dark line-center candidates from Gaussian derivatives.

Research geometry only. The reviewed binary object mask remains the boundary;
no inferred source feature can expand it. No trained model or semantic ROI.
"""
import math
import numpy as np
from scipy import ndimage as ndi


def ridge_field(source,sigma=.65):
    luma=source[:,:,:3]@np.array([.2126,.7152,.0722])
    smooth=ndi.gaussian_filter(luma,sigma,mode='nearest')
    gx=ndi.gaussian_filter(luma,sigma,order=(0,1),mode='nearest')
    gy=ndi.gaussian_filter(luma,sigma,order=(1,0),mode='nearest')
    xx=ndi.gaussian_filter(luma,sigma,order=(0,2),mode='nearest')
    xy=ndi.gaussian_filter(luma,sigma,order=(1,1),mode='nearest')
    yy=ndi.gaussian_filter(luma,sigma,order=(2,0),mode='nearest')
    discriminant=np.sqrt((xx-yy)**2+4*xy**2)
    plus=.5*(xx+yy+discriminant);minus=.5*(xx+yy-discriminant)
    angle=.5*np.arctan2(2*xy,xx-yy)
    select=np.abs(minus)>np.abs(plus)
    angle+=select*np.pi/2
    normal_x,normal_y=np.cos(angle),np.sin(angle)
    eigen=np.where(select,minus,plus)
    offset=-(gx*normal_x+gy*normal_y)/np.where(np.abs(eigen)>1e-7,eigen,1.)
    y,x=np.mgrid[:luma.shape[0],:luma.shape[1]]
    # A real ridge is brighter/darker than both sides. A step edge is not a
    # line center merely because one Hessian eigenvalue happens to be large.
    center=ndi.map_coordinates(smooth,[y+offset*normal_y,x+offset*normal_x],order=1,mode='nearest')
    left=ndi.map_coordinates(smooth,[y+(offset-1.5)*normal_y,x+(offset-1.5)*normal_x],order=1,mode='nearest')
    right=ndi.map_coordinates(smooth,[y+(offset+1.5)*normal_y,x+(offset+1.5)*normal_x],order=1,mode='nearest')
    contrast=np.where(eigen<0,np.minimum(center-left,center-right),np.minimum(left-center,right-center))
    eligible=(np.abs(offset)<=.85)&(np.abs(eigen)>.012)&(contrast>.012)
    return dict(source=source,trace_color=ndi.gaussian_filter(source[:,:,:3],(sigma,sigma,0),mode='nearest'),nx=normal_x,ny=normal_y,offset=offset,eigen=eigen,
                eligible=eligible,strength=np.maximum(0,contrast)*eligible)


def trace_ridge(seed,field,mask,max_length=32):
    h,w=mask.shape
    def snap(point):
        x,y=point
        if x<0 or y<0 or x>=w or y>=h:return None
        # Search a local 3x3 neighborhood for a compatible, evidenced center.
        ix,iy=int(x),int(y);choices=[]
        for py in range(max(0,iy-1),min(h,iy+2)):
            for px in range(max(0,ix-1),min(w,ix+2)):
                if not field['eligible'][py,px]:continue
                nx,ny=field['nx'][py,px],field['ny'][py,px]
                correction=field['offset'][py,px]-((x-px-.5)*nx+(y-py-.5)*ny)
                qx,qy=x+correction*nx,y+correction*ny
                distance=math.hypot(qx-x,qy-y)
                if distance<=1.1 and 0<=qx<w and 0<=qy<h and mask[int(qy),int(qx)]:
                    choices.append((distance,qx,qy,-ny,nx,1 if field['eigen'][py,px]>0 else -1))
        return min(choices) if choices else None
    initial=snap(seed)
    if initial is None:return None
    _,x,y,tx,ty,polarity=initial
    start_color=field['trace_color'][int(y),int(x)]
    branches=[];step=.65
    for direction in (-1,1):
        path=[(x,y)];dx,dy=tx*direction,ty*direction
        for _ in range(int(max_length/step/2)):
            previous=path[-1];candidate=snap((previous[0]+step*dx,previous[1]+step*dy))
            if candidate is None:break
            _,qx,qy,ex,ey,sign=candidate
            if sign!=polarity:break
            if ex*dx+ey*dy<0:ex,ey=-ex,-ey
            if ex*dx+ey*dy<.7:break
            if math.hypot(qx-previous[0],qy-previous[1])<.25:break
            if np.max(np.abs(field['trace_color'][int(qy),int(qx)]-start_color))>.14:break
            segment=np.linspace(previous,(qx,qy),max(3,int(math.hypot(qx-previous[0],qy-previous[1])*4)+2))
            if any(not mask[int(py),int(px)] for px,py in segment):break
            if len(path)>5 and math.hypot(qx-x,qy-y)<step:break
            path.append((qx,qy));dx,dy=ex,ey
        branches.append(path)
    path=list(reversed(branches[0][1:]))+branches[1]
    length=sum(math.hypot(b[0]-a[0],b[1]-a[1]) for a,b in zip(path,path[1:]))
    if length<3:return None
    n=len(path)
    return [(x,y,.65+.35*min(1,min(i,n-1-i)/2)) for i,(x,y) in enumerate(path)]
