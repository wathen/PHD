#!/usr/bin/python

# interpolate scalar gradient onto nedelec space

import petsc4py
import sys

petsc4py.init(sys.argv)

from petsc4py import PETSc
from dolfin import *
# from MatrixOperations import *
import numpy as np
import PETScIO as IO
import common
import scipy
import scipy.io
import time
import scipy.sparse as sp
import BiLinear as forms
import IterOperations as Iter
import MatrixOperations as MO
import CheckPetsc4py as CP
import Solver as S
import MHDmatrixPrecondSetup as PrecondSetup
import NSprecondSetup
import MHDprec as MHDpreconditioner
import gc
import MHDmulti
import MHDmatrixSetup as MHDsetup
import HartmanChannel
# import matplotlib.pyplot as plt
#@profile
m = 6

set_log_active(False)
errL2u = np.zeros((m-1,1))
errH1u = np.zeros((m-1,1))
errL2p = np.zeros((m-1,1))
errL2b = np.zeros((m-1,1))
errCurlb = np.zeros((m-1,1))
errL2r = np.zeros((m-1,1))
errH1r = np.zeros((m-1,1))



l2uorder =  np.zeros((m-1,1))
H1uorder =np.zeros((m-1,1))
l2porder =  np.zeros((m-1,1))
l2border =  np.zeros((m-1,1))
Curlborder = np.zeros((m-1,1))
l2rorder =  np.zeros((m-1,1))
H1rorder = np.zeros((m-1,1))

NN = np.zeros((m-1,1))
DoF = np.zeros((m-1,1))
Velocitydim = np.zeros((m-1,1))
Magneticdim = np.zeros((m-1,1))
Pressuredim = np.zeros((m-1,1))
Lagrangedim = np.zeros((m-1,1))
Wdim = np.zeros((m-1,1))
iterations = np.zeros((m-1,1))
SolTime = np.zeros((m-1,1))
udiv = np.zeros((m-1,1))
MU = np.zeros((m-1,1))
level = np.zeros((m-1,1))
NSave = np.zeros((m-1,1))
Mave = np.zeros((m-1,1))
TotalTime = np.zeros((m-1,1))
DimSave = np.zeros((m-1,4))

dim = 2
ShowResultPlots = 'yes'
split = 'Linear'
MU[0] = 1e0
K = [1., 10., 100.]
K = [1., 1./10, 1./100]

TableValues = np.zeros((m-1,len(K)*3))
SolTime = np.zeros((m-1,len(K)*3))
Decouple = ["P", "MD", "CD"]
print len(K)
# ii = 0
for xx in xrange(1,m):
    ii = 0
    for i in xrange(0,len(K)):
        MU = K[i]
        for j in range(0, 3):

            DecoupleType = Decouple[j]
            IterType = Decouple[j]
            print xx
            level[xx-1] = xx + 3
            nn = 2**(level[xx-1])

            # Create mesh and define function space
            nn = int(nn)
            NN[xx-1] = nn/2
            L = 10.
            y0 = 2.
            z0 = 1.
            mesh, boundaries, domains = HartmanChannel.Domain(nn)

            parameters['form_compiler']['quadrature_degree'] = -1
            order = 2
            parameters['reorder_dofs_serial'] = False
            Velocity = VectorElement("CG", mesh.ufl_cell(), order)
            Pressure = FiniteElement("CG", mesh.ufl_cell(), order-1)
            Magnetic = FiniteElement("N1curl", mesh.ufl_cell(), order-1)
            Lagrange = FiniteElement("CG", mesh.ufl_cell(), order-1)

            VelocityF = VectorFunctionSpace(mesh, "CG", order)
            PressureF = FunctionSpace(mesh, "CG", order-1)
            MagneticF = FunctionSpace(mesh, "N1curl", order-1)
            LagrangeF = FunctionSpace(mesh, "CG", order-1)
            W = FunctionSpace(mesh, MixedElement([Velocity, Pressure, Magnetic,Lagrange]))

            Velocitydim[xx-1] = W.sub(0).dim()
            Pressuredim[xx-1] = W.sub(1).dim()
            Magneticdim[xx-1] = W.sub(2).dim()
            Lagrangedim[xx-1] = W.sub(3).dim()
            Wdim[xx-1] = W.dim()

            print "\n\nW:  ",Wdim[xx-1],"Velocity:  ",Velocitydim[xx-1],"Pressure:  ",Pressuredim[xx-1],"Magnetic:  ",Magneticdim[xx-1],"Lagrange:  ",Lagrangedim[xx-1],"\n\n"

            dim = [W.sub(0).dim(), W.sub(1).dim(), W.sub(2).dim(), W.sub(3).dim()]

            def boundary(x, on_boundary):
                return on_boundary

            FSpaces = [VelocityF,PressureF,MagneticF,LagrangeF]
            DimSave[xx-1,:] = np.array(dim)

            kappa = 1.0
            Mu_m = 10.0
            # MU = 1.0

            N = FacetNormal(mesh)

            # IterType = 'Full'

            params = [kappa,Mu_m,MU]
            n = FacetNormal(mesh)
            trunc = 4
            u0, p0, b0, r0, pN, Laplacian, Advection, gradPres, NScouple, CurlCurl, gradLagr, Mcouple = HartmanChannel.ExactSolution(mesh, params)
            # kappa = 0.0
            # params = [kappa,Mu_m,MU]

            MO.PrintStr("Seting up initial guess matricies",2,"=","\n\n","\n")
            BCtime = time.time()
            BC = MHDsetup.BoundaryIndices(mesh)
            MO.StrTimePrint("BC index function, time: ", time.time()-BCtime)
            Hiptmairtol = 1e-6
            HiptmairMatrices = PrecondSetup.MagneticSetup(mesh, Magnetic, Lagrange, b0, r0, Hiptmairtol, params)

            MO.PrintStr("Setting up MHD initial guess",5,"+","\n\n","\n\n")
            print params
            F_NS = -MU*Laplacian + Advection + gradPres - kappa*NScouple
            if kappa == 0.0:
                F_M = Mu_m*CurlCurl + gradLagr - kappa*Mcouple
            else:
                F_M = Mu_m*kappa*CurlCurl + gradLagr - kappa*Mcouple
            u_k, p_k = HartmanChannel.Stokes(Velocity, Pressure, F_NS, u0, pN, params, mesh, boundaries, domains)
            b_k, r_k = HartmanChannel.Maxwell(Magnetic, Lagrange, F_M, b0, r0, params, mesh, HiptmairMatrices, Hiptmairtol)


            (u, p, b, r) = TrialFunctions(W)
            (v, q, c, s) = TestFunctions(W)
            if kappa == 0.0:
                m11 = params[1]*inner(curl(b),curl(c))*dx
            else:
                r
                m11 = params[1]*params[0]*inner(curl(b),curl(c))*dx
            m21 = inner(c,grad(r))*dx
            m12 = inner(b,grad(s))*dx

            a11 = params[2]*inner(grad(v), grad(u))*dx
            O = inner((grad(u)*u_k),v)*dx + (1./2)*div(u_k)*inner(u,v)*dx - (1./2)*inner(u_k,n)*inner(u,v)*ds
            a12 = -div(v)*p*dx
            a21 = -div(u)*q*dx

            CoupleT = params[0]*(v[0]*b_k[1]-v[1]*b_k[0])*curl(b)*dx
            Couple = -params[0]*(u[0]*b_k[1]-u[1]*b_k[0])*curl(c)*dx

            if DecoupleType == "P":
                a = m11 + m12 + m21 + a11 + O + a21 + a12 + Couple + CoupleT
            elif DecoupleType == "MD":
                a = m11 + m12 + m21 + a11 + O + a21 + a12
            elif DecoupleType == "CD":
                a = m11 + m12 + m21 + a11 + a21 + a12


            Lns  = inner(v, F_NS)*dx #- inner(pN*n,v)*ds(2)
            Lmaxwell  = inner(c, F_M)*dx
            if kappa == 0.0:
                m11 = params[1]*params[0]*inner(curl(b_k),curl(c))*dx
            else:
                m11 = params[1]*inner(curl(b_k),curl(c))*dx
            m21 = inner(c,grad(r_k))*dx
            m12 = inner(b_k,grad(s))*dx

            a11 = params[2]*inner(grad(v), grad(u_k))*dx + inner((grad(u_k)*u_k),v)*dx + (1./2)*div(u_k)*inner(u_k,v)*dx - (1./2)*inner(u_k,n)*inner(u_k,v)*ds
            a12 = -div(v)*p_k*dx
            a21 = -div(u_k)*q*dx

            CoupleT = params[0]*(v[0]*b_k[1]-v[1]*b_k[0])*curl(b_k)*dx
            Couple = -params[0]*(u_k[0]*b_k[1]-u_k[1]*b_k[0])*curl(c)*dx

            L = Lns + Lmaxwell - (m11 + m12 + m21 + a11 + a21 + a12 + Couple + CoupleT)

            ones = Function(PressureF)
            ones.vector()[:]=(0*ones.vector().array()+1)
            pConst = - assemble(p_k*dx)/assemble(ones*dx)
            p_k.vector()[:] += - assemble(p_k*dx)/assemble(ones*dx)
            x = Iter.u_prev(u_k,p_k,b_k,r_k)

            KSPlinearfluids, MatrixLinearFluids = PrecondSetup.FluidLinearSetup(PressureF, MU, mesh)
            kspFp, Fp = PrecondSetup.FluidNonLinearSetup(PressureF, MU, u_k, mesh)

            IS = MO.IndexSet(W, 'Blocks')

            eps = 1.0           # error measure ||u-u_k||
            tol = 1.0E-4         # tolerance
            iter = 0            # iteration counter
            maxiter = 50       # max no of iterations allowed
            SolutionTime = 0
            outer = 0
            # parameters['linear_algebra_backend'] = 'uBLAS'

            u_is = PETSc.IS().createGeneral(W.sub(0).dofmap().dofs())
            b_is = PETSc.IS().createGeneral(W.sub(2).dofmap().dofs())
            NS_is = PETSc.IS().createGeneral(range(VelocityF.dim()+PressureF.dim()))
            M_is = PETSc.IS().createGeneral(range(VelocityF.dim()+PressureF.dim(),W.dim()))

            OuterTol = 1e-5
            InnerTol = 1e-5
            NSits = 0
            Mits = 0
            TotalStart = time.time()
            SolutionTime = 0


            while eps > tol  and iter < maxiter:
                iter += 1
                MO.PrintStr("Iter "+str(iter),7,"=","\n\n","\n\n")

                bcu = DirichletBC(W.sub(0),Expression(("0.0","0.0"), degree=4), boundary)
                #bcu = DirichletBC(W.sub(0),Expression(("0.0","0.0")), boundary)
                bcb = DirichletBC(W.sub(2),Expression(("0.0","0.0"),degree=4), boundary)
                bcr = DirichletBC(W.sub(3),Expression("0.0",degree=4), boundary)
                bcs = [bcu,bcb,bcr]
                initial = Function(W)
                R = action(a,initial);
                DR = derivative(R, initial);
                A, b = assemble_system(a, L, bcs)
                A, b = CP.Assemble(A,b)
                u = b.duplicate()
                u.setRandom()
                print "                               Max rhs = ",np.max(b.array)

                kspFp, Fp = PrecondSetup.FluidNonLinearSetup(PressureF, MU, u_k, mesh)
                # b_t = TrialFunction(Velocity)
                # c_t = TestFunction(Velocity)
                # n = FacetNormal(mesh)
                # mat =  as_matrix([[b_k[1]*b_k[1],-b_k[1]*b_k[0]],[-b_k[1]*b_k[0],b_k[0]*b_k[0]]])
                # aa = params[2]*inner(grad(b_t), grad(c_t))*dx(W.mesh()) + inner((grad(b_t)*u_k),c_t)*dx(W.mesh()) +(1./2)*div(u_k)*inner(c_t,b_t)*dx(W.mesh()) - (1./2)*inner(u_k,n)*inner(c_t,b_t)*ds(W.mesh())+kappa/Mu_m*inner(mat*b_t,c_t)*dx(W.mesh())
                # ShiftedMass = assemble(aa)
                # bcu.apply(ShiftedMass)
                # ShiftedMass = CP.Assemble(ShiftedMass)
                ShiftedMass = A.getSubMatrix(u_is, u_is)
                kspF = NSprecondSetup.LSCKSPnonlinear(ShiftedMass)
                Options = 'p4'

                stime = time.time()
                u, mits,nsits = S.solve(A,b,u,params,W,'Direct',IterType,OuterTol,InnerTol,HiptmairMatrices,Hiptmairtol,KSPlinearfluids, Fp,kspF)
                MO.PrintStr(str((u).norm()),70,"=","\n\n","\n\n")

                Soltime = time.time() - stime
                MO.StrTimePrint("MHD solve, time: ", Soltime)
                Mits += mits
                NSits += mits
                SolutionTime += Soltime
                # u = IO.arrayToVec(  u)

                u1, p1, b1, r1, eps = Iter.PicardToleranceDecouple(u,x,FSpaces,dim,"2",iter)
                p1.vector()[:] += - assemble(p1*dx)/assemble(ones*dx)
                u_k.assign(u1)
                p_k.assign(p1)
                b_k.assign(b1)
                r_k.assign(r1)
                uOld = np.concatenate((u_k.vector().array(),p_k.vector().array(),b_k.vector().array(),r_k.vector().array()), axis=0)
                x = IO.arrayToVec(uOld)
                if eps > 1e10:
                    iter = 100000
                    break

                # b = assemble(L)
                # for bc in bcs:
                #     bc.apply(b)


                # print (b-A*u).norm()
                # sss
            # iter = 1

            SolTime[xx-1, ii] = SolutionTime/iter
            NSave[xx-1] = (float(NSits)/iter)
            Mave[xx-1] = (float(Mits)/iter)
            iterations[xx-1] = iter
            TotalTime[xx-1] = time.time() - TotalStart

            XX= np.concatenate((u_k.vector().array(),p_k.vector().array(),b_k.vector().array(),r_k.vector().array()), axis=0)

            ExactSolution = [u0,p0,b0,r0]
            TableValues[xx-1, ii] = iter
            ii += 1

        print TableValues
        print SolTime

import pandas as pd
print "\n\n   Iterations"
IterTitles = ["l","DoF"] + ["P", "MD", "CD"] + ["P", "MD", "CD"] + ["P", "MD", "CD"]
IterValues = np.concatenate((level,Wdim,TableValues),axis=1)
IterTable= pd.DataFrame(IterValues, columns = IterTitles)
print IterTable.to_latex()

print "\n\n   Time"
IterValues = np.concatenate((level,Wdim,SolTime),axis=1)
IterTable= pd.DataFrame(IterValues, columns = IterTitles)
print IterTable.to_latex()
MO.StoreMatrix(DimSave, "dim")


# {\setlength{\tabcolsep}{.21em} \begin{table}
#     \begin{tabular}{|cc|ccc|ccc|ccc|ccc|}
# \hline
# \multicolumn{2}{|c|}{} & \multicolumn{3}{|c|}{tol$_{\rm L}=$1e-6} & \multicolumn{3}{|c|}{tol$_{\rm L}=$1e-5} & \multicolumn{3}{|c|}{tol$_{\rm L}=$1e-4} & \multicolumn{3}{|c|}{tol$_{\rm L}=$1e-3} \\
# $\ell$ & DoF & it$_{\rm NL}$ & it$_{\rm L}$ & time & it$_{\rm NL}$ & it$_{\rm L}$ & time & it$_{\rm NL}$ &  it$_{\rm L}$ & time & it$_{\rm NL}$ & tol$_{\rm L}$ & time \\
# \hline
# 4 & 3,556  & 5 & 36.0 & 2.16 & 6 & 33.2 & 2.49 & - & 29.0 & 9.40 & - & 25.1 & 8.48 \\
# 5 & 13,764  & 5 & 42.0 & 13.58 & 5 & 37.8 & 12.12 & 12 & 33.7 & 26.69 & - & 29.8 & 48.09 \\
# 6 & 54,148  & 5 & 42.2 & 57.71 & 5 & 39.4 & 54.47 & 5 & 37.2 & 51.36 & - & 31.4 & 220.93 \\
# 7 & 214,788  & 5 & 51.6 & 299.48 & 5 & 41.2 & 240.10 & 5 & 38.6 & 231.42 & - & 32.9 & 985.58 \\
# 8 & 855,556  & 5 & 52.4 & 1485.20 & 5 & 42.0 & 1230.99 & 5 & 35.6 & 1126.70 & 20 & 35.0 & 4306.09 \\
# \hline
# \end{tabular}
# \caption{tol$_{\rm NL}=$1e-4}
# \end{table}}