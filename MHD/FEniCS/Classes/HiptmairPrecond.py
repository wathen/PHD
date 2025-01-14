import petsc4py
import sys
petsc4py.init(sys.argv)
from petsc4py import PETSc
import numpy as np
from scipy.sparse import csr_matrix, tril, triu, linalg


class BaseMyPC(object):

    def setup(self, pc):
        pass

    def reset(self, pc):
        pass

    def apply(self, pc, x, y):
        raise NotImplementedError

    def applyT(self, pc, x, y):
        self.apply(pc, x, y)

    def applyS(self, pc, x, y):
        self.apply(pc, x, y)

    def applySL(self, pc, x, y):
        self.applyS(pc, x, y)

    def applySR(self, pc, x, y):
        self.applyS(pc, x, y)

    def applyRich(self, pc, x, y, w, tols):
        self.apply(pc, x, y)


class GSvector(BaseMyPC):

    def __init__(self, G, P, kspVector, kspScalar, diag):
        self.G = G
        self.P = P
        self.kspVector = kspVector
        self.kspScalar = kspScalar
        self.diag = diag

    # def create(self, pc):
    #     print "created"

    # def setUp(self, pc):
    #     print "setup"
    def apply(self, pc, x, y):


        xhat = self.P[0].getVecRight()
        self.P[0].multTranspose(x, xhat)
        yp1 = self.P[0].getVecLeft()
        yhat = self.P[0].getVecRight()
        self.kspVector.solve(xhat, yhat)
        self.P[0].mult(yhat, yp1)
        xhat.destroy()
        yhat.destroy()

        xhat = self.P[1].getVecRight()
        self.P[1].multTranspose(x, xhat)
        yp2 = self.P[1].getVecLeft()
        yhat = self.P[1].getVecRight()
        self.kspVector.solve(xhat, yhat)
        self.P[1].mult(yhat, yp2)
        xhat.destroy()
        yhat.destroy()

        if len(self.P) == 3:
            xhat = self.P[2].getVecRight()
            self.P[2].multTranspose(x, xhat)
            yp3 = self.P[2].getVecLeft()
            yhat = self.P[2].getVecRight()
            self.kspVector.solve(xhat, yhat)
            self.P[2].mult(yhat, yp3)

        xhat = self.G.getVecRight()
        self.G.multTranspose(x, xhat)
        yg = self.G.getVecLeft()
        yhat = self.G.getVecRight()
        self.kspScalar.solve(xhat, yhat)
        self.G.mult(yhat, yg)
        xhat.destroy()
        yhat.destroy()
        xx = x.duplicate()
        self.diag.solve(x, xx)
        # xx.pointwiseMult(self.diag, x)
        # print xx.norm()
        # print yp1.norm()
        # print yp2.norm()
        # print yp3.norm()
        # print yg.norm()
        # sss
        if len(self.P) == 2:
            y.array = (xx.array+yp1.array+yp2.array+yg.array)
        else:
            y.array = (xx.array+yp1.array+yp2.array+yp3.array+yg.array)


class SGS(BaseMyPC):

    def __init__(self, A):
        self.A = A

    def create(self, pc):
        ai, aj, av = self.A.getValuesCSR()
        # print ai, aj, av
        self.Asp = csr_matrix((av, aj, ai))
        self.L = tril(self.Asp)
        self.U = triu(self.Asp)

    def apply(self, pc, x, y):
        x = x.array
        # L = x.duplicate()
        # L = L.array
        # Lt = x.duplicate()
        # Lt = Lt.array
        # ALt = x.duplicate()
        # ALt = ALt.array
        # LALt = x.duplicate()
        # LALt = LALt.array

        L = linalg.spsolve(self.L, x)
        Lt = linalg.spsolve(self.U, x)
        LALt = linalg.spsolve(self.L, self.Asp*Lt)


        # self.kspL.solve(x, L)
        # self.kspT.solve(x, Lt)
        # self.A.mult(Lt, ALt)
        # self.kspL.solve(ALt, LALt)
        y.array = L + Lt + LALt
