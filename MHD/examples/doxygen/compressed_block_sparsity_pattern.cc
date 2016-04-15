//---------------------------------------------------------------------------
//    $Id: compressed_block_sparsity_pattern.cc 28377 2013-02-13 15:22:32Z heister $
//
//    Copyright (C) 2006-2007 by the deal.II authors
//
//    This file is subject to QPL and may not be  distributed
//    without copyright and license information. Please refer
//    to the file deal.II/doc/license.html for the  text  and
//    further information on this license.
//
//---------------------------------------------------------------------------

// See documentation of BlockCompressedSparsityPattern for documentation of this example

#include <deal.II/lac/block_sparsity_pattern.h>
#include <deal.II/lac/constraint_matrix.h>
#include <deal.II/grid/tria.h>
#include <deal.II/grid/tria_accessor.h>
#include <deal.II/grid/tria_iterator.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/fe/fe_q.h>
#include <deal.II/fe/fe_system.h>
#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_renumbering.h>
#include <deal.II/dofs/dof_tools.h>

#include <iostream>

using namespace dealii;

int main()
{
  Triangulation<2> tr;
  GridGenerator::subdivided_hyper_cube(tr, 3);
  tr.begin_active()->set_refine_flag();
  tr.execute_coarsening_and_refinement();

  FE_Q<2> fe1(1);
  FE_Q<2> fe2(2);
  FESystem<2> fe(fe1, 2, fe2, 1);

  DoFHandler<2> dof(tr);
  dof.distribute_dofs(fe);
  DoFRenumbering::Cuthill_McKee(dof);
  DoFRenumbering::component_wise(dof);

  ConstraintMatrix constraints;
  DoFTools::make_hanging_node_constraints(dof, constraints);
  constraints.close();

  std::vector<unsigned int> dofs_per_block(fe.n_blocks());
  DoFTools::count_dofs_per_block(dof, dofs_per_block);

  BlockCompressedSparsityPattern c_sparsity(fe.n_blocks(), fe.n_blocks());
  for (unsigned int i=0; i<fe.n_blocks(); ++i)
    for (unsigned int j=0; j<fe.n_blocks(); ++j)
      c_sparsity.block(i,j).reinit(dofs_per_block[i],dofs_per_block[j]);
  c_sparsity.collect_sizes();

  DoFTools::make_sparsity_pattern(dof, c_sparsity);
  constraints.condense(c_sparsity);

  BlockSparsityPattern sparsity;
  sparsity.copy_from(c_sparsity);

  unsigned int ig = 0;
  for (unsigned int ib=0; ib<fe.n_blocks(); ++ib)
    for (unsigned int i=0; i<dofs_per_block[ib]; ++i,++ig)
      {
        unsigned int jg = 0;
        for (unsigned int jb=0; jb<fe.n_blocks(); ++jb)
          for (unsigned int j=0; j<dofs_per_block[jb]; ++j,++jg)
            {
              if (sparsity.exists(ig,jg))
                std::cout << ig << ' ' << jg
                          << '\t' << ib << jb << std::endl;
            }
      }
}
