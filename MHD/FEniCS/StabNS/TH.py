

import petsc4py
import sys
petsc4py.init(sys.argv)
from petsc4py import PETSc

from dolfin import *
# from MatrixOperations import *
import numpy as np
import os
import scipy.io
#from PyTrilinos import Epetra, EpetraExt, AztecOO, ML, Amesos
#from scipy2Trilinos import scipy_csr_matrix2CrsMatrix
import PETScIO as IO
import time
import common
import CheckPetsc4py as CP
import NSpreconditioner
from scipy.sparse import  spdiags
import MatrixOperations as MO
import ExactSol
import NSprecondSetup
import matplotlib.pylab as plt
import sympy as sy
# parameters["form_compiler"]["optimize"]     = True
# parameters["form_compiler"]["cpp_optimize"] = True
#MO.SwapBackend('epetra')
#os.system("echo $PATH")

m = 5
errL2u =np.zeros((m-1,1))
errH1u =np.zeros((m-1,1))
errL2p =np.zeros((m-1,1))

l2uorder =  np.zeros((m-1,1))
H1uorder =np.zeros((m-1,1))
l2porder =  np.zeros((m-1,1))
NN = np.zeros((m-1,1))
DoF = np.zeros((m-1,1))
Vdim = np.zeros((m-1,1))
Qdim = np.zeros((m-1,1))
Wdim = np.zeros((m-1,1))
l2uorder = np.zeros((m-1,1))
l2porder = np.zeros((m-1,1))
nonlinear = np.zeros((m-1,1))
SolTime = np.zeros((m-1,1))
AvIt = np.zeros((m-1,1))
NonLinearIts = np.zeros((m-1,1))
nn = 2

dim = 2
Solver = 'PCD'
Saving = 'no'
case = 4
# parameters['linear_algebra_backend'] = 'uBLAS'
# parameters = CP.ParameterSetup()
def LOG(arg):
    if INFO:
        print(arg)

x = sy.Symbol('x')
y = sy.Symbol('y')

# u = sy.diff(x,x)
# v = sy.diff(x,x)
# p = sy.diff(x,x)

u = y**2
v = x**2
p = x*y
p = sy.sin(x)*sy.exp(y)
uu = y*x*sy.exp(x+y)
u = sy.diff(uu,y)
v = -sy.diff(uu,x)

kappa = 1.0
Mu_m = float(1e4)
MU = 1.0
params = [kappa,Mu_m,MU]

# G = 10.
# Re = 1./params[2]
# Ha = sqrt(params[0]/(params[1]*params[2]))

# p = -G*x - (G**2)/(2*params[0])*(sy.sinh(y*Ha)/sy.sinh(Ha)-y)**2
# u = (G/(params[2]*Ha*sy.tanh(Ha)))*(1-sy.cosh(y*Ha)/sy.cosh(Ha))
# v = sy.diff(x,y)

L1 = sy.diff(u,x,x) + sy.diff(u,y,y)
L2 = sy.diff(v,x,x) + sy.diff(v,y,y)

print "u=(", u,",", v,")"
print "p=(", p,")"

P1 = sy.diff(p,x)
P2 = sy.diff(p,y)

A1 = u*sy.diff(u,x)+v*sy.diff(u,y)
A2 = u*sy.diff(v,x)+v*sy.diff(v,y)

F1 = -L1 + P1 + A1
F2 = -L2 + P2 + A1
print F1
print F2
J11 = p - sy.diff(u,x)
J12 = - sy.diff(u,y)
J21 = - sy.diff(v,x)
J22 = p - sy.diff(v,y)
# sss
p = sy.lambdify((x, y), p, "numpy")
u = sy.lambdify((x, y), u, "numpy")
v = sy.lambdify((x, y), v, "numpy")

F1 = sy.lambdify((x, y), F1, "numpy")
F2 = sy.lambdify((x, y), F2, "numpy")
J11 = sy.lambdify((x, y), J11, "numpy")
J12 = sy.lambdify((x, y), J12, "numpy")
J21 = sy.lambdify((x, y), J21, "numpy")
J22 = sy.lambdify((x, y), J22, "numpy")

class u_in(Expression):
    def __init__(self, u ,v):
        self.u = u
        self.v = v
    def eval_cell(self, values, x, ufc_cell):
        values[0] = x[0]*x[1]*exp(x[0] + x[1]) + x[0]*exp(x[0] + x[1])
        values[1] = -x[0]*x[1]*exp(x[0] + x[1]) - x[1]*exp(x[0] + x[1])
        # values[0] = self.u(x[0],x[1])
        # values[1] = self.v(x[0],x[1])

    def value_shape(self):
        return (2,)


class p_in(Expression):
    def __init__(self, p):
        self.p = p
    def eval_cell(self, values, x, ufc_cell):
        values[0] = exp(x[1])*sin(x[0])
        # values[0] = self.p(x[0],x[1])

class f_in(Expression):
    def __init__(self, F1, F2):
        self.F1 = F1
        self.F2 = F2

    def eval_cell(self, values, x, ufc_cell):
        values[0] = -x[0]*(x[1] + 3)*exp(x[0] + x[1]) + (-x[0]*x[1]*exp(x[0] + x[1]) - x[1]*exp(x[0] + x[1]))*(x[0]*x[1]*exp(x[0] + x[1]) + 2*x[0]*exp(x[0] + x[1])) + (x[0]*x[1]*exp(x[0] + x[1]) + x[0]*exp(x[0] + x[1]))*(x[0]*x[1]*exp(x[0] + x[1]) + x[0]*exp(x[0] + x[1]) + x[1]*exp(x[0] + x[1]) + exp(x[0] + x[1])) - (x[0]*x[1] + x[0] + 2*x[1] + 2)*exp(x[0] + x[1]) + exp(x[1])*cos(x[0])

        values[1] = x[1]*(x[0] + 3)*exp(x[0] + x[1]) + (-x[0]*x[1]*exp(x[0] + x[1]) - x[1]*exp(x[0] + x[1]))*(x[0]*x[1]*exp(x[0] + x[1]) + 2*x[0]*exp(x[0] + x[1])) + (x[0]*x[1]*exp(x[0] + x[1]) + x[0]*exp(x[0] + x[1]))*(x[0]*x[1]*exp(x[0] + x[1]) + x[0]*exp(x[0] + x[1]) + x[1]*exp(x[0] + x[1]) + exp(x[0] + x[1])) + (x[0]*x[1] + 2*x[0] + x[1] + 2)*exp(x[0] + x[1]) + exp(x[1])*sin(x[0])
        # values[0] = self.F1(x[0],x[1])
        # values[1] = self.F2(x[0],x[1])
    def value_shape(self):
        return (2,)

class J(Expression):
    def __init__(self, J11, J12, J21, J22):
        self.J11 = J11
        self.J12 = J12
        self.J21 = J21
        self.J22 = J22

    def eval_cell(self, values, x, ufc_cell):
        values[0] = self.J11(x[0],x[1])
        values[1] = self.J12(x[0],x[1])
        values[2] = self.J21(x[0],x[1])
        values[3] = self.J22(x[0],x[1])
    def value_shape(self):
        return (4,)

f = f_in(F1, F2)
u0 = u_in(u, v)
p0 = p_in(p)

for xx in xrange(1,m):
    print xx
    NN[xx-1] = xx+1
    nn = 2**(NN[xx-1])
    nn = int(nn)
    mesh = UnitSquareMesh(nn,nn)
    # tic()
    parameters["form_compiler"]["quadrature_degree"] = -1

    parameters['reorder_dofs_serial'] = False
    V = VectorFunctionSpace(mesh, "CG", 2)
    Q = FunctionSpace(mesh, "CG", 1)
    mpi_comm = mpi_comm_world()
    my_rank = MPI.rank(mpi_comm)
    num_threads = dolfin.parameters["num_threads"]
    parameters["num_threads"] = 0
    # if my_rank == 0:
    ones = Function(Q)
    ones.vector()[:]=(1)
    print ones.shape
    parameters["num_threads"] = num_threads
    # QQ = VectorFunctionSpace(mesh,"B",3)
    # V = V+QQ
    parameters['reorder_dofs_serial'] = False
    # print 'time to create function spaces', toc(),'\n\n'
    W = V*Q
    Vdim[xx-1] = V.dim()
    Qdim[xx-1] = Q.dim()
    Wdim[xx-1] = W.dim()
    print "\n\nV:  ",Vdim[xx-1],"Q:  ",Qdim[xx-1],"W:  ",Wdim[xx-1],"\n\n"

    def boundary(x, on_boundary):
        return on_boundary


    u0, p0, Laplacian, Advection, gradPres = ExactSol.NS2D(case, mesh)

    R = 10.0
    # MU = Constant(0.01)
    # MU = 1000.0
    MU = 1.0
    bcc = DirichletBC(W.sub(0),u0, boundary)
    bcs = [bcc]

    (u, p) = TrialFunctions(W)
    (v, q) = TestFunctions(W)




    f = -MU*Laplacian+Advection+gradPres


    n = FacetNormal(mesh)
    h = CellSize(mesh)
    h_avg =avg(h)
    d = 0
    u_k,p_k = common.Stokes(V,Q,u0,f,[1,1,MU], FS = "CG",InitialTol = 1e-6)
    # p_k.vector()[:] = p_k.vector().array()
    u_k
    pConst = - assemble(p_k*dx)/assemble(ones*dx)
    p_k.vector()[:] += pConst
    # u_k = Function(V)
    # p_k = Function(Q)
    # plot(u_k)
    # plot(p_k)
    uOld = np.concatenate((u_k.vector().array(),p_k.vector().array()), axis=0)
    r = IO.arrayToVec(uOld)

    a11 = MU*inner(grad(v), grad(u))*dx(mesh) + inner((grad(u)*u_k),v)*dx(mesh) + (1./2)*div(u_k)*inner(u,v)*dx(mesh) - (1./2)*inner(u_k,n)*inner(u,v)*ds(mesh)
    a12 = div(v)*p*dx
    a21 = div(u)*q*dx
    # a22 = 0.1*h_avg*jump(p)*jump(q)*dS
    # L1  = inner(v-0.1*h*h*grad(q), f)*dx
    print inner(v, f).shape()
    L1  = inner(v, f)*dx
    a = a11-a12-a21


    r11 = MU*inner(grad(v), grad(u_k))*dx(mesh) + inner((grad(u_k)*u_k),v)*dx(mesh) + (1./2)*div(u_k)*inner(u_k,v)*dx(mesh) - (1./2)*inner(u_k,n)*inner(u_k,v)*ds(mesh)
    r12 = div(v)*p_k*dx
    r21 = div(u_k)*q*dx
    # r22 = 0.1*h_avg*jump(p_k)*jump(q)*dS
    RHSform = r11-r12-r21
    # -r22
    # RHSform = 0

    p11 = inner(u,v)*dx
    # p12 = div(v)*p*dx
    # p21 = div(u)*q*dx
    p22 = inner(p,q)*dx
    prec = p11 +p22
    bc = DirichletBC(W.sub(0),Expression(("0.0","0.0")), boundary)
    bcs = [bc]

    eps = 1.0           # error measure ||u-u_k||
    tol = 1.0E-4      # tolerance
    iter = 0            # iteration counter
    maxiter = 20        # max no of iterations allowed
    # parameters = CP.ParameterSetup()
    outerit = 0

    if Solver == "LSC":
        parameters['linear_algebra_backend'] = 'uBLAS'
        PP = assemble(inner(u,v)*dx-div(v)*p*dx)
#        bc.apply(PP)
        PP = PP.sparray()
        X = PP[0:V.dim(),0:V.dim()]
        Xdiag = X.diagonal()
        # PP = assemble(-div(v)*p*dx)
        # bc.apply(PP)
        # PP = PP.sparray()
        # Xdiag = X.sum(1).A
        # print Xdiag



        Bt = PP[0:V.dim(),V.dim():W.dim()]
        d = spdiags(1.0/Xdiag, 0, len(Xdiag), len(Xdiag))
        dBt = (d*Bt).tocsr()

        plt.spy(dBt)
        plt.show()
        BQB = Bt.transpose()*dBt
        dBt = PETSc.Mat().createAIJ(size=dBt.shape,csr=(dBt.indptr, dBt.indices, dBt.data))
        print dBt.size
        BQB = PETSc.Mat().createAIJ(size=BQB.tocsr().shape,csr=(BQB.tocsr().indptr, BQB.tocsr().indices, BQB.tocsr().data))
        # parameters['linear_algebra_backend'] = 'PETSc'
        kspBQB = NSprecondSetup.Ksp(BQB)
    elif Solver == "PCD":
        N = FacetNormal(mesh)
        h = CellSize(mesh)
        h_avg =avg(h)
        alpha = 10.0
        gamma =10.0

        (pQ) = TrialFunction(Q)
        (qQ) = TestFunction(Q)
        print MU
        Mass = assemble(inner(pQ,qQ)*dx)
        L = MU*(inner(grad(qQ), grad(pQ))*dx(mesh))

#        O = - dot(u_k, n)*pQ('+')*qQ*ds(mesh) - dot(u_k, n)*(pQ('+') - pQ('-'))*qQ('-')*dS(mesh)
        pp = Function(Q)

        fp = MU*inner(grad(qQ), grad(pQ))*dx(mesh)+inner((u_k[0]*grad(pQ)[0]+u_k[1]*grad(pQ)[1]),qQ)*dx(mesh) + (1/2)*div(u_k)*inner(pQ,qQ)*dx(mesh) - (1/2)*(u_k[0]*n[0]+u_k[1]*n[1])*inner(pQ,qQ)*ds(mesh)


        Laplacian = assemble(L)
        Laplacian = CP.Assemble(Laplacian)
        Mass = CP.Assemble(Mass)
        kspA, kspQ = NSprecondSetup.PCDKSPlinear(Mass,Laplacian)

    u_is = PETSc.IS().createGeneral(range(W.sub(0).dim()))
    p_is = PETSc.IS().createGeneral(range(W.sub(0).dim(),W.sub(0).dim()+W.sub(1).dim()))
    # print L
    SolutionTime = 0
    while eps > tol and iter < maxiter:
        iter += 1
        x = Function(W)

        uu = Function(W)
        tic()
        AA, bb = assemble_system(a, L1-RHSform, bcs)
        A,b = CP.Assemble(AA,bb)
        print toc()
        # b = b.getSubVector(t_is)
        F = A.getSubMatrix(u_is,u_is)

        kspF = NSprecondSetup.Ksp(F)

        x = b.duplicate()
        ksp = PETSc.KSP()
        ksp.create(comm=PETSc.COMM_WORLD)
        # ksp.setTolerances(1e-5)
        ksp.setType('gmres')
        pc = ksp.getPC()


        pc.setType('python')
        if Solver == "PCD":
            Fp = assemble(fp)
            Fp = CP.Assemble(Fp)
            pc.setPythonContext(NSpreconditioner.NSPCD(W, kspF, kspA, kspQ,Fp))
        else:
            pc.setPythonContext(NSpreconditioner.NSLSC(W, kspF, kspBQB, dBt) )
        # elif Solver == "PCD":
            # F = assemble(fp)
            # F = CP.Assemble(F)
        #     pc.setPythonContext(NSprecond.PCD(W, A, Mass, F, L))

        ksp.setOperators(A)
        OptDB = PETSc.Options()
        ksp.max_it = 1000
        # OptDB['pc_factor_shift_amount'] = 1
        # OptDB['pc_factor_mat_ordering_type'] = 'rcm'
        # OptDB['pc_factor_mat_solver_package']  = 'mumps'
        ksp.setFromOptions()


        x = r

        toc()
        ksp.solve(b, x)

        time = toc()
        print time
        MO.StrTimePrint("Solve time",time)
        SolutionTime = SolutionTime +time
        outerit += ksp.its
        print "==============", ksp.its
        # r = bb.duplicate()
        # A.MUlt(x, r)
        # r.aypx(-1, bb)
        # rnorm = r.norm()
        # PETSc.Sys.Print('error norm = %g' % rnorm,comm=PETSc.COMM_WORLD)

        uu = IO.vecToArray(x)
        UU = uu[0:Vdim[xx-1][0]]
        # time = time+toc()
        u1 = Function(V)
        u1.vector()[:] = UU

        pp = uu[V.dim():V.dim()+Q.dim()]
        # time = time+toc()
        p1 = Function(Q)
        # n = pp.shape
        # pend = assemble(pa*dx)


        # pp = Function(Q)
        # p1.vector()[:] = pa.vector().array()- assemble(pa*dx)/assemble(ones*dx)
        p1.vector()[:] =  pp
        p1.vector()[:] += - assemble(p1*dx)/assemble(ones*dx)
        diff = u1.vector().array()
        # print p1.vector().array()
        eps = np.linalg.norm(diff, ord=np.Inf) #+np.linalg.norm(p1.vector().array(),ord=np.Inf)

        print '\n\n\niter=%d: norm=%g' % (iter, eps)
        print np.linalg.norm(p1.vector().array(), ord=np.Inf)
        u2 = Function(V)
        u2.vector()[:] = u1.vector().array() + u_k.vector().array()
        p2 = Function(Q)
        p2.vector()[:] = p1.vector().array() + p_k.vector().array()
        p2.vector()[:] += - assemble(p2*dx)/assemble(ones*dx)
        u_k.assign(u2)
        p_k.assign(p2)

        # plot(p_k)

        uOld = np.concatenate((u_k.vector().array(),p_k.vector().array()), axis=0)
        r = IO.arrayToVec(uOld)
        # plot(p_k)
    SolTime[xx-1] = SolutionTime/iter

    NonLinearIts[xx-1] = iter
    ue = u0
    pe = p0


    AvIt[xx-1] = float(outerit)/iter
    u = interpolate(ue,V)
    p = interpolate(pe,Q)

    ua = Function(V)
    ua.vector()[:] = u_k.vector().array()
    VelocityE = VectorFunctionSpace(mesh,"CG",3)
    u = interpolate(ue,VelocityE)

    PressureE = FunctionSpace(mesh,"CG",2)
    parameters["form_compiler"]["quadrature_degree"] = 5

    Nv  = ua.vector().array().shape

    X = IO.vecToArray(r)
    xu = X[0:V.dim()]
    ua = Function(V)
    ua.vector()[:] = xu

    pp = X[V.dim():V.dim()+Q.dim()]


    n = pp.shape
    pa = Function(Q)
    pa.vector()[:] = pp

    pend = assemble(pa*dx)

    ones = Function(Q)
    ones.vector()[:]=(0*pp+1)
    pp = Function(Q)
    pp.vector()[:] = pa.vector().array()- assemble(pa*dx)/assemble(ones*dx)

    pInterp = interpolate(pe,PressureE)
    pe = Function(PressureE)
    pe.vector()[:] = pInterp.vector().array()
    const = - assemble(pe*dx)/assemble(ones*dx)
    pe.vector()[:] = pe.vector()[:]+const




    ErrorU = Function(V)
    ErrorP = Function(Q)

    ErrorU = ue-ua
    ErrorP = pe-pp


    # errL2u[xx-1]= errornorm(ue, ua, norm_type='L2', degree_rise=8)
    # errH1u[xx-1]= errornorm(ue, ua, norm_type='H10', degree_rise=8)
    # errL2p[xx-1]= errornorm(pe, pp, norm_type='L2', degree_rise=8)

    errL2u[xx-1]= errornorm(ue, ua, norm_type='L2', degree_rise=4)
    # sqrt(abs(assemble(inner(ErrorU, ErrorU)*dx)))
    # errornorm(ue, ua, norm_type='L2', degree_rise=8)
    errH1u[xx-1]= errornorm(ue, ua, norm_type='H10', degree_rise=4)
    # sqrt(abs(assemble(inner(grad(ErrorU), grad(ErrorU))*dx)))
    # errornorm(ue, ua, norm_type='H10', degree_rise=8)
    errL2p[xx-1]= sqrt(abs(assemble(inner(ErrorP, ErrorP)*dx)))
    # errornorm(pe, pp, norm_type='L2', degree_rise=8)

    if xx == 1:
        l2uorder[xx-1] = 0
        l2porder[xx-1] = 0
    else:
        l2uorder[xx-1] =  np.abs(np.log2(errL2u[xx-2]/errL2u[xx-1]))
        H1uorder[xx-1] =  np.abs(np.log2(errH1u[xx-2]/errH1u[xx-1]))

        l2porder[xx-1] =  np.abs(np.log2(errL2p[xx-2]/errL2p[xx-1]))
    print errL2u[xx-1]
    print errL2p[xx-1]
    # del  solver



# print AvIt
# print nonlinear



# print "Velocity Elements rate of convergence ", np.log2(np.average((errL2u[0:m-2]/errL2u[1:m-1])))
# print "Pressure Elements rate of convergence ", np.log2(np.average((errL2p[0:m-2]/errL2p[1:m-1])))


import pandas as pd
# # tableTitles = ["Total DoF","V DoF","Q DoF","AvIt","V-L2","V-order","P-L2","P-order"]
# # tableValues = np.concatenate((Wdim,Vdim,Qdim,AvIt,errL2u,l2uorder,errL2p,l2porder),axis=1)
# # df = pd.DataFrame(tableValues, columns = tableTitles)
# # pd.set_option('precision',3)
# # print df
# # print df.to_latex()

print "\n\n   Velocity convergence"
VelocityTitles = ["Total DoF","V DoF","Soln Time","AvIt","V-L2","L2-order","V-H1","H1-order"]
VelocityValues = np.concatenate((Wdim,Vdim,SolTime,AvIt,errL2u,l2uorder,errH1u,H1uorder),axis=1)
VelocityTable= pd.DataFrame(VelocityValues, columns = VelocityTitles)
pd.set_option('precision',3)
VelocityTable = MO.PandasFormat(VelocityTable,"V-L2","%2.4e")
VelocityTable = MO.PandasFormat(VelocityTable,'V-H1',"%2.4e")
VelocityTable = MO.PandasFormat(VelocityTable,"H1-order","%1.2f")
VelocityTable = MO.PandasFormat(VelocityTable,'L2-order',"%1.2f")
print VelocityTable

print "\n\n   Pressure convergence"
PressureTitles = ["Total DoF","P DoF","Soln Time","AvIt","P-L2","L2-order"]
PressureValues = np.concatenate((Wdim,Qdim,SolTime,AvIt,errL2p,l2porder),axis=1)
PressureTable= pd.DataFrame(PressureValues, columns = PressureTitles)
pd.set_option('precision',3)
PressureTable = MO.PandasFormat(PressureTable,"P-L2","%2.4e")
PressureTable = MO.PandasFormat(PressureTable,'L2-order',"%1.2f")
print PressureTable

# print "\n\n"

# LatexTitles = ["l","DoFu","Dofp","V-L2","L2-order","V-H1","H1-order","P-L2","PL2-order"]
# LatexValues = np.concatenate((NN,Vdim,Qdim,errL2u,l2uorder,errH1u,H1uorder,errL2p,l2porder), axis=1)
# LatexTable = pd.DataFrame(LatexValues, columns = LatexTitles)
# pd.set_option('precision',3)
# LatexTable = MO.PandasFormat(LatexTable,"V-L2","%2.4e")
# LatexTable = MO.PandasFormat(LatexTable,'V-H1',"%2.4e")
# LatexTable = MO.PandasFormat(LatexTable,"H1-order","%1.2f")
# LatexTable = MO.PandasFormat(LatexTable,'L2-order',"%1.2f")
# LatexTable = MO.PandasFormat(LatexTable,"P-L2","%2.4e")
# LatexTable = MO.PandasFormat(LatexTable,'PL2-order',"%1.2f")
# print LatexTable.to_latex()


print "\n\n\n\n"

LatexTitles = ["l","DoFu","Dofp","Soln Time","AvIt","Non-Lin its"]
LatexValues = np.concatenate((NN,Vdim,Qdim, SolTime,AvIt, NonLinearIts), axis=1)
LatexTable = pd.DataFrame(LatexValues, columns = LatexTitles)
pd.set_option('precision',3)
LatexTable = MO.PandasFormat(LatexTable,'AvIt',"%3.1f")
print LatexTable.to_latex()


# plot(ua)
# plot(interpolate(ue,V))

# plot(pp)
# plot(interpolate(p0,Q))

# interactive()

plt.show()

