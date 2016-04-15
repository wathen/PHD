//---------------------------------------------------------------------------
//    $Id: product_matrix.cc 27657 2012-11-21 13:19:08Z bangerth $
//
//    Copyright (C) 2005-2006, 2010, 2012 by the deal.II authors
//
//    This file is subject to QPL and may not be distributed without copyright
//    and license information. Please refer to the file
//    deal.II/doc/license.html for the text and further information on this
//    license.
//
//---------------------------------------------------------------------------

// See documentation of ProductMatrix for documentation of this example

#include <deal.II/base/logstream.h>
#include <deal.II/lac/matrix_lib.h>
#include <deal.II/lac/full_matrix.h>
#include <deal.II/lac/vector.h>

using namespace dealii;

double Adata[] =
{
  .5, .1,
  .4, .2
};

double Bdata[] =
{
  .866, .5,
  -.5, .866
};


int main()
{
  FullMatrix<float> A(2,2);
  FullMatrix<double> B(2,2);

  A.fill(Adata);
  B.fill(Bdata);

  GrowingVectorMemory<Vector<double> > mem;

  ProductMatrix<Vector<double> > AB(A,B,mem);

  Vector<double> u(2);
  Vector<double> v(2);

  u(0) = 1.;
  u(1) = 2.;

  AB.vmult(v,u);

  deallog << v(0) << '\t' << v(1) << std::endl;

  AB.Tvmult(v,u);

  deallog << v(0) << '\t' << v(1) << std::endl;
}
